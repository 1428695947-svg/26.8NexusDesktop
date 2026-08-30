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

/* 摇杆鼠标速度映射:
 * 摇杆驱动已把归一化值做平方 (JOY_ApplyCurve), hjoy.x_norm/y_norm 即二次曲线
 * (-1.0~1.0)。乘以 MOUSE_STEP 得到每帧像素位移, 即"摇杆偏移量 -> 鼠标位移
 * (-50 ~ +50 pixels/frame)"。满推约 MOUSE_STEP*200 px/s。 */
#define MOUSE_STEP 50.0f

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

   TP_Enable();                /* 按需启动触摸 (首次自动 TP_Init) */
   if (!App_TouchIsCalibrated())
   {
       App_TouchCalibrate();   /* 无存储校准时才校准 (9 点), 结果写 Flash */
   }

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
    for(;;)
    {
        App_MouseUpdate();      /* 根据摇杆/触摸/PA2 更新鼠标坐标与按下状态 */
        App_ProcessInputEvents();/* 消费输入事件队列, 驱动 LVGL 点击 (含长按重复) */
        lv_task_handler();
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
    int     new_x = (int)g_mouse_x;
    int     new_y = (int)g_mouse_y;
    uint8_t touched = 0;

    /* 触摸: 点击坐标即鼠标当前坐标 (位置更新由这里负责, 按下事件走队列) */
    if (TP_IsEnabled() && TP_Scan(0))
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
        s_mouse_fx += hjoy.x_norm * MOUSE_STEP;
        s_mouse_fy += hjoy.y_norm * MOUSE_STEP;
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

    g_mouse_x = new_x;
    g_mouse_y = new_y;

    /* 移动小猫光标, 使其尾部 (热点) 落在 (new_x, new_y) */
    gui_cursor_set_pos(new_x, new_y);
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

    /* 左键按下 = PA2(按键驱动) 或 触摸 任一按下 */
    g_mouse_pressed = (uint8_t)((s_pa2_btn_down || s_touch_down) ? 1U : 0U);
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
    disp_disable_update();
    TP_MultiPointCalibrate();
    disp_enable_update();
    /* 校准直接操作了 LCD, 强制 LVGL 整屏重绘, 恢复校准前的界面 */
    if (lv_scr_act() != NULL)
    {
        lv_obj_invalidate(lv_scr_act());
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
            }
            break;

        case KEY_EVENT_CLICK:           // 1: 单击
            if (msg->keyId == KEY_ID_0) {
                s_click_pending = 1;    /* 单击确认: LVGL 任务消费 */
            }
            break;

        case KEY_EVENT_DOUBLE_CLICK:    // 2: 双击
            if (msg->keyId == KEY_ID_0) {
                s_double_click_pending = 1;  /* 双击 -> 右键, LVGL 任务消费 */
            }
            break;

        case KEY_EVENT_RELEASE:         // 3: 释放
            if (msg->keyId == KEY_ID_0) {
                s_pa2_btn_down = 0;     /* 左键释放: 结束拖拽 */
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
