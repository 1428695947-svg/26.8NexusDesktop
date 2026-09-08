/**
  ******************************************************************************
  * @file    app_desktop.c
  * @brief   桌面状态信息 - 显示开机运行时长
  ******************************************************************************
  */

#include "app_desktop.h"

#include "cmsis_os.h"

static lv_obj_t *s_uptime_label = NULL;
static lv_timer_t *s_uptime_timer = NULL;

static void desktop_uptime_refresh(lv_timer_t *timer)
{
    uint32_t total_sec;
    uint32_t hour;
    uint32_t minute;
    uint32_t second;
    char text[24];

    (void)timer;
    if (s_uptime_label == NULL) {
        return;
    }

    total_sec = (uint32_t)osKernelGetTickCount() / 1000U;
    hour = total_sec / 3600U;
    minute = (total_sec / 60U) % 60U;
    second = total_sec % 60U;
    lv_snprintf(text, sizeof(text), "UP %03lu:%02lu:%02lu",
                (unsigned long)hour,
                (unsigned long)minute,
                (unsigned long)second);
    lv_label_set_text(s_uptime_label, text);
}

void App_DesktopAttachUptimeLabel(lv_obj_t *label)
{
    s_uptime_label = label;
    desktop_uptime_refresh(NULL);
    if (s_uptime_timer == NULL) {
        s_uptime_timer = lv_timer_create(desktop_uptime_refresh, 1000U, NULL);
    }
}
