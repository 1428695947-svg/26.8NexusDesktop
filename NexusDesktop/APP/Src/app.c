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
  * 2. 按键回调运行在TIM5中断上下文，通知任务需使用FromISR接口
  * 3. 集中在此处初始化，避免CubeMX重新生成main.c时中文注释乱码
  ******************************************************************************
  */

#include "app.h"
#include "cmsis_os.h"
#include "touch.h"
#include "lcd.h"
#include "gui.h"
#include "cal_store.h"
#include "delay.h"

/* ========================= 私有全局变量 ========================= */
// 摇杆模块实例（由应用层持有）
Joystick_HandleTypeDef hjoy;

/* ========================= 私有函数声明 ========================= */
static void App_JoystickKeyCallback(uint8_t keyId);
static void App_KeyLongPressCallback(uint8_t keyId);
static void App_KeyLongPressHoldCallback(uint8_t keyId);
static void App_KeyReleaseCallback(uint8_t keyId);

/* 触摸画图状态: 上一笔的坐标, 用于连续触摸时连线 */
static uint16_t s_draw_prev_x = 0;
static uint16_t s_draw_prev_y = 0;
static uint8_t  s_draw_prev_valid = 0;

/* 触摸功能使能标志: 默认关闭, 调用 App_TouchEnable() 后才启动扫描/画图 */
static volatile uint8_t s_touch_enabled = 0;
static volatile uint8_t s_touch_inited = 0;

/* 校准请求标志: PA2 按键 (key.c 中按键槽位 0) 短按置位, 触摸任务中执行 9 点校准 */
static volatile uint8_t s_cal_request = 0;

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

    /* 注册按键回调（短按/长按/长按保持/释放） */
    Key_SetShortPressCallback(App_JoystickKeyCallback);
    Key_SetLongPressCallback(App_KeyLongPressCallback);
    Key_SetLongPressHoldCallback(App_KeyLongPressHoldCallback);
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
    POINT_COLOR = WHITE;
    BACK_COLOR = RED;
    LCD_ShowString(20, 20, 16, "ILI9488 HAL OK", 0);
    LCD_ShowString(20, 40, 16, "F407VET6 SPI1 10.5M", 0);
    LCD_ShowString(20, 60, 16, "Touch: Touch The Screen", 0);

    App_TouchEnable();          /* 按需启动触摸 (首次自动 TP_Init) */
    if (!App_TouchIsCalibrated())
    {
        App_TouchCalibrate();   /* 无存储校准时才校准 (9 点), 结果写 Flash */
    }
}

/**
  * @brief  1ms节拍处理（在TIM5中断回调中调用）
  * @retval None
  */
void App_Tick1ms(void)
{
    Key_ScanHandler();
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
        if (s_cal_request)          /* PA2 按键短按: 需要重新校准 */
        {
            s_cal_request = 0;
            App_TouchCalibrate();   /* 阻塞到 9 点校准完成, 结果存 Flash */
        }
        if (s_touch_enabled)
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
  * @brief  按需开启触摸功能
  * @note   首次调用会执行 TP_Init() (触摸 GPIO + EXTI3 中断配置), 之后
  *         TouchMonitorTask 才开始扫描触摸、画图与刷新自检行。
  *         必须在 delay_init() 之后调用 (TP_Init 内部使用 delay_us);
  *         可在 main 调度器启动前调用, 也可在任意 FreeRTOS 任务中调用。
  */
void App_TouchEnable(void)
{
    if (s_touch_inited == 0)
    {
        TP_Init();
        s_touch_inited = 1;
    }
    s_touch_enabled = 1;
}

/**
  * @brief  关闭触摸功能 (停止扫描/画图/自检显示)
  */
void App_TouchDisable(void)
{
    s_touch_enabled = 0;
}

/**
  * @brief  查询触摸功能是否已开启
  * @retval 1=已开启, 0=未开启
  */
uint8_t App_TouchIsEnabled(void)
{
    return (uint8_t)s_touch_enabled;
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

/* ==================== 多点校准参数 ==================== */
#define CAL_POINTS_NUM   9U      /* 校准点数: 3x3 网格 (点越多越准) */
#define CAL_MARGIN       30U     /* 边缘留白 (像素) */
#define CAL_SAMPLE_CNT   5U      /* 每点采样次数 */
#define CAL_MIN_VALID    3U      /* 每点最少有效采样数 */

/* 校准点: 屏幕坐标 + 对应原始 AD */
typedef struct
{
    uint16_t sx, sy;
    uint16_t rx, ry;
} CalSample_t;

/**
  * @brief  简单插入排序 (升序), 供取中值
  */
static void cal_sort_u16(uint16_t *buf, uint8_t n)
{
    uint8_t i, j;
    for (i = 1; i < n; i++)
    {
        uint16_t v = buf[i];
        j = i;
        while ((j > 0) && (buf[j - 1U] > v))
        {
            buf[j] = buf[j - 1U];
            j--;
        }
        buf[j] = v;
    }
}

/**
  * @brief  在屏幕坐标 (sx,sy) 画一个十字 (用 POINT_COLOR)
  */
static void cal_draw_cross(uint16_t sx, uint16_t sy, uint16_t color)
{
    POINT_COLOR = color;
    LCD_DrawLine((uint16_t)(sx - 14U), sy, (uint16_t)(sx + 15U), sy);
    LCD_DrawLine(sx, (uint16_t)(sy - 14U), sx, (uint16_t)(sy + 15U));
}

/**
  * @brief  等待按下并采样一个校准点 (多次读数取中值)
  * @retval 1=成功, 0=有效采样不足
  */
static uint8_t cal_sample_point(uint16_t *rx_out, uint16_t *ry_out)
{
    uint16_t rx[CAL_SAMPLE_CNT];
    uint16_t ry[CAL_SAMPLE_CNT];
    uint8_t valid = 0;
    uint8_t i;

    while (PEN_READ() != GPIO_PIN_RESET)    /* 等待按下 */
    {
        delay_ms(5);
    }
    delay_ms(40);                           /* 等待坐标稳定 */

    for (i = 0; i < CAL_SAMPLE_CNT; i++)
    {
        uint16_t x, y;
        if (TP_Read_XY2(&x, &y))
        {
            rx[valid] = x;
            ry[valid] = y;
            valid++;
        }
        delay_ms(8);
    }

    while (PEN_READ() == GPIO_PIN_RESET)    /* 等待松开 */
    {
        delay_ms(5);
    }

    if (valid < CAL_MIN_VALID)
    {
        return 0;
    }
    cal_sort_u16(rx, valid);
    cal_sort_u16(ry, valid);
    *rx_out = rx[valid / 2U];
    *ry_out = ry[valid / 2U];
    return 1;
}

/**
  * @brief  触摸多点校准 (按需调用, 阻塞到全部点完)
  * @note   屏幕依次出现 3x3 共 9 个十字, 用笔/手指逐个点击; 完成后用最小二乘
  *         拟合 xfac/yfac/xoff/yoff, 并写入片内 Flash (只保留最近一条), 掉电不丢。
  *         之后开机自动载入, 不再校准; 发现偏移时再次调用本函数即可更新。
  *         须在 delay_init() 之后调用; 可在 main 调度器前或任务中调用。
  */
void App_TouchCalibrate(void)
{
    CalSample_t pts[CAL_POINTS_NUM];
    uint16_t sx[CAL_POINTS_NUM];
    uint16_t sy[CAL_POINTS_NUM];
    uint8_t i;
    uint32_t n = 0;
    uint32_t sum_rx = 0, sum_ry = 0, sum_sx = 0, sum_sy = 0;
    uint32_t sum_rxrx = 0, sum_rxsx = 0, sum_ryry = 0, sum_rysy = 0;
    float nf, den;

    if (s_touch_inited == 0)
    {
        TP_Init();
        s_touch_inited = 1;
    }

    /* 3x3 网格校准点 */
    for (i = 0; i < CAL_POINTS_NUM; i++)
    {
        uint8_t col = (uint8_t)(i % 3U);
        uint8_t row = (uint8_t)(i / 3U);
        sx[i] = (col == 0U) ? CAL_MARGIN
              : (col == 1U) ? (uint16_t)(LCD_W / 2U)
              : (uint16_t)(LCD_W - CAL_MARGIN);
        sy[i] = (row == 0U) ? CAL_MARGIN
              : (row == 1U) ? (uint16_t)(LCD_H / 2U)
              : (uint16_t)(LCD_H - CAL_MARGIN);
    }

    /* 提示 */
    LCD_Clear(WHITE);
    POINT_COLOR = BLACK;
    BACK_COLOR = WHITE;
    LCD_ShowString(10, 10, 16, "Calibrate: tap each cross", 0);

    for (i = 0; i < CAL_POINTS_NUM; i++)
    {
        uint16_t rx = 0, ry = 0;
        uint8_t ok = 0;

        cal_draw_cross(sx[i], sy[i], RED);          /* 画当前点 (红色) */
        LCD_ShowNum(10, 30, (uint32_t)(i + 1U), 1, 16);
        LCD_ShowString(22, 30, 16, "/9", 0);

        while (!ok)                                  /* 采样直到有效 */
        {
            ok = cal_sample_point(&rx, &ry);
            if (!ok)
            {
                cal_draw_cross(sx[i], sy[i], WHITE);
                delay_ms(300);
                cal_draw_cross(sx[i], sy[i], RED);
            }
        }

        cal_draw_cross(sx[i], sy[i], WHITE);         /* 抹掉十字 */
        pts[n].sx = sx[i];
        pts[n].sy = sy[i];
        pts[n].rx = rx;
        pts[n].ry = ry;
        n++;
    }

    /* 最小二乘线性拟合: sx = xfac*rx + xoff, sy = yfac*ry + yoff */
    for (i = 0; i < n; i++)
    {
        sum_rx   += pts[i].rx;
        sum_ry   += pts[i].ry;
        sum_sx   += pts[i].sx;
        sum_sy   += pts[i].sy;
        sum_rxrx += (uint32_t)pts[i].rx * pts[i].rx;
        sum_rxsx += (uint32_t)pts[i].rx * pts[i].sx;
        sum_ryry += (uint32_t)pts[i].ry * pts[i].ry;
        sum_rysy += (uint32_t)pts[i].ry * pts[i].sy;
    }
    nf = (float)n;

    den = nf * (float)sum_rxrx - (float)sum_rx * (float)sum_rx;
    if (den != 0.0f)
    {
        tp_dev.xfac = (nf * (float)sum_rxsx - (float)sum_rx * (float)sum_sx) / den;
        tp_dev.xoff = (int16_t)(((float)sum_sx - tp_dev.xfac * (float)sum_rx) / nf);
    }
    den = nf * (float)sum_ryry - (float)sum_ry * (float)sum_ry;
    if (den != 0.0f)
    {
        tp_dev.yfac = (nf * (float)sum_rysy - (float)sum_ry * (float)sum_sy) / den;
        tp_dev.yoff = (int16_t)(((float)sum_sy - tp_dev.yfac * (float)sum_ry) / nf);
    }
    tp_dev.touchtype = 0;

    /* 写入 Flash (只保留最近一条), 更新序号 */
    TP_Save_Adjdata();
    g_cal_seq = CalStore_GetSeq();

    /* 完成提示, 恢复红色画布 */
    LCD_Clear(RED);
    POINT_COLOR = WHITE;
    BACK_COLOR = RED;
    LCD_ShowString(20, 20, 16, "Calibrated OK, Touch To Draw", 0);
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
  * @brief  按键短按回调
  * @param  keyId: 按键ID
  * @note   运行在TIM5中断上下文中
  */
static void App_JoystickKeyCallback(uint8_t keyId)
{
    /* PA2 按键 (key.c 中按键槽位 0, 对应 KEY_ID_0) 短按: 请求重新校准触摸 */
    if (keyId == KEY_ID_0) {
        s_cal_request = 1;
    }
}

/**
  * @brief  按键长按回调（预留）
  * @param  keyId: 按键ID
  */
static void App_KeyLongPressCallback(uint8_t keyId)
{
    (void)keyId;
    // TODO: 长按功能，按需实现
}

/**
  * @brief  按键长按保持回调（预留）
  * @param  keyId: 按键ID
  */
static void App_KeyLongPressHoldCallback(uint8_t keyId)
{
    if (keyId == KEY_ID_2) {        // 摇杆Z轴按键：长按保持功能待接入
        // TODO: 按需实现
    }
}

/**
  * @brief  按键释放回调（预留）
  * @param  keyId: 按键ID
  */
static void App_KeyReleaseCallback(uint8_t keyId)
{
    (void)keyId;
    // TODO: 释放功能，按需实现
}
