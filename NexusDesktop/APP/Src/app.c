/**
  ******************************************************************************
  * @file    app.c
  * @brief   应用层模块 - 模块初始化装配与按键回调
  * @author  Embedded Expert
  * @version V1.0
  * @date    2026-08-22
  ******************************************************************************
  * @attention
  * 1. 应用层只负责装配：按键/摇杆/定时器各驱动模块互不依赖
  * 2. 按键回调（按下/单击/双击/释放）运行在TIM5中断上下文，
  *    通过 FreeRTOS 队列（xQueueSendFromISR）投递 KeyEventMsg_t 给任务处理
  * 3. 集中在此处初始化，避免CubeMX重新生成main.c时中文注释乱码
  ******************************************************************************
  */

#include "app.h"
#include "cmsis_os.h"
#include "FreeRTOS.h"
#include "queue.h"
#include "touch.h"
#include "lcd.h"
#include "gui.h"
#include "cal_store.h"
#include "delay.h"
#include "flash_store.h"
#include "user_store.h"

#include "lvgl.h"
#include "lv_port_disp.h"
#include "lv_port_indev.h"
#include "gui_guider.h"
#include "custom.h"

/* ========================= 私有全局变量 ========================= */
// 摇杆模块实例（由应用层持有）
Joystick_HandleTypeDef hjoy;

/* ========================= 私有函数声明 ========================= */
static void App_KeyEventPostFromISR(uint8_t keyId, Key_Event_t event);
static void App_KeyPressDownCallback(uint8_t keyId);
static void App_KeyClickCallback(uint8_t keyId);
static void App_KeyDoubleClickCallback(uint8_t keyId);
static void App_KeyReleaseCallback(uint8_t keyId);
static void App_KeyEventHandler(KeyEventMsg_t *msg);

/* ========================= 按键事件队列（应用层示例） =========================
 * key.c 在 TIM5 1ms 中断中产生按键事件并调用回调，本层回调通过 FreeRTOS
 * 队列（FromISR 接口）把 KeyEventMsg_t 投递给 App_KeyEventTask() 任务，
 * 避免在中断上下文中直接处理业务逻辑、防止事件丢失。
 */
#define KEY_EVENT_QUEUE_LENGTH   8       // 队列深度（每条消息为 KeyEventMsg_t）
QueueHandle_t g_keyEventQueue;          // 按键事件队列句柄（App_Init 中创建）

// 调试用：各类事件累计计数（可在 Keil Watch 窗口观察）
static volatile uint32_t s_keyEventCounts[4] = {0, 0, 0, 0};

/* 触摸画图状态: 上一笔的坐标, 用于连续触摸时连线 */
static uint16_t s_draw_prev_x = 0;
static uint16_t s_draw_prev_y = 0;
static uint8_t  s_draw_prev_valid = 0;

/* 鼠标状态: 由 App_MouseUpdate() 维护, 供 LVGL 指针输入设备读取 */
volatile int     g_mouse_x = 160;
volatile int     g_mouse_y = 240;
volatile uint8_t g_mouse_pressed = 0;

/* 摇杆鼠标速度: 每帧像素位移系数 (px/frame)
 * 摇杆驱动已把归一化值做平方 (JOY_ApplyCurve), hjoy.x_norm/y_norm 即二次曲线
 * (-1.0~1.0)。App_MouseUpdate 中每帧位移 = x_norm * s_mouse_speed;
 * LVGL 任务 200Hz, 满推速度约 s_mouse_speed*200 px/s。
 * 默认 3.0f 满推约 600 px/s, 可用 App_SetMouseSpeed() 运行时调整。 */
static float s_mouse_speed = 3.0f;

/* 系统设置关联状态 (自动熄屏) */
static volatile uint16_t s_sleep_sec = SETTINGS_SLEEP_DEFAULT;  /* 自动熄屏时间(秒), 0=从不 */
static volatile uint32_t s_last_activity_ms = 0;                /* 最近一次输入时间 */

/* 画图应用前台标志/打开请求 (供 App_LvglTask 与画图任务切换) */
static volatile uint8_t s_paint_fg = 0;
static volatile uint8_t s_paint_open_req = 0;

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
static InEv_t         s_evq[IN_EVQ_SIZE];
static volatile uint8_t s_evq_head = 0;
static volatile uint8_t s_evq_tail = 0;

/* 入队 (可在 ISR 或任务中调用) */
static void in_evq_push(uint8_t type, int16_t x, int16_t y)
{
    uint8_t n = (uint8_t)((s_evq_head + 1U) % IN_EVQ_SIZE);
    __disable_irq();
    if (n == s_evq_tail) {          /* 队列满, 丢弃 */
        __enable_irq();
        return;
    }
    s_evq[s_evq_head].type = type;
    s_evq[s_evq_head].x    = x;
    s_evq[s_evq_head].y    = y;
    s_evq[s_evq_head].ts   = HAL_GetTick();
    s_evq_head = n;
    __enable_irq();
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

/* 鼠标按键状态 (按键事件任务设置, LVGL 任务消费) */
static volatile uint8_t s_pa2_btn_down      = 0;   /* PA2 左键是否按下 */
static volatile uint8_t s_click_pending     = 0;   /* 单击确认事件待处理 */
static volatile uint8_t s_double_click_pending = 0;/* 双击(右键)事件待处理 */
static uint8_t  s_touch_down = 0;                 /* 触摸是否按下 */
static uint8_t  s_tp_prev    = 0;                 /* 触摸上一周期是否按下 */

/* 熄屏(关机)状态与摇杆(鼠标)连接状态 */
static volatile uint8_t s_power_off       = 0;    /* 1=熄屏中 */
static uint8_t          s_power_off_pa2_prev = 0; /* 进入熄屏时的 PA2 状态 */
static volatile uint8_t s_mouse_connected = 1;    /* 摇杆任务周期检测更新: 1=已连接 */
static uint8_t          s_conn_check_cnt  = 0;    /* 连接检测计数 */

/* ========================= 公共函数实现 ========================= */

/**
  * @brief  应用层初始化（在main中调用）
  * @retval None
  * @note   完成各驱动模块初始化、按键回调注册与TIM5中断启动
  */
void App_Init(void)
{
    /* 驱动模块初始化 */
    Key_Init();
    JOY_Init(&hjoy);

    /* 创建按键事件队列（ISR入队 -> App_KeyEventTask 任务处理） */
    g_keyEventQueue = xQueueCreate(KEY_EVENT_QUEUE_LENGTH, sizeof(KeyEventMsg_t));

    /* 注册按键回调（按下/单击/双击/释放） */
    Key_SetPressDownCallback(App_KeyPressDownCallback);
    Key_SetClickCallback(App_KeyClickCallback);
    Key_SetDoubleClickCallback(App_KeyDoubleClickCallback);
    Key_SetReleaseCallback(App_KeyReleaseCallback);

    /* 启动TIM5 1ms周期中断，驱动按键扫描 */
    HAL_TIM_Base_Start_IT(&htim5);

    /* ==================== ILI9488 LCD + XPT2046 触摸初始化 ====================
     * 说明: 原实现写在 main.c 的 USER CODE 区, CubeMX 重新生成 main.c 时中文
     *       注释容易乱码, 故集中放在应用层 App_Init() 中 (app.c 不会被重新生成)。
     * 1. delay_init(): 初始化 DWT 周期计数器 (delay_us 依赖)
     * 2. LCD_Init()  : SPI1(10.5MHz) + LCD GPIO + ILI9488 初始化序列
     * 3. 显示红色背景 + 字符串, 用于物理观察与 Debug 验证
     * 4. 触摸按需启动; 无存储校准时自动校准一次 (9 点, 结果写 Flash)
     * ======================================================================== */
    delay_init();
    LCD_Init();
    LCD_Clear(RED);
   POINT_COLOR = BLUE;
   BACK_COLOR = BLACK;

   UserStore_Init();               /* 提前载入校准/密码/设置 (供校准判定与开机生效使用) */

   TP_Enable();                /* 按需启动触摸 (首次自动 TP_Init) */
   /* 不再开机阻塞校准: 已校准(含旧版迁移)直接使用; 未校准先从设置页"触摸校准"手动校准,
      期间触摸按未校准的线性映射近似使用, 摇杆鼠标不受影响。 */

    lv_init();
    lv_port_disp_init();
    lv_port_indev_init();

    /* ==================== GUI Guider 界面初始化 ====================
     * 加载登录/桌面界面、密码 Flash 存储、小猫光标。
     * 之后的输入由摇杆/触摸/PA2 控制鼠标, 见 App_MouseUpdate()。
     */
    App_GuiInit();
}

/**
  * @brief  1ms节拍处理（在TIM5中断回调中调用）
  * @retval None
  */
void App_Tick1ms(void)
{
    /* PA2 由 key.c (V2.0) 在 TIM5 中断内完成消抖/单击/双击判定并回调入队,
     * 这里不再直接读取 GPIO, 只驱动按键扫描与 LVGL 时钟。 */
    Key_ScanHandler();
    lv_tick_inc(1);
}

/**
  * @brief  摇杆数据读取任务（在FreeRTOS任务中调用，不返回）
  * @retval None
  * @note   周期性调用 JOY_Update 刷新摇杆数据，
  *         hjoy.x_raw/y_raw/x_norm/y_norm 可在任务中直接使用
  */
void App_JoystickTask(void)
{
    for (;;) {
        JOY_Update(&hjoy);      /* 读取摇杆：原始值->归一化->曲线，结果在 hjoy.x_norm/y_norm */
        /* 约每 500ms 检测一次摇杆(鼠标)连接状态 (ADC 访问集中在摇杆任务, 避免与 LVGL 任务抢) */
        if (++s_conn_check_cnt >= 50U) {
            s_conn_check_cnt = 0;
            s_mouse_connected = JOY_IsConnected(&hjoy);
        }
        osDelay(10);            /* 100Hz采样（configTICK_RATE_HZ=1000） */
    }
}

/**
  * @brief  触摸监控任务（调试用，FreeRTOS）
  * @note   周期调用 TP_UpdateDebug 刷新 dbg_tp_x/y/x_raw/y_raw/pressed，
  *         可在 Keil Debug 模式的 Watch 窗口直接观察触摸 AD 值变化。
  *         任务入口在 freertos.c 中创建（USER CODE RTOS_THREADS 区）。
  */
void App_TouchMonitorTask(void)
{
    uint32_t lastDbgTick = 0;

    for (;;) {
        if (TP_IsEnabled())
        {
            TP_UpdateDebug();       /* 扫描触摸并刷新调试变量 */
            App_TouchDraw();        /* 触摸画图: 按下画蓝点, 拖动连成线 */
            if (HAL_GetTick() - lastDbgTick >= 200U)   /* 每 200ms 刷新一次自检行 */
            {
                lastDbgTick = HAL_GetTick();
                App_TouchShowDbg();
            }
        }
        osDelay(25);            /* 40Hz 采样 (加快跟踪, 减小移动时画点滞后) */
    }
}

/**
  * @brief  触摸硬件自检显示 (屏幕底部一行)
  * @note   格式: P=PD3(IRQ) D=PD2(DOUT) C=PC11(CS) K=PC10(CLK) N=PC12(DIN)
  *         X/Y 为 XPT2046 原始 12 位 AD 值。
  *         触摸时 P 应变 0, X/Y 应变为非 0 的有效值 —— 若不变说明触摸信号
  *         没到 MCU (查接线/触摸 FPC); I 计数应随触摸增加。
  */
void App_TouchShowDbg(void)
{
    uint8_t pen  = (HAL_GPIO_ReadPin(TP_PEN_PORT,  TP_PEN_PIN)  == GPIO_PIN_RESET) ? 1U : 0U;
    uint8_t dout = (HAL_GPIO_ReadPin(TP_DOUT_PORT, TP_DOUT_PIN) == GPIO_PIN_SET)   ? 1U : 0U;
    uint8_t cs   = (HAL_GPIO_ReadPin(TP_CS_PORT,   TP_CS_PIN)   == GPIO_PIN_RESET) ? 1U : 0U;
    uint8_t clk  = (HAL_GPIO_ReadPin(TP_CLK_PORT,  TP_CLK_PIN)  == GPIO_PIN_SET)   ? 1U : 0U;
    uint8_t din  = (HAL_GPIO_ReadPin(TP_DIN_PORT,  TP_DIN_PIN)  == GPIO_PIN_SET)   ? 1U : 0U;

    /* 清底部一行 (黑底), 再画黄色文字 */
    LCD_Fill(0, (uint16_t)(LCD_H - 16), (uint16_t)(LCD_W - 1), (uint16_t)(LCD_H - 1), BLACK);
    POINT_COLOR = YELLOW;
    BACK_COLOR  = BLACK;

    LCD_ShowString(0,   (uint16_t)(LCD_H - 16), 16, "P", 0);
    LCD_ShowNum(12,     (uint16_t)(LCD_H - 16), pen,  1, 16);
    LCD_ShowString(28,  (uint16_t)(LCD_H - 16), 16, "D", 0);
    LCD_ShowNum(40,     (uint16_t)(LCD_H - 16), dout, 1, 16);
    LCD_ShowString(56,  (uint16_t)(LCD_H - 16), 16, "C", 0);
    LCD_ShowNum(68,     (uint16_t)(LCD_H - 16), cs,   1, 16);
    LCD_ShowString(84,  (uint16_t)(LCD_H - 16), 16, "K", 0);
    LCD_ShowNum(96,     (uint16_t)(LCD_H - 16), clk,  1, 16);
    LCD_ShowString(112, (uint16_t)(LCD_H - 16), 16, "N", 0);
    LCD_ShowNum(124,    (uint16_t)(LCD_H - 16), din,  1, 16);
    LCD_ShowString(140, (uint16_t)(LCD_H - 16), 16, "X", 0);
    LCD_ShowNum(152,    (uint16_t)(LCD_H - 16), dbg_tp_x_raw, 4, 16);
    LCD_ShowString(196, (uint16_t)(LCD_H - 16), 16, "Y", 0);
    LCD_ShowNum(208,    (uint16_t)(LCD_H - 16), dbg_tp_y_raw, 4, 16);
}

/**
  * @brief  图形化界面刷新
  * @note   NONE
  */
void App_LvglTask(void)
{
    uint8_t was_draw = 0;       /* 1=刚从画图应用切回, 需要重绘桌面 */

    for(;;)
    {
        /* 画图应用前台: 暂停 LVGL (不刷新不处理输入), 避免覆盖画布/状态互踩 */
        if (s_paint_fg)
        {
            was_draw = 1;
            osDelay(10);        /* 10ms */
            continue;
        }

        /* 画图退出/最小化后切回: 强制 LVGL 重绘当前界面 (画图直接写过 LCD) */
        if (was_draw)
        {
            InEv_t evt;
            was_draw = 0;
            /* 复位鼠标输入状态: 画图里那一下"按下-松开"不能在桌面重复触发 */
            while (in_evq_pop(&evt)) { }      /* 丢弃残留触摸事件 */
            s_touch_down = 0;
            s_pa2_btn_down = 0;
            g_mouse_pressed = 0;
            s_click_pending = 0;
            s_double_click_pending = 0;
            lv_indev_reset(NULL, NULL);       /* 清 LVGL 输入设备内部按下状态 */
            if (lv_scr_act() != NULL)
            {
                lv_obj_invalidate(lv_scr_act());
            }
        }

        App_MouseUpdate();      /* 根据摇杆/触摸/PA2 更新鼠标坐标与按下状态 */
        App_ProcessInputEvents();/* 消费输入事件队列, 驱动 LVGL 点击 (含长按重复) */
        lv_task_handler();

        /* 桌面入口: 请求打开画图应用 -> 置前台, 由画图任务接管屏幕 */
        if (App_ConsumeDrawOpenRequest())
        {
            s_paint_fg = 1;
            continue;           /* 下一轮进入暂停分支 */
        }

        /* 处理设置页发起的"写回 Flash"请求 (滑条松开即保存, 掉电后保持) */
        if (gui_save_requested()) {
            gui_save_settings();
        }
        /* 处理设置页发起的触摸校准请求 (在 LVGL 任务循环里执行, 避免事件回调内长阻塞) */
        if (gui_cal_requested()) {
            gui_cal_request_clear();    /* 先清除, 防止校准(含超时)退出后反复进入 */
            App_TouchCalibrate();
        }
        /* 自动熄屏: 无操作超过设定时间 */
        if (s_sleep_sec > 0 && !s_power_off &&
            (HAL_GetTick() - s_last_activity_ms) >= (uint32_t)s_sleep_sec * 1000UL) {
            App_EnterPowerOff();
        }
        osDelay(5);  /* 200Hz */
    }
}

/**
  * @brief  GUI 界面初始化 (应用层装配)
  * @note   载入掉电保存的密码, 创建登录/桌面界面与小猫光标。
  *         必须在 lv_init/lv_port_disp_init/lv_port_indev_init 之后调用。
  */
void App_GuiInit(void)
{
    FlashStore_Init();          /* 载入(或首次写入默认)密码到 Flash */
    setup_ui(&guider_ui);       /* GUI Guider 生成的界面装配 */
    custom_init(&guider_ui);    /* 自定义回调: 显示密码/登录/小猫光标 */
    gui_cursor_set_pos(g_mouse_x, g_mouse_y);
    App_ApplySettings();        /* 应用开机保存的系统设置 */
}

/**
  * @brief  进入熄屏状态 (关机)
  * @note   关闭背光并隐藏鼠标光标; 摇杆移动或 PA2 按键按下时由
  *         App_MouseUpdate 唤醒并回到登录界面。
  */
void App_EnterPowerOff(void)
{
    /* 若有未保存的系统设置, 先写回 Flash 再熄屏 (掉电保持) */
    if (gui_settings_is_dirty()) {
        gui_save_settings();
    }
    s_power_off = 1;
    s_power_off_pa2_prev = (s_pa2_btn_down ? 1U : 0U);
    LCD_LED_CLR();              /* 关闭背光 = 熄屏 */
    gui_cursor_hide();
}

/**
  * @brief  查询摇杆(鼠标)是否已连接
  * @retval 1=已连接, 0=未连接
  */
uint8_t App_IsMouseConnected(void)
{
    return s_mouse_connected;
}

/**
  * @brief  鼠标输入更新 (由 LVGL 任务周期调用)
  * @note   - 触摸: 按下时把鼠标坐标吸附到触摸点, 并视为鼠标按下
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

    /* ===== 熄屏(关机)状态: 摇杆移动 或 PA2 按键按下 -> 唤醒并回到登录界面 ===== */
    if (s_power_off) {
        pa2_now = (s_pa2_btn_down ? 1U : 0U);
        uint8_t joy_moved = (hjoy.x_norm != 0.0f) || (hjoy.y_norm != 0.0f);
        if (joy_moved || (pa2_now && !s_power_off_pa2_prev)) {
            s_power_off = 0;
            LCD_LED_SET();                          /* 点亮背光 */
            gui_lock_screen();                      /* 返回登录界面并清空密码 */
            if (s_mouse_connected) {
                gui_cursor_set_pos(g_mouse_x, g_mouse_y);  /* 重新显示光标 */
            }
        }
        s_power_off_pa2_prev = pa2_now;
        return;                                     /* 熄屏期间不处理移动/触摸 */
    }

    /* ===== 摇杆(鼠标)连接状态变化 -> 更新切换按钮显示 ===== */
    if (s_mouse_connected != s_last_conn) {
        s_last_conn = s_mouse_connected;
        gui_update_mouse_conn(s_mouse_connected);
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
        if (touched) {
            in_evq_push(IN_EV_PRESS,   (int16_t)new_x, (int16_t)new_y);
        }
        else {
            in_evq_push(IN_EV_RELEASE, (int16_t)new_x, (int16_t)new_y);
        }
    }
    s_tp_prev = touched;

    /* 摇杆移动: 未触摸时始终移动指针。
     * 若按键处于按下状态, 移动即进入拖拽模式 (拖拽 > 点击)。 */
    if (!touched)
    {
        s_mouse_fx += hjoy.x_norm * s_mouse_speed;
        s_mouse_fy += hjoy.y_norm * s_mouse_speed;
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
        s_last_activity_ms = HAL_GetTick();
    }

    g_mouse_x = new_x;
    g_mouse_y = new_y;

    /* 移动小猫光标, 使其尾部 (热点) 落在 (new_x, new_y) */
    gui_cursor_set_pos(new_x, new_y);
}

/**
  * @brief  设置摇杆鼠标移动速度
  * @param  speed: 每帧像素位移系数（px/frame，浮点数）
  * @retval None
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
  * @brief  设置自动熄屏时间
  * @param  sec: 无操作多久后熄屏 (秒), 0=从不自动熄屏
  */
void App_SetSleepSec(uint16_t sec)
{
    s_sleep_sec = sec;
}

/**
  * @brief  查询 PA2 鼠标左键是否按下 (供画图等原生应用读取)
  */
uint8_t App_MouseBtnDown(void)
{
    return s_pa2_btn_down;
}

/**
  * @brief  应用开机/修改后的系统设置
  * @note   灵敏度 -> 摇杆鼠标速度; 光标大小 -> LVGL 缩放; 亮度 -> 顶层遮罩;
  *         熄屏时间 -> 空闲计时阈值。
  */
void App_ApplySettings(void)
{
    const SysSettings_t *st = UserStore_GetSettings();

    if (st == NULL) {
        return;
    }
    App_SetMouseSpeed(0.6f * (float)st->sens);   /* 默认灵敏度5 -> 3.0 */
    App_SetSleepSec(st->sleep_sec);
    gui_cursor_set_zoom(st->cursor_zoom);
    gui_set_brightness(st->brightness);
    s_last_activity_ms = HAL_GetTick();
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
    /* TODO: 如需右键菜单, 可在此查找光标下对象并派发右键/上下文事件 */
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
    InEv_t evt;

    /* 触摸事件 (App_MouseUpdate 轮询入队) */
    while (in_evq_pop(&evt))
    {
        if (evt.type == IN_EV_PRESS) {
            s_touch_down = 1;
        }
        else {
            s_touch_down = 0;
        }
    }

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
  * @brief  触摸画图 (应用层)
  * @note   前置条件: 调用前需先执行 TP_UpdateDebug()/TP_Scan(0) 刷新 tp_dev。
  *         - 触摸按下: 在触摸点画一个蓝色点
  *         - 持续触摸拖动: 与上一点连线 (蓝色)
  *         - 松开后: 重置笔画, 下一次按下重新起笔
  * @note   未校准时 tp_dev.x/y 是 XPT2046 原始 AD 值 (0~4095), 超出屏幕范围,
  *         这里做线性映射到 320x480 并做边界保护; 完成 TP_Adjust() 校准后
  *         xfac/xoff 生效, 自动使用精确屏幕坐标。
  */
void App_TouchDraw(void)
{
    uint16_t x, y;

    if (dbg_tp_pressed == 0)
    {
        s_draw_prev_valid = 0;      /* 松开: 下一笔重新起笔 */
        return;
    }

    /* 每周期重新双读校验 (两次读数偏差 < ERR_RANGE 才算有效):
       失败说明本次读数抖动/不可靠, 直接跳过本周期, 绝不用上一笔的陈旧坐标,
       避免出现"有时准、有时偏"的跳变 */
    if (TP_Read_XY2(&x, &y) == 0)
    {
        return;
    }

    if (tp_dev.xfac == 0.0f)
    {
        /* 未校准: 原始 AD 线性映射到屏幕坐标。
           注意: 本屏 XPT2046 原始 AD 的 X 方向与屏幕 X 相反 (实测左右镜像),
           因此 X 坐标取反后再映射; Y 方向一致, 直接映射。
           线性映射只是近似, 建议调用 App_TouchCalibrate() 校准后更准。 */
        x = (uint16_t)((uint32_t)(LCD_W - 1U) - (((uint32_t)x * LCD_W) / 4096U));
        y = (uint16_t)(((uint32_t)y * LCD_H) / 4096U);
    }
    else
    {
        /* 已校准: 使用校准后的屏幕坐标 */
        x = (uint16_t)(tp_dev.xfac * x + tp_dev.xoff);
        y = (uint16_t)(tp_dev.yfac * y + tp_dev.yoff);
    }

    /* 边界保护, 防止越界写窗口 */
    if (x >= LCD_W) { x = (uint16_t)(LCD_W - 1); }
    if (y >= LCD_H) { y = (uint16_t)(LCD_H - 1); }

    POINT_COLOR = BLUE;

    if (s_draw_prev_valid)
    {
        /* 连续触摸: 与上一点连线 */
        LCD_DrawLine(s_draw_prev_x, s_draw_prev_y, x, y);
    }
    else
    {
        /* 按下处画一个蓝色点 */
        LCD_DrawPoint(x, y);
    }

    s_draw_prev_x = x;
    s_draw_prev_y = y;
    s_draw_prev_valid = 1;
}

/**
  * @brief  触摸多点校准 (应用层包装)
  * @note   实际 9 点校准在驱动层 TP_MultiPointCalibrate() (touch.c) 中实现;
  *         此处仅包一层 LVGL 显示更新开关, 避免校准期间 LVGL 刷新干扰屏幕。
  */
void App_TouchCalibrate(void)
{
    static const uint16_t cal_sx[9] = {30, 160, 290, 30, 160, 290, 30, 160, 290};
    static const uint16_t cal_sy[9] = {90, 90, 90, 240, 240, 240, 390, 390, 390};
    uint16_t rx[9] = {0};
    uint16_t ry[9] = {0};
    uint8_t i;
    uint32_t t0;
    uint32_t t_total;
    uint32_t diag_t = 0;
    uint8_t done = 0;
    uint16_t last_rx = 0;       /* 上一位置原始 AD (变化检测参照) */
    uint16_t last_ry = 0;
    uint8_t saved_bright = gui_get_brightness();
    lv_obj_t *prev_scr = lv_scr_act();
    lv_obj_t *scr = NULL;
    lv_obj_t *cross_h = NULL;
    lv_obj_t *cross_v = NULL;
    lv_obj_t *lbl = NULL;
    char buf[32];

    gui_set_brightness(100);        /* 校准期间全亮, 避免亮度遮罩压暗 */
    TP_Enable();

    /* 建校准界面 (LVGL, 与设置页同一套显示路径, 保证可见) */
    scr = lv_obj_create(NULL);
    lv_obj_set_size(scr, 320, 480);
    lv_obj_set_scrollbar_mode(scr, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0xffffff), LV_PART_MAIN|LV_STATE_DEFAULT);

    lbl = lv_label_create(scr);
    lv_obj_set_style_text_font(lbl, &lv_font_sourcehan18_custom, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0x1a1a2e), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_label_set_text(lbl, "Calibrate: hold cross 1/9");
    lv_obj_set_pos(lbl, 10, 10);

    cross_h = lv_obj_create(scr);
    lv_obj_remove_style_all(cross_h);
    lv_obj_set_size(cross_h, 44, 3);
    lv_obj_set_style_bg_opa(cross_h, LV_OPA_COVER, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(cross_h, lv_color_hex(0xe53935), LV_PART_MAIN|LV_STATE_DEFAULT);

    cross_v = lv_obj_create(scr);
    lv_obj_remove_style_all(cross_v);
    lv_obj_set_size(cross_v, 3, 44);
    lv_obj_set_style_bg_opa(cross_v, LV_OPA_COVER, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(cross_v, lv_color_hex(0xe53935), LV_PART_MAIN|LV_STATE_DEFAULT);

    lv_scr_load(scr);
    t_total = HAL_GetTick();

    /* 采集空闲基线 (请先别碰屏幕) 作为第一点"变化检测"的参照 */
    {
        uint32_t sx = 0, sy = 0;
        uint8_t k;
        uint16_t x, y;
        lv_label_set_text(lbl, "idle...");
        lv_refr_now(NULL);
        for (k = 0; k < 8U; k++)
        {
            TP_Read_XY(&x, &y);
            sx += x;
            sy += y;
            delay_ms(20);
        }
        last_rx = (uint16_t)(sx / 8U);
        last_ry = (uint16_t)(sy / 8U);
    }

    for (i = 0; i < 9U; i++)
    {
        /* 更新十字与序号 */
        lv_obj_set_pos(cross_h, (int16_t)cal_sx[i] - 22, (int16_t)cal_sy[i] - 1);
        lv_obj_set_pos(cross_v, (int16_t)cal_sx[i] - 1, (int16_t)cal_sy[i] - 22);
        lv_snprintf(buf, sizeof(buf), "Calibrate: hold cross %d/9", (int)(i + 1U));
        lv_label_set_text(lbl, buf);
        lv_refr_now(NULL);

        /* 等待手指移动到当前十字: 读数相对上一位置持续偏离 (不依赖 PEN) */
        t0 = HAL_GetTick();
        diag_t = 0;
        {
            uint8_t stable_cnt = 0;
            for (;;)
            {
                uint16_t x, y;
                uint16_t ddx, ddy;
                TP_Read_XY(&x, &y);
                ddx = (x > last_rx) ? (uint16_t)(x - last_rx) : (uint16_t)(last_rx - x);
                ddy = (y > last_ry) ? (uint16_t)(y - last_ry) : (uint16_t)(last_ry - y);
                if ((x >= 60U) && (x <= 4035U) && (y >= 60U) && (y <= 4035U) &&
                    ((ddx > 120U) || (ddy > 120U)))
                {
                    if (++stable_cnt >= 3U) break;   /* 连续 3 次偏离 -> 已按下新位置 */
                }
                else
                {
                    stable_cnt = 0;
                }
                if ((HAL_GetTick() - diag_t) >= 100U)
                {
                    diag_t = HAL_GetTick();
                    lv_snprintf(buf, sizeof(buf), "%d/9  X:%d Y:%d",
                                (int)(i + 1U), (int)x, (int)y);
                    lv_label_set_text(lbl, buf);
                    lv_refr_now(NULL);
                }
                if ((HAL_GetTick() - t0) > 10000U)
                {
                    done = 1;           /* 单点超时放弃 */
                    break;
                }
                delay_ms(5);
            }
        }
        if (done) break;
        delay_ms(40);                   /* 等待坐标稳定 */

        /* 采样: 当前位置固定采 5 次取中值 */
        {
            uint16_t ax[5] = {0}, ay[5] = {0};
            uint8_t valid = 0;
            uint8_t k;

            for (k = 0; k < 5U; k++)
            {
                uint16_t x, y;
                if (TP_Read_XY2(&x, &y))
                {
                    ax[valid] = x;
                    ay[valid] = y;
                    valid++;
                }
                else
                {
                    TP_Read_XY(&x, &y);
                    if ((x >= 60U) && (x <= 4035U) && (y >= 60U) && (y <= 4035U))
                    {
                        ax[valid] = x;
                        ay[valid] = y;
                        valid++;
                    }
                }
                lv_snprintf(buf, sizeof(buf), "%d/9 sampling n:%d",
                            (int)(i + 1U), (int)valid);
                lv_label_set_text(lbl, buf);
                delay_ms(8);
            }
            lv_refr_now(NULL);

            if (valid >= 3U)
            {
                /* 插入排序取中值 */
                uint8_t a, b;
                for (a = 1; a < valid; a++)
                {
                    uint16_t v = ax[a];
                    uint16_t w = ay[a];
                    b = a;
                    while ((b > 0) && (ax[b - 1U] > v))
                    {
                        ax[b] = ax[b - 1U];
                        ay[b] = ay[b - 1U];
                        b--;
                    }
                    ax[b] = v;
                    ay[b] = w;
                }
                rx[i] = ax[valid / 2U];
                ry[i] = ay[valid / 2U];
                last_rx = rx[i];        /* 更新"上一位置", 供下一十字变化检测 */
                last_ry = ry[i];
            }
            else
            {
                if ((HAL_GetTick() - t_total) > 60000U)
                {
                    done = 1;           /* 整体超时放弃 */
                    break;
                }
                i--;                    /* 本点无效: 重试同一十字 */
                continue;
            }
        }

    }

    if (!done)
    {
        TP_CalibrateFromPoints(cal_sx, cal_sy, rx, ry, 9);
        if (lbl != NULL)
        {
            lv_label_set_text(lbl, "Calibrate OK");
            lv_refr_now(NULL);
            delay_ms(600);
        }
    }

    /* 清理: 回到进入校准前的界面 */
    if (scr != NULL)
    {
        if (prev_scr != NULL)
        {
            lv_scr_load(prev_scr);
        }
        lv_obj_del(scr);
    }
    gui_set_brightness(saved_bright);
    if (prev_scr != NULL)
    {
        lv_obj_invalidate(prev_scr);
    }
}

/**
  * @brief  查询是否已有掉电保存的校准数据
  * @retval 1=有 (开机将自动载入), 0=无 (需先校准)
  */
uint8_t App_TouchIsCalibrated(void)
{
    return (CalStore_GetSeq() != 0U) ? 1U : 0U;
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

    if (g_keyEventQueue == NULL) {
        return;
    }

    msg.keyId = keyId;
    msg.eventType = (uint8_t)event;

    // 发送失败仅发生在队列满时（可适当加大 KEY_EVENT_QUEUE_LENGTH）
    xQueueSendFromISR(g_keyEventQueue, &msg, &xHigherPriorityTaskWoken);

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
static void App_KeyEventHandler(KeyEventMsg_t *msg)
{
    if (msg == NULL) {
        return;
    }

    // 调试用事件计数（可在 Keil Watch 窗口观察 s_keyEventCounts[0~3]）
    if (msg->eventType < 4U) {
        s_keyEventCounts[msg->eventType]++;
    }

    switch (msg->eventType) {
        case KEY_EVENT_PRESS_DOWN:      // 0: 按下
            if (msg->keyId == KEY_ID_0) {
                s_pa2_btn_down = 1;     /* 左键按下: 立即生效, 摇杆移动即拖拽 */
                s_last_activity_ms = HAL_GetTick();
            }
            break;

        case KEY_EVENT_CLICK:           // 1: 单击
            if (msg->keyId == KEY_ID_0) {
                s_click_pending = 1;    /* 单击确认: LVGL 任务消费 */
                s_last_activity_ms = HAL_GetTick();
            }
            break;

        case KEY_EVENT_DOUBLE_CLICK:    // 2: 双击
            if (msg->keyId == KEY_ID_0) {
                s_double_click_pending = 1;  /* 双击 -> 右键, LVGL 任务消费 */
                s_last_activity_ms = HAL_GetTick();
            }
            break;

        case KEY_EVENT_RELEASE:         // 3: 释放
            if (msg->keyId == KEY_ID_0) {
                s_pa2_btn_down = 0;     /* 左键释放: 结束拖拽 */
                s_last_activity_ms = HAL_GetTick();
            }
            break;

        default:
            break;
    }
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
        if (xQueueReceive(g_keyEventQueue, &msg, portMAX_DELAY) == pdPASS) {
            App_KeyEventHandler(&msg);
        }
    }
}

/* =========================================================================
 * 画图应用 (原生 LCD 绘制; 独立 FreeRTOS 任务; 摇杆+PA2 鼠标操控)
 * 说明: 全部逻辑在 app.c 内, freertos.c 只调用 App_DrawTask() 一个总函数。
 *       - 输入只读 PA2 + 摇杆(不读触摸), 避免触摸误锁导致"画着画着就点不动";
 *       - 画线仿 App_TouchDraw: 按下期间每帧从上一点连到当前点(连续实线);
 *       - 按钮/色块动作在"松开沿"触发一次, 一次点击只生效一次。
 * ========================================================================= */
#define PAINT_CANVAS_Y   112U
#define PAINT_BTN_Y0     4U
#define PAINT_BTN_H      32U
#define PAINT_BTN_W      78U
#define PAINT_BTN_STEP   80U
#define PAINT_SW_BG_Y    42U
#define PAINT_SW_BG_H    30U
#define PAINT_SW_BG_STEP 38U
#define PAINT_BG_NUM     4U
#define PAINT_SW_PN_Y    78U
#define PAINT_SW_PN_H    24U
#define PAINT_SW_PN_STEP 30U
#define PAINT_PN_NUM     6U
#define PAINT_PANEL_BG   0xEF5B
#define PAINT_RING       0xF81F
#define PAINT_CUR_HALF   4U
#define PAINT_JOY_SPEED  4.0f

static const uint16_t s_paint_bg[PAINT_BG_NUM] = { WHITE, BLACK, YELLOW, CYAN };
static const uint16_t s_paint_pn[PAINT_PN_NUM]  = { BLACK, RED, GREEN, BLUE, YELLOW, MAGENTA };
static const char *const s_paint_btn_txt[4] = { "Clear", "Save", "Min", "Exit" };

static DrawData_t s_paint_work;                   /* 画布工作副本 */
static uint16_t   s_paint_pen = BLACK;            /* 当前笔刷 */
static uint8_t    s_paint_session = 0;            /* 会话有效(最小化保留) */

static uint16_t s_paint_cur_x = 160;
static uint16_t s_paint_cur_y = 240;
static uint16_t s_paint_drawn_x = 160;
static uint16_t s_paint_drawn_y = 240;
static uint8_t  s_paint_cur_on = 0;

/* ---- 基础绘制 ---- */
static void paint_fill(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t c)
{
    if (x2 < x1) { uint16_t t = x1; x1 = x2; x2 = t; }
    if (y2 < y1) { uint16_t t = y1; y1 = y2; y2 = t; }
    LCD_Fill(x1, y1, x2, y2, c);
}

static void paint_btn(uint16_t x, const char *txt)
{
    uint16_t tw = 0;
    const char *p;

    paint_fill(x, PAINT_BTN_Y0, x + PAINT_BTN_W - 1, PAINT_BTN_Y0 + PAINT_BTN_H - 1, WHITE);
    POINT_COLOR = BLACK;
    LCD_DrawRectangle(x, PAINT_BTN_Y0, x + PAINT_BTN_W - 1, PAINT_BTN_Y0 + PAINT_BTN_H - 1);
    for (p = txt; *p; p++) tw += 8U;
    POINT_COLOR = BLACK;
    LCD_ShowString((uint16_t)(x + (PAINT_BTN_W - tw) / 2U), (uint16_t)(PAINT_BTN_Y0 + 8U), 16,
                   (char *)txt, 1);
}

static void paint_sel_ring(uint16_t x, uint16_t y, uint16_t w)
{
    POINT_COLOR = PAINT_RING;
    LCD_DrawRectangle((uint16_t)(x - 2U), (uint16_t)(y - 2U),
                      (uint16_t)(x + w + 1U), (uint16_t)(y + w + 1U));
    LCD_DrawRectangle((uint16_t)(x - 3U), (uint16_t)(y - 3U),
                      (uint16_t)(x + w + 2U), (uint16_t)(y + w + 2U));
}

static void paint_bg_swatch(uint16_t idx)
{
    uint16_t x = (uint16_t)(44U + idx * PAINT_SW_BG_STEP);
    paint_fill(x, PAINT_SW_BG_Y, x + PAINT_SW_BG_H - 1, PAINT_SW_BG_Y + PAINT_SW_BG_H - 1,
               s_paint_bg[idx]);
    POINT_COLOR = BLACK;
    LCD_DrawRectangle(x, PAINT_SW_BG_Y, x + PAINT_SW_BG_H - 1, PAINT_SW_BG_Y + PAINT_SW_BG_H - 1);
    if (s_paint_work.bg_color == s_paint_bg[idx])
    {
        paint_sel_ring(x, PAINT_SW_BG_Y, PAINT_SW_BG_H);
    }
}

static void paint_pn_swatch(uint16_t idx)
{
    uint16_t x = (uint16_t)(40U + idx * PAINT_SW_PN_STEP);
    paint_fill(x, PAINT_SW_PN_Y, x + PAINT_SW_PN_H - 1, PAINT_SW_PN_Y + PAINT_SW_PN_H - 1,
               s_paint_pn[idx]);
    POINT_COLOR = BLACK;
    LCD_DrawRectangle(x, PAINT_SW_PN_Y, x + PAINT_SW_PN_H - 1, PAINT_SW_PN_Y + PAINT_SW_PN_H - 1);
    if (s_paint_pen == s_paint_pn[idx])
    {
        paint_sel_ring(x, PAINT_SW_PN_Y, PAINT_SW_PN_H);
    }
}

/* 面板局部恢复: 只重画与矩形相交的按钮/色块/标签 */
static void paint_panel_restore(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    int i;
    uint8_t bg_lbl = 0;
    uint8_t pn_lbl = 0;

    paint_fill(x0, y0, x1, y1, PAINT_PANEL_BG);
    for (i = 0; i < 4; i++)
    {
        uint16_t bx = (uint16_t)(1 + i * PAINT_BTN_STEP);
        if (x1 >= bx && x0 <= (uint16_t)(bx + PAINT_BTN_W - 1U) &&
            y1 >= PAINT_BTN_Y0 && y0 <= (uint16_t)(PAINT_BTN_Y0 + PAINT_BTN_H - 1U))
        {
            paint_btn(bx, s_paint_btn_txt[i]);
        }
    }
    for (i = 0; i < (int)PAINT_BG_NUM; i++)
    {
        uint16_t sx = (uint16_t)(44 + i * PAINT_SW_BG_STEP);
        if (x1 >= (uint16_t)(sx - 3) && x0 <= (uint16_t)(sx + PAINT_SW_BG_H + 2U) &&
            y1 >= (uint16_t)(PAINT_SW_BG_Y - 3) && y0 <= (uint16_t)(PAINT_SW_BG_Y + PAINT_SW_BG_H + 2U))
        {
            paint_bg_swatch((uint16_t)i);
        }
    }
    for (i = 0; i < (int)PAINT_PN_NUM; i++)
    {
        uint16_t sx = (uint16_t)(40 + i * PAINT_SW_PN_STEP);
        if (x1 >= (uint16_t)(sx - 3) && x0 <= (uint16_t)(sx + PAINT_SW_PN_H + 2U) &&
            y1 >= (uint16_t)(PAINT_SW_PN_Y - 3) && y0 <= (uint16_t)(PAINT_SW_PN_Y + PAINT_SW_PN_H + 2U))
        {
            paint_pn_swatch((uint16_t)i);
        }
    }
    if (x1 >= 4 && x0 <= 40 && y1 >= (uint16_t)(PAINT_SW_BG_Y + 4) &&
        y0 <= (uint16_t)(PAINT_SW_BG_Y + 20)) bg_lbl = 1;
    if (x1 >= 4 && x0 <= 40 && y1 >= (uint16_t)(PAINT_SW_PN_Y + 2) &&
        y0 <= (uint16_t)(PAINT_SW_PN_Y + 18)) pn_lbl = 1;
    BACK_COLOR = PAINT_PANEL_BG;
    POINT_COLOR = BLACK;
    if (bg_lbl) LCD_ShowString(4, PAINT_SW_BG_Y + 6, 16, "BG:", 0);
    if (pn_lbl) LCD_ShowString(4, PAINT_SW_PN_Y + 4, 16, "PN:", 0);
}

static void paint_ui_panel(void)
{
    uint16_t i;

    paint_fill(0, 0, 319, PAINT_CANVAS_Y - 1, PAINT_PANEL_BG);
    for (i = 0; i < 4; i++)
    {
        paint_btn((uint16_t)(1 + i * PAINT_BTN_STEP), s_paint_btn_txt[i]);
    }
    BACK_COLOR = PAINT_PANEL_BG;
    POINT_COLOR = BLACK;
    LCD_ShowString(4, PAINT_SW_BG_Y + 6, 16, "BG:", 0);
    for (i = 0; i < PAINT_BG_NUM; i++) paint_bg_swatch(i);
    POINT_COLOR = BLACK;
    LCD_ShowString(4, PAINT_SW_PN_Y + 4, 16, "PN:", 0);
    for (i = 0; i < PAINT_PN_NUM; i++) paint_pn_swatch(i);
}

static void paint_canvas(void)
{
    uint16_t i;

    paint_fill(0, PAINT_CANVAS_Y, 319, 479, s_paint_work.bg_color);
    for (i = 0; i < s_paint_work.seg_cnt; i++)
    {
        POINT_COLOR = s_paint_work.segs[i].color;
        if (s_paint_work.segs[i].x1 == s_paint_work.segs[i].x2 &&
            s_paint_work.segs[i].y1 == s_paint_work.segs[i].y2)
        {
            LCD_DrawPoint(s_paint_work.segs[i].x1, s_paint_work.segs[i].y1);
        }
        else
        {
            LCD_DrawLine(s_paint_work.segs[i].x1, s_paint_work.segs[i].y1,
                         s_paint_work.segs[i].x2, s_paint_work.segs[i].y2);
        }
    }
}

static void paint_redraw_all(void)
{
    paint_ui_panel();
    paint_canvas();
}

/* ---- 光标 (仅用于非笔画状态瞄准) ---- */
static void paint_restore_rect(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    uint16_t i;
    uint16_t cy0 = (y0 > PAINT_CANVAS_Y) ? y0 : PAINT_CANVAS_Y;

    if (cy0 > y1) return;
    paint_fill(x0, cy0, x1, y1, s_paint_work.bg_color);
    for (i = 0; i < s_paint_work.seg_cnt; i++)
    {
        DrawSeg_t *s = &s_paint_work.segs[i];
        if (s->x1 <= x1 && s->x2 >= x0 && s->y1 <= y1 && s->y2 >= cy0)
        {
            POINT_COLOR = s->color;
            if (s->x1 == s->x2 && s->y1 == s->y2) LCD_DrawPoint(s->x1, s->y1);
            else LCD_DrawLine(s->x1, s->y1, s->x2, s->y2);
        }
    }
}

static void paint_cursor_erase(void)
{
    uint16_t x0, y0, x1, y1;

    if (!s_paint_cur_on) return;
    x0 = (s_paint_drawn_x > PAINT_CUR_HALF + 1U)
         ? (uint16_t)(s_paint_drawn_x - PAINT_CUR_HALF - 1U) : 0U;
    y0 = (s_paint_drawn_y > PAINT_CUR_HALF + 1U)
         ? (uint16_t)(s_paint_drawn_y - PAINT_CUR_HALF - 1U) : 0U;
    x1 = (uint16_t)(s_paint_drawn_x + PAINT_CUR_HALF + 1U);
    if (x1 >= LCD_W) x1 = (uint16_t)(LCD_W - 1U);
    y1 = (uint16_t)(s_paint_drawn_y + PAINT_CUR_HALF + 1U);
    if (y1 >= LCD_H) y1 = (uint16_t)(LCD_H - 1U);

    if (y0 < PAINT_CANVAS_Y)
    {
        uint16_t py1 = (y1 < PAINT_CANVAS_Y) ? y1 : (uint16_t)(PAINT_CANVAS_Y - 1U);
        paint_panel_restore(x0, y0, x1, py1);
        y0 = PAINT_CANVAS_Y;
    }
    if (y0 <= y1) paint_restore_rect(x0, y0, x1, y1);
    s_paint_cur_on = 0;
}

static void paint_cursor_draw(void)
{
    uint16_t x = s_paint_cur_x;
    uint16_t y = s_paint_cur_y;

    if (x < PAINT_CUR_HALF) x = PAINT_CUR_HALF;
    if (x > (uint16_t)(LCD_W - 1U - PAINT_CUR_HALF)) x = (uint16_t)(LCD_W - 1U - PAINT_CUR_HALF);
    if (y < PAINT_CUR_HALF) y = PAINT_CUR_HALF;
    if (y > (uint16_t)(LCD_H - 1U - PAINT_CUR_HALF)) y = (uint16_t)(LCD_H - 1U - PAINT_CUR_HALF);
    s_paint_cur_x = x;
    s_paint_cur_y = y;

    paint_fill((uint16_t)(x - PAINT_CUR_HALF), (uint16_t)(y - PAINT_CUR_HALF),
               (uint16_t)(x + PAINT_CUR_HALF), (uint16_t)(y + PAINT_CUR_HALF), BLACK);
    paint_fill((uint16_t)(x - 1U), (uint16_t)(y - 1U),
               (uint16_t)(x + 1U), (uint16_t)(y + 1U), WHITE);
    s_paint_drawn_x = x;
    s_paint_drawn_y = y;
    s_paint_cur_on = 1;
}

/* ---- 命中测试 / 动作 ---- */
static int paint_hit(int x, int y)
{
    int i;

    if (y >= PAINT_BTN_Y0 && y < (int)(PAINT_BTN_Y0 + PAINT_BTN_H))
    {
        for (i = 0; i < 4; i++)
        {
            int bx = (int)(1 + i * PAINT_BTN_STEP);
            if (x >= bx && x < bx + (int)PAINT_BTN_W) return 1 + i;
        }
    }
    if (y >= PAINT_SW_BG_Y && y < (int)(PAINT_SW_BG_Y + PAINT_SW_BG_H))
    {
        for (i = 0; i < (int)PAINT_BG_NUM; i++)
        {
            int sx = (int)(44 + i * PAINT_SW_BG_STEP);
            if (x >= sx && x < sx + (int)PAINT_SW_BG_H) return 10 + i;
        }
    }
    if (y >= PAINT_SW_PN_Y && y < (int)(PAINT_SW_PN_Y + PAINT_SW_PN_H))
    {
        for (i = 0; i < (int)PAINT_PN_NUM; i++)
        {
            int sx = (int)(40 + i * PAINT_SW_PN_STEP);
            if (x >= sx && x < sx + (int)PAINT_SW_PN_H) return 20 + i;
        }
    }
    if (y >= (int)PAINT_CANVAS_Y) return 100;
    return 0;
}

static void paint_clear(void)
{
    s_paint_work.seg_cnt = 0;
    paint_canvas();
}

static void paint_save(void)
{
    paint_fill(100, PAINT_CANVAS_Y + 4, 220, PAINT_CANVAS_Y + 24, WHITE);
    POINT_COLOR = BLACK;
    LCD_DrawRectangle(100, PAINT_CANVAS_Y + 4, 220, PAINT_CANVAS_Y + 24);
    LCD_ShowString(124, PAINT_CANVAS_Y + 8, 16, "SAVED", 1);
    UserStore_SaveDrawing(&s_paint_work);
    delay_ms(500);
    paint_canvas();
}

static void paint_set_bg(uint8_t idx)
{
    if (idx >= PAINT_BG_NUM) return;
    s_paint_work.bg_color = s_paint_bg[idx];
    paint_canvas();
    paint_ui_panel();
}

/* 摇杆移动光标 (画图输入只用摇杆+PA2, 不读触摸, 防止触摸误锁) */
static void paint_joy_move(void)
{
    if (hjoy.x_norm != 0.0f || hjoy.y_norm != 0.0f)
    {
        int dx = (int)(hjoy.x_norm * PAINT_JOY_SPEED);
        int dy = (int)(hjoy.y_norm * PAINT_JOY_SPEED);
        if (dx != 0)
        {
            int nx = (int)s_paint_cur_x + dx;
            s_paint_cur_x = (nx < 0) ? 0U
                           : (uint16_t)((nx > (int)(LCD_W - 1U)) ? (int)(LCD_W - 1U) : nx);
        }
        if (dy != 0)
        {
            int ny = (int)s_paint_cur_y + dy;
            s_paint_cur_y = (ny < 0) ? 0U
                           : (uint16_t)((ny > (int)(LCD_H - 1U)) ? (int)(LCD_H - 1U) : ny);
        }
    }
}

/* 前台运行: 按下画布=起笔并连续画线; 松开画布=抬笔; 按钮/色块=松开沿单击 */
static void paint_run(void)
{
    uint16_t pen_x = 0, pen_y = 0;      /* 笔画上一屏幕点 */
    uint16_t pers_x = 0, pers_y = 0;    /* 持久化节流锚点 */
    uint8_t stroking = 0;
    uint8_t btn_prev = 0;
    uint8_t quit = 0;
    uint32_t stroke_last = 0;
    uint32_t t0;

    paint_redraw_all();

    s_paint_cur_x = (g_mouse_x > 0) ? (uint16_t)g_mouse_x : 0U;
    s_paint_cur_y = (g_mouse_y > 0) ? (uint16_t)g_mouse_y : 0U;
    if (s_paint_cur_x >= LCD_W) s_paint_cur_x = (uint16_t)(LCD_W - 1U);
    if (s_paint_cur_y >= LCD_H) s_paint_cur_y = (uint16_t)(LCD_H - 1U);
    s_paint_cur_on = 0;
    s_paint_drawn_x = s_paint_cur_x;
    s_paint_drawn_y = s_paint_cur_y;

    /* 等待打开应用的那一下按键松开 */
    t0 = HAL_GetTick();
    while (App_MouseBtnDown() && ((HAL_GetTick() - t0) < 500U)) osDelay(5);
    btn_prev = App_MouseBtnDown();

    while (!quit)
    {
        uint8_t down;
        uint8_t moved;
        uint8_t action_redraw = 0;

        paint_joy_move();
        down = App_MouseBtnDown() ? 1U : 0U;
        moved = (s_paint_cur_x != s_paint_drawn_x) || (s_paint_cur_y != s_paint_drawn_y);

        if (stroking)
        {
            /* 笔画中: 光标隐藏, 每帧从上一点连到当前点 (连续实线) */
            if (moved)
            {
                uint16_t x = s_paint_cur_x, y = s_paint_cur_y;
                if (y < PAINT_CANVAS_Y) y = PAINT_CANVAS_Y;
                if (x >= LCD_W) x = (uint16_t)(LCD_W - 1U);
                POINT_COLOR = s_paint_pen;
                LCD_DrawLine(pen_x, pen_y, x, y);
                pen_x = x;
                pen_y = y;
                stroke_last = HAL_GetTick();
                {
                    uint16_t ddx = (x > pers_x) ? (x - pers_x) : (pers_x - x);
                    uint16_t ddy = (y > pers_y) ? (y - pers_y) : (pers_y - y);
                    if ((ddx >= 3U || ddy >= 3U) && s_paint_work.seg_cnt < DRAW_SEG_MAX)
                    {
                        DrawSeg_t *sg = &s_paint_work.segs[s_paint_work.seg_cnt];
                        sg->color = s_paint_pen;
                        sg->x1 = pers_x; sg->y1 = pers_y;
                        sg->x2 = x;     sg->y2 = y;
                        s_paint_work.seg_cnt++;
                        pers_x = x;
                        pers_y = y;
                    }
                }
            }
            if (!down || ((HAL_GetTick() - stroke_last) > 3000U))
            {
                stroking = 0;       /* 抬笔或看门狗超时: 结束笔画, 恢复光标 */
            }
        }
        else
        {
            if (moved) paint_cursor_erase();

            if (down && !btn_prev)          /* 按下沿 */
            {
                if (paint_hit((int)s_paint_cur_x, (int)s_paint_cur_y) == 100)
                {
                    uint16_t x = s_paint_cur_x, y = s_paint_cur_y;
                    if (y < PAINT_CANVAS_Y) y = PAINT_CANVAS_Y;
                    if (x >= LCD_W) x = (uint16_t)(LCD_W - 1U);
                    paint_cursor_erase();   /* 隐藏方块光标, 进入连续画线 */
                    if (s_paint_work.seg_cnt < DRAW_SEG_MAX)
                    {
                        DrawSeg_t *sg = &s_paint_work.segs[s_paint_work.seg_cnt];
                        sg->color = s_paint_pen;
                        sg->x1 = x; sg->y1 = y;
                        sg->x2 = x; sg->y2 = y;
                        s_paint_work.seg_cnt++;
                    }
                    POINT_COLOR = s_paint_pen;
                    LCD_DrawPoint(x, y);
                    pen_x = x; pen_y = y;
                    pers_x = x; pers_y = y;
                    stroking = 1;
                    stroke_last = HAL_GetTick();
                }
            }
            else if (!down && btn_prev)     /* 松开沿: 一次单击一次动作 */
            {
                int act = paint_hit((int)s_paint_cur_x, (int)s_paint_cur_y);
                if (act >= 1 && act <= 4)
                {
                    if (act == 1)      { paint_clear(); action_redraw = 1; }
                    else if (act == 2) { paint_save(); action_redraw = 1; }
                    else if (act == 3) { s_paint_session = 1; quit = 1; }   /* 最小化 */
                    else               { s_paint_session = 0; quit = 1; }   /* 退出 */
                }
                else if (act >= 10 && act < 10 + (int)PAINT_BG_NUM)
                {
                    paint_set_bg((uint8_t)(act - 10));
                    action_redraw = 1;
                }
                else if (act >= 20 && act < 20 + (int)PAINT_PN_NUM)
                {
                    s_paint_pen = s_paint_pn[act - 20];
                    paint_ui_panel();
                    action_redraw = 1;
                }
            }
        }

        g_mouse_x = (int)s_paint_cur_x;     /* 同步桌面鼠标位置 */
        g_mouse_y = (int)s_paint_cur_y;

        if (!quit && !stroking && (moved || action_redraw || !s_paint_cur_on))
        {
            paint_cursor_draw();
        }
        btn_prev = down;
        osDelay(5);
    }

    /* 退出/最小化: 等按键松开再交还, 防桌面重复触发 */
    t0 = HAL_GetTick();
    while (App_MouseBtnDown() && ((HAL_GetTick() - t0) < 1000U)) osDelay(5);
    paint_cursor_erase();
    g_mouse_x = (int)s_paint_cur_x;
    g_mouse_y = (int)s_paint_cur_y;
}

/**
  * @brief  画图应用 FreeRTOS 任务 (freertos.c 直接调用本函数)
  */
void App_DrawTask(void *argument)
{
    (void)argument;
    for (;;)
    {
        while (!s_paint_fg) osDelay(10);

        /* 新会话(开机/退出后)载入已保存绘图; 最小化恢复沿用 RAM 画布 */
        if (!s_paint_session)
        {
            s_paint_work = *UserStore_GetDrawing();
            if (s_paint_work.seg_cnt > DRAW_SEG_MAX) s_paint_work.seg_cnt = DRAW_SEG_MAX;
            s_paint_session = 1;
        }
        paint_run();
        s_paint_fg = 0;
        osDelay(20);
    }
}

/**
  * @brief  桌面"画图"按钮请求打开应用 (由 custom.c 事件调用)
  */
void App_RequestDrawOpen(void)
{
    s_paint_open_req = 1;
}

/**
  * @brief  消费打开请求 (LVGL 任务轮询)
  */
uint8_t App_ConsumeDrawOpenRequest(void)
{
    if (s_paint_open_req)
    {
        s_paint_open_req = 0;
        return 1;
    }
    return 0;
}
