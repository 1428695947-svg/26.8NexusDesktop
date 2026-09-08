/**
  ******************************************************************************
  * @file    app_mouse.c
  * @brief   鼠标/输入模块 - 鼠标ADC读取任务实现 (任务划分之一)
  * @note    摇杆 ADC 采样任务 + 鼠标坐标/按下状态维护 + 输入事件队列消费。
  *          模块内综合最终任务函数入口 App_MouseTask(), 由 freertos.c 创建调用。
  *          与 LVGL 图形刷新任务通过 g_mouse_x/y/pressed 共享坐标。
  ******************************************************************************
  */

#include "app_mouse.h"

#include "cmsis_os.h"
#include "FreeRTOS.h"
#include "queue.h"
#include "tim.h"

#include "joystick.h"
#include "app_power.h"
#include "app_health.h"
#include "touch.h"        /* TP_* */
#include "lcd.h"          /* LCD_LED_SET */
#include "gui.h"          /* gui_cursor_set_pos/gui_cursor_hide/gui_update_mouse_conn */
#include "gui_guider.h"   /* gui_lock_screen */
#include "custom.h"
#include "app_log.h"

/* ========================= 鼠标坐标与按下状态 (与 LVGL/画图共享) ========================= */
volatile int     g_mouse_x = 160;
volatile int     g_mouse_y = 240;
volatile uint8_t g_mouse_pressed = 0;

/* 摇杆实例只由输入模块更新，其他应用通过 App_MouseGetVector() 读取快照。 */
static Joystick_HandleTypeDef s_joystick;

typedef struct {
    uint8_t keyId;
    uint8_t eventType;
} KeyEventMsg_t;

#define KEY_EVENT_QUEUE_LENGTH 8U

static QueueHandle_t s_key_event_queue = NULL;
static volatile uint32_t s_key_event_counts[4] = {0U, 0U, 0U, 0U};
static volatile uint8_t s_key_queue_peak = 0U;

static void App_KeyEventPostFromISR(uint8_t key_id, Key_Event_t event);
static void App_KeyPressDownCallback(uint8_t key_id);
static void App_KeyClickCallback(uint8_t key_id);
static void App_KeyDoubleClickCallback(uint8_t key_id);
static void App_KeyReleaseCallback(uint8_t key_id);
static void App_KeyEventHandler(const KeyEventMsg_t *msg);

/* ========================= 输入状态 ========================= */
static volatile uint8_t s_pa2_btn_down         = 0; /* PA2 左键是否按下 */
static volatile uint8_t s_click_pending        = 0; /* 单击确认事件待处理 */
static volatile uint8_t s_double_click_pending = 0; /* 双击(右键)事件待处理 */
static uint8_t          s_touch_down = 0;           /* 触摸是否按下 */
static uint8_t          s_tp_prev    = 0;           /* 触摸上一周期是否按下 */
static volatile uint8_t s_mouse_connected = 1;      /* 摇杆是否已连接 */
static uint8_t          s_conn_check_cnt = 0;       /* 连接检测计数 */

static float s_mouse_speed = 7.5f;            /* 默认灵敏度5对应的摇杆移动速度 */

/* ========================= 输入事件队列 =========================
 * 触摸输入先转为事件, 由 LVGL 主循环(App_ProcessInputEvents)统一消费;
 * 实体按键 PA2 由 key.c V2.0 判定后经 FreeRTOS 队列 -> App_KeyEventHandler
 * 设置 s_pa2_btn_down 等标志, 同样由 LVGL 任务消费。
 * - 线程安全: 单核 + __disable_irq 保护, 环形缓冲。
 */
typedef enum {
    IN_EV_PRESS   = 1,
    IN_EV_RELEASE = 2
} InEvType_t;

typedef struct {
    uint8_t  type;
    int16_t  x;
    int16_t  y;
    uint32_t ts;
} InEv_t;

#define IN_EVQ_SIZE 16
static InEv_t           s_evq[IN_EVQ_SIZE];
static volatile uint8_t s_evq_head = 0;
static volatile uint8_t s_evq_tail = 0;
static volatile uint8_t s_published_pressed = 0U; /* 最近一次进入边沿队列的组合状态 */
static uint8_t s_lv_pressed = 0U;                 /* LVGL 已按顺序消费到的状态 */
static volatile uint8_t s_pointer_queue_peak = 0U;
static volatile uint32_t s_pointer_consumed = 0U;

/* 输入事件统计 (供系统监控应用) */
static volatile uint32_t s_input_events  = 0; /* 输入事件总数 */
static volatile uint32_t s_input_dropped = 0; /* 丢弃的输入事件数 */

/* 入队 (可在 ISR 或任务中调用) */
static void in_evq_push(uint8_t type, int16_t x, int16_t y)
{
    uint8_t n;

    __disable_irq();
    n = (uint8_t)((s_evq_head + 1U) % IN_EVQ_SIZE);
    if (n == s_evq_tail) {          /* 队列满, 丢弃 */
        s_input_dropped++;
        __enable_irq();
        return;
    }
    s_evq[s_evq_head].type = type;
    s_evq[s_evq_head].x    = x;
    s_evq[s_evq_head].y    = y;
    s_evq[s_evq_head].ts   = HAL_GetTick();
    s_evq_head = n;
    s_input_events++;
    {
        uint8_t used = (s_evq_head >= s_evq_tail) ?
                       (uint8_t)(s_evq_head - s_evq_tail) :
                       (uint8_t)(IN_EVQ_SIZE - s_evq_tail + s_evq_head);
        if (used > s_pointer_queue_peak) {
            s_pointer_queue_peak = used;
        }
    }
    __enable_irq();
}

/* 仅在组合按压状态变化时发布边沿，PA2 与触摸重叠时不会产生伪释放。 */
static void input_publish_pointer_state(int16_t x, int16_t y)
{
    uint8_t pressed = (uint8_t)((s_touch_down ||
                       (s_mouse_connected && s_pa2_btn_down)) ? 1U : 0U);

    if (pressed != s_published_pressed) {
        s_published_pressed = pressed;
        in_evq_push(pressed ? IN_EV_PRESS : IN_EV_RELEASE, x, y);
    }
}


/* ========================= 私有函数实现 ========================= */

/**
  * @brief  按键事件入队（供各回调调用）
  * @param  keyId: 按键ID
  * @param  event: 事件类型（Key_Event_t）
  * @retval None
  * @note   运行在TIM5中断上下文中，必须使用 xQueueSendFromISR
  */
static void App_KeyEventPostFromISR(uint8_t keyId, Key_Event_t event)
{
    KeyEventMsg_t msg;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    BaseType_t sent;
    UBaseType_t waiting;

    if (s_key_event_queue == NULL) {
        return;
    }

    msg.keyId = keyId;
    msg.eventType = (uint8_t)event;

    // 发送失败仅发生在队列满时（可适当加大 KEY_EVENT_QUEUE_LENGTH）
    sent = xQueueSendFromISR(s_key_event_queue, &msg, &xHigherPriorityTaskWoken);
    App_InputRecordKeyQueueResult((sent == pdTRUE) ? 1U : 0U);
    waiting = uxQueueMessagesWaitingFromISR(s_key_event_queue);
    if (waiting > s_key_queue_peak) {
        s_key_queue_peak = (uint8_t)waiting;
    }
    // 若唤醒高优先级任务，则请求任务切换
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

/**
  * @brief  按键按下回调
  * @param  keyId: 按键ID
  * @note   运行在TIM5中断上下文中
  */
static void App_KeyPressDownCallback(uint8_t keyId)
{
    App_KeyEventPostFromISR(keyId, KEY_EVENT_PRESS_DOWN);
}

/**
  * @brief  按键单击回调
  * @param  keyId: 按键ID
  * @note   运行在TIM5中断上下文中
  */
static void App_KeyClickCallback(uint8_t keyId)
{
    App_KeyEventPostFromISR(keyId, KEY_EVENT_CLICK);
}

/**
  * @brief  按键双击回调
  * @param  keyId: 按键ID
  * @note   运行在TIM5中断上下文中
  */
static void App_KeyDoubleClickCallback(uint8_t keyId)
{
    App_KeyEventPostFromISR(keyId, KEY_EVENT_DOUBLE_CLICK);
}

/**
  * @brief  按键释放回调
  * @param  keyId: 按键ID
  * @note   运行在TIM5中断上下文中
  */
static void App_KeyReleaseCallback(uint8_t keyId)
{
    App_KeyEventPostFromISR(keyId, KEY_EVENT_RELEASE);
}

/**
  * @brief  按键事件处理（任务上下文，应用层示例）
  * @param  msg: 从队列取出的按键事件消息
  * @retval None
  * @note   按需实现具体业务（如双击返回桌面、单击选中、按下音效反馈等）
  */
static void App_KeyEventHandler(const KeyEventMsg_t *msg)
{
    if (msg == NULL) {
        return;
    }

    // 调试用事件计数（可在 Keil Watch 窗口观察 s_keyEventCounts[0~3]）
    if (msg->eventType < 4U) {
        s_key_event_counts[msg->eventType]++;
    }

    App_InputHandleKeyEvent(msg->keyId, (Key_Event_t)msg->eventType);
}

/**
  * @brief  按键事件消费任务（FreeRTOS）
  * @retval None
  * @note   阻塞接收按键事件队列消息并处理；
  *         任务入口在 freertos.c 的 USER CODE RTOS_THREADS 区创建
  */
void App_KeyEventTask(void)
{
    KeyEventMsg_t msg;

    for (;;) {
        App_HealthBeat(APP_HEALTH_KEY);
        if (xQueueReceive(s_key_event_queue, &msg, pdMS_TO_TICKS(500U)) == pdPASS) {
            App_KeyEventHandler(&msg);
        }
    }
}

/* 出队 */
static int in_evq_pop(InEv_t *out)
{
    int ok = 0;
    __disable_irq();
    if (s_evq_tail != s_evq_head) {
        *out = s_evq[s_evq_tail];
        s_evq_tail = (uint8_t)((s_evq_tail + 1U) % IN_EVQ_SIZE);
        ok = 1;
    }
    __enable_irq();
    return ok;
}

/**
  * @brief  清空残留输入事件 (画图切回桌面时调用)
  */
void App_InputFlush(void)
{
    InEv_t evt;
    while (in_evq_pop(&evt)) { }
    s_touch_down = 0U;
    s_pa2_btn_down = 0U;
    s_click_pending = 0U;
    s_double_click_pending = 0U;
    s_published_pressed = 0U;
    s_lv_pressed = 0U;
    s_pointer_queue_peak = 0U;
    s_pointer_consumed = 0U;
    g_mouse_pressed = 0U;
}

/**
  * @brief  初始化摇杆与输入模块状态
  */
void App_MouseInit(void)
{
    Key_Init();
    JOY_Init(&s_joystick);
    App_InputFlush();

    s_key_event_queue = xQueueCreate(KEY_EVENT_QUEUE_LENGTH, sizeof(KeyEventMsg_t));
    Key_SetPressDownCallback(App_KeyPressDownCallback);
    Key_SetClickCallback(App_KeyClickCallback);
    Key_SetDoubleClickCallback(App_KeyDoubleClickCallback);
    Key_SetReleaseCallback(App_KeyReleaseCallback);
    HAL_TIM_Base_Start_IT(&htim5);
}

void App_InputTick1ms(void)
{
    Key_ScanHandler();
}

/**
  * @brief  鼠标ADC读取任务 (在FreeRTOS任务中调用，不返回)
  * @note   周期性调用 JOY_Update 刷新摇杆数据, 并周期检测鼠标连接状态。
  *         hjoy.x_raw/y_raw/x_norm/y_norm 可在任务中直接使用。
  */
void App_MouseTask(void)
{
    for (;;) {
        App_HealthBeat(APP_HEALTH_MOUSE);
        JOY_Update(&s_joystick);
        /* 持续按压状态直接取已消抖驱动快照；事件队列仍负责点击/双击语义。 */
        {
            uint8_t pressed = Key_IsPressed(KEY_ID_0);
            if (pressed != s_pa2_btn_down) {
                s_pa2_btn_down = pressed;
                input_publish_pointer_state((int16_t)g_mouse_x, (int16_t)g_mouse_y);
            }
        }
        /* 约每 200ms 检测连接状态，降低断开/恢复提示延迟。 */
        if (++s_conn_check_cnt >= 20U) {
            s_conn_check_cnt = 0;
            s_mouse_connected = JOY_IsConnected(&s_joystick);
        }
        osDelay(10);            /* 100Hz采样（configTICK_RATE_HZ=1000） */
    }
}

/**
  * @brief  摇杆/触摸/PA2 -> 鼠标坐标与按下状态
  * @note   - 熄屏(关机)状态: 摇杆移动或 PA2 按下唤醒并回到登录界面
  *         - 摇杆: 按 x_norm/y_norm 增量移动鼠标 (空闲与按下均移动, 按下即拖拽)
  *         - PA2 : 左键按下/释放由 key.c 事件驱动 (App_KeyEventHandler)
  */
void App_MouseUpdate(void)
{
    static float s_mouse_fx = 0.0f;   /* 低速段浮点累加器 (保留小数, 实现细腻移动) */
    static float s_mouse_fy = 0.0f;
    static uint8_t s_last_conn = 0xFF;
    int     new_x = (int)g_mouse_x;
    int     new_y = (int)g_mouse_y;
    int     prev_x = (int)g_mouse_x;
    int     prev_y = (int)g_mouse_y;
    uint8_t touched = 0;
    uint8_t pa2_now;
    uint8_t joy_moved;

    /* ===== 熄屏(关机)状态: 摇杆移动 或 PA2 按键按下 -> 唤醒并回到登录界面 ===== */
    pa2_now = s_pa2_btn_down ? 1U : 0U;
    joy_moved = ((s_joystick.x_norm != 0.0f) || (s_joystick.y_norm != 0.0f)) ? 1U : 0U;
    if (App_PowerHandleWake(pa2_now, s_mouse_connected, joy_moved,
                            g_mouse_x, g_mouse_y)) {
        return;
    }

    /* ===== 摇杆(鼠标)连接状态变化 -> 更新切换按钮显示 ===== */
    if (s_mouse_connected != s_last_conn) {
        s_last_conn = s_mouse_connected;
        input_publish_pointer_state((int16_t)g_mouse_x, (int16_t)g_mouse_y);
        gui_update_mouse_conn(s_mouse_connected);
        App_Log_Event(LOG_LEVEL_WARN, s_mouse_connected ? "输入设备(摇杆)已连接" : "输入设备(摇杆)已断开");
    }
    if (!s_mouse_connected) {
        gui_cursor_hide();                          /* 未连接不显示鼠标图案 */
        return;
    }

    /* 触摸: 点击坐标即鼠标当前坐标 (位置更新由这里负责, 按下事件走队列)。
     * 仅当坐标落在屏幕范围内才作为有效触摸, 避免触摸误报/噪声产生越界坐标
     * 把光标吸附到屏幕边缘并锁死摇杆控制。 */
    if (TP_IsEnabled() && TP_Scan(0) &&
        (tp_dev.x < 320U) && (tp_dev.y < 480U))
    {
        touched = 1;
        s_mouse_fx = 0.0f;      /* 触摸接管时清空摇杆累加器, 避免电量残留跳变 */
        s_mouse_fy = 0.0f;
        new_x = (int)tp_dev.x;
        new_y = (int)tp_dev.y;
    }

    /* 触摸边沿 -> 仅入队事件 (不在轮询里直接操作 UI) */
    if (touched != s_tp_prev)
    {
        s_touch_down = touched;
        input_publish_pointer_state((int16_t)new_x, (int16_t)new_y);
    }
    s_tp_prev = touched;

    /* 摇杆移动: 未触摸时始终移动指针。
     * 若按键处于按下状态, 移动即进入拖拽模式 (拖拽 > 点击)。 */
    if (!touched)
    {
        s_mouse_fx += s_joystick.x_norm * s_mouse_speed;
        s_mouse_fy += s_joystick.y_norm * s_mouse_speed;
        int dx = (int)s_mouse_fx;
        int dy = (int)s_mouse_fy;
        s_mouse_fx -= (float)dx;
        s_mouse_fy -= (float)dy;
        new_x += dx;
        new_y += dy;
    }

    /* 边界保护 (屏幕 320x480) */
    if (new_x < 0) new_x = 0;
    if (new_x > 319) new_x = 319;
    if (new_y < 0) new_y = 0;
    if (new_y > 479) new_y = 479;

    /* 记录用户活动时间 (用于自动熄屏) */
    if (touched || new_x != prev_x || new_y != prev_y) {
        App_PowerRecordActivity();
    }

    g_mouse_x = new_x;
    g_mouse_y = new_y;

    /* 移动小猫光标, 使其尾部 (热点) 落在 (new_x, new_y) */
    gui_cursor_set_pos(new_x, new_y);
}

/**
  * @brief  设置摇杆鼠标移动速度
  * @param  speed: 每帧像素位移系数（px/frame，浮点数）
  * @note   满推速度约为 speed*200 px/s（LVGL 任务 200Hz）；负值按 0 处理
  * @note   仅影响摇杆控制鼠标的移动，不影响触摸输入
  */
void App_SetMouseSpeed(float speed)
{
    if (speed < 0.0f) {
        speed = 0.0f;
    }
    s_mouse_speed = speed;
}

/**
  * @brief  查询 PA2 鼠标左键是否按下 (供画图等原生应用读取)
  */
uint8_t App_MouseBtnDown(void)
{
    return s_pa2_btn_down;
}

/**
  * @brief  查询摇杆(鼠标)是否已连接
  */
uint8_t App_IsMouseConnected(void)
{
    return s_mouse_connected;
}

/**
  * @brief  输入事件统计 (供系统监控应用)
  */
void App_GetInputStats(uint32_t *events, uint32_t *dropped)
{
    if (events  != NULL) *events  = s_input_events;
    if (dropped != NULL) *dropped = s_input_dropped;
}

void App_InputGetQueueStats(uint8_t *current, uint8_t *peak)
{
    UBaseType_t waiting = 0U;

    if (s_key_event_queue != NULL) {
        waiting = uxQueueMessagesWaiting(s_key_event_queue);
    }
    if (current != NULL) {
        *current = (uint8_t)waiting;
    }
    if (peak != NULL) {
        *peak = s_key_queue_peak;
    }
}

/**
  * @brief  记录按键 ISR 向队列投递的结果
  */
void App_InputRecordKeyQueueResult(uint8_t queued)
{
    if (queued) {
        s_input_events++;
    }
    else {
        s_input_dropped++;
    }
}

/**
  * @brief  在按键消费任务中更新鼠标按键状态
  */
void App_InputHandleKeyEvent(uint8_t key_id, Key_Event_t event)
{
    if (key_id != KEY_ID_0) {
        return;
    }

    switch (event) {
        case KEY_EVENT_PRESS_DOWN:
            s_pa2_btn_down = 1U;
            input_publish_pointer_state((int16_t)g_mouse_x, (int16_t)g_mouse_y);
            break;
        case KEY_EVENT_CLICK:
            s_click_pending = 1U;
            break;
        case KEY_EVENT_DOUBLE_CLICK:
            s_double_click_pending = 1U;
            break;
        case KEY_EVENT_RELEASE:
            s_pa2_btn_down = 0U;
            input_publish_pointer_state((int16_t)g_mouse_x, (int16_t)g_mouse_y);
            break;
        default:
            return;
    }
    App_PowerRecordActivity();
}

void App_MouseGetVector(float *x, float *y)
{
    if (x != NULL) {
        *x = s_joystick.x_norm;
    }
    if (y != NULL) {
        *y = s_joystick.y_norm;
    }
}

/**
  * @brief  鼠标右键 (双击) 动作入口
  * @note   双击事件由 key.c 判定后经队列到达, 此处作为"打开上下文菜单"等
  *         右键行为的钩子; 当前 UI 未定义右键菜单, 预留接口。
  */
static void App_MouseRightClick(int x, int y)
{
    (void)x;
    (void)y;
    /* 当前桌面未定义上下文菜单；双击只保留输入统计，不改动 UI。 */
}

/**
  * @brief  消费输入事件, 驱动 LVGL 指针按压状态 (仅在 LVGL 任务上下文调用)
  * @note   - 触摸按下/释放 -> s_touch_down
  *         - PA2 按下/释放(由 App_KeyEventHandler 设置) -> s_pa2_btn_down
  *         - 左键按下状态 = PA2 或 触摸任一按下; 按下期间摇杆移动即拖拽
  *         - CLICK 为按键驱动确认的单击: LVGL 指针在释放时已生成点击,
  *           这里不重复触发, 避免双击
  *         - DOUBLE_CLICK -> 鼠标右键动作
  */
void App_ProcessInputEvents(void)
{
    /* 单击确认: 按下->释放 已在 LVGL 指针上生成一次点击, 此处不重复触发 */
    if (s_click_pending) {
        s_click_pending = 0;
    }

    /* 双击 -> 右键 */
    if (s_double_click_pending) {
        s_double_click_pending = 0;
        App_MouseRightClick(g_mouse_x, g_mouse_y);
    }

    /* 左键按下 = PA2(按键驱动) 或 触摸 任一按下; 鼠标未连接时强制释放 */
    if (s_mouse_connected) {
        g_mouse_pressed = (uint8_t)((s_pa2_btn_down || s_touch_down) ? 1U : 0U);
    }
    else {
        g_mouse_pressed = 0;
    }
}

/**
  * @brief  向 LVGL 顺序提供一个指针边沿或当前稳定状态
  * @note   快速点击产生的 PRESS/RELEASE 不再在同一应用循环内折叠；more=1 时
  *         LVGL 会立即再次调用 read_cb，并逐个执行其指针状态机。
  */
void App_InputReadPointer(int *x, int *y, uint8_t *pressed, uint8_t *more)
{
    InEv_t evt;
    uint8_t has_event = (uint8_t)in_evq_pop(&evt);

    if (has_event) {
        s_pointer_consumed++;
        s_lv_pressed = (evt.type == IN_EV_PRESS) ? 1U : 0U;
        if (x != NULL) *x = evt.x;
        if (y != NULL) *y = evt.y;
    } else {
        if (x != NULL) *x = g_mouse_x;
        if (y != NULL) *y = g_mouse_y;
    }
    if (pressed != NULL) *pressed = s_lv_pressed;
    if (more != NULL) *more = (s_evq_tail != s_evq_head) ? 1U : 0U;
}

void App_InputGetPointerStats(uint8_t *current, uint8_t *peak, uint32_t *consumed)
{
    uint8_t head;
    uint8_t tail;

    __disable_irq();
    head = s_evq_head;
    tail = s_evq_tail;
    if (peak != NULL) *peak = s_pointer_queue_peak;
    if (consumed != NULL) *consumed = s_pointer_consumed;
    __enable_irq();
    if (current != NULL) {
        *current = (head >= tail) ? (uint8_t)(head - tail) :
                   (uint8_t)(IN_EVQ_SIZE - tail + head);
    }
}
