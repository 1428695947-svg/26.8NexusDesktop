/**
  ******************************************************************************
  * @file    app_health.c
  * @brief   应用任务心跳与超时恢复监测实现
  * @note    各任务只上报轻量心跳；独立健康任务统一判断超时与恢复并记录日志。
  ******************************************************************************
  */

#include "app_health.h"

#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "app_log.h"

#define HEALTH_TIMEOUT_MS  5000U
#define HEALTH_PERIOD_MS   1000U
/* 仅实验固件置 1：8~15 秒屏蔽 LVGL 心跳，用于验证超时发现与自动恢复。 */
#define APP_HEALTH_SELF_TEST  0U

static volatile uint32_t s_last_beat[APP_HEALTH_TASK_COUNT];
static volatile App_HealthStats_t s_stats;
/* 调试器故障注入掩码：置位后忽略对应任务心跳；正常固件始终为 0。 */
static volatile uint32_t s_test_mask;

static const char *const s_task_names[APP_HEALTH_TASK_COUNT] = {
    "LVGL", "MOUSE", "KEY", "DRAW", "LOG"
};

void App_HealthInit(void)
{
    uint32_t now = (uint32_t)xTaskGetTickCount();
    uint32_t i;

    for (i = 0U; i < APP_HEALTH_TASK_COUNT; i++) {
        s_last_beat[i] = now;
    }
    memset((void *)&s_stats, 0, sizeof(s_stats));
    s_test_mask = 0U;
}

void App_HealthBeat(App_HealthTaskId_t id)
{
    if ((uint32_t)id < APP_HEALTH_TASK_COUNT &&
        (s_test_mask & (1UL << (uint32_t)id)) == 0U) {
        s_last_beat[id] = (uint32_t)xTaskGetTickCount();
    }
}

void App_HealthTask(void *argument)
{
    uint32_t i;

    (void)argument;
    for (;;) {
        uint32_t now = (uint32_t)xTaskGetTickCount();

#if APP_HEALTH_SELF_TEST
        if (now >= pdMS_TO_TICKS(8000U) && now < pdMS_TO_TICKS(15000U)) {
            s_test_mask = 1UL << APP_HEALTH_LVGL;
        } else if (now >= pdMS_TO_TICKS(15000U)) {
            s_test_mask = 0U;
        }
#endif

        for (i = 0U; i < APP_HEALTH_TASK_COUNT; i++) {
            uint32_t mask = 1UL << i;
            uint8_t timed_out = ((now - s_last_beat[i]) > pdMS_TO_TICKS(HEALTH_TIMEOUT_MS)) ? 1U : 0U;

            if (timed_out && (s_stats.fault_mask & mask) == 0U) {
                s_stats.fault_mask |= mask;
                s_stats.timeout_count++;
                App_Log_Error("任务超时 task=%s", s_task_names[i]);
            } else if (!timed_out && (s_stats.fault_mask & mask) != 0U) {
                s_stats.fault_mask &= ~mask;
                s_stats.recovery_count++;
                App_Log_Event(LOG_LEVEL_WARN, "任务恢复 task=%s", s_task_names[i]);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(HEALTH_PERIOD_MS));
    }
}

void App_HealthGetStats(App_HealthStats_t *out)
{
    if (out != NULL) {
        taskENTER_CRITICAL();
        memcpy(out, (const void *)&s_stats, sizeof(*out));
        taskEXIT_CRITICAL();
    }
}
