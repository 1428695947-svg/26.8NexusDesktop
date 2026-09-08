/**
  ******************************************************************************
  * @file    app_touch.c
  * @brief   触摸诊断与校准服务
  * @note    封装 XPT2046 诊断、校准界面和校准数据持久化。
  ******************************************************************************
  */

#include "app_touch.h"

#include "cmsis_os.h"
#include "touch.h"
#include "lcd.h"
#include "gui.h"
#include "delay.h"
#include "cal_store.h"
#include "user_store.h"
#include "lvgl.h"
#include "custom.h"
#include "app_draw.h"
#include "app_health.h"

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
            App_HealthBeat(APP_HEALTH_LVGL);
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
                App_HealthBeat(APP_HEALTH_LVGL);
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
                App_HealthBeat(APP_HEALTH_LVGL);
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
