/**
  ******************************************************************************
  * @file    app_power.c
  * @brief   应用电源状态服务 - 熄屏、空闲计时与输入唤醒
  * @note    本模块只管理应用可见的 ACTIVE/OFF 状态；当前 OFF 为关闭 LCD
  *          背光，不进入 MCU STOP 模式。唤醒后统一返回登录界面。
  ******************************************************************************
  */

#include "app_power.h"

#include "main.h"
#include "lcd.h"
#include "gui.h"
#include "custom.h"
#include "app_log.h"
#include "user_store.h"

/* ========================= 私有状态 ========================= */
static volatile uint8_t  s_power_off = 0U;
static uint8_t           s_power_off_button_prev = 0U;
static volatile uint16_t s_sleep_sec = SETTINGS_SLEEP_DEFAULT;
static volatile uint32_t s_last_activity_ms = 0U;

void App_PowerInit(void)
{
    s_power_off = 0U;
    s_power_off_button_prev = 0U;
    s_last_activity_ms = HAL_GetTick();
}

void App_SetSleepSec(uint16_t sec)
{
    s_sleep_sec = sec;
}

void App_PowerRecordActivity(void)
{
    s_last_activity_ms = HAL_GetTick();
}

void App_PowerPoll(void)
{
    if ((s_sleep_sec > 0U) && !s_power_off &&
        ((HAL_GetTick() - s_last_activity_ms) >= ((uint32_t)s_sleep_sec * 1000UL))) {
        App_EnterPowerOff();
    }
}

void App_EnterPowerOff(void)
{
    if (s_power_off) {
        return;
    }

    /* 掉电保存请求仍由设置模块提供；后续由存储服务进一步异步化。 */
    if (gui_settings_is_dirty()) {
        gui_save_settings();
    }

    s_power_off = 1U;
    s_power_off_button_prev = 0U;
    LCD_LED_CLR();
    gui_cursor_hide();
    App_Log_Event(LOG_LEVEL_INFO, "屏幕熄灭 (自动或手动)");
}

uint8_t App_PowerHandleWake(uint8_t button_down,
                            uint8_t pointer_connected,
                            uint8_t pointer_moved,
                            int pointer_x,
                            int pointer_y)
{
    if (!s_power_off) {
        return 0U;
    }

    if (pointer_moved || (button_down && !s_power_off_button_prev)) {
        s_power_off = 0U;
        s_last_activity_ms = HAL_GetTick();
        LCD_LED_SET();
        App_Log_Event(LOG_LEVEL_INFO, "屏幕唤醒");
        gui_lock_screen();
        if (pointer_connected) {
            gui_cursor_set_pos(pointer_x, pointer_y);
        }
    }

    s_power_off_button_prev = button_down;
    return 1U;
}
