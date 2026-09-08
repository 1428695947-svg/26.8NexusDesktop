/**
  ******************************************************************************
  * @file    app_health.h
  * @brief   应用任务心跳与超时恢复监测接口
  ******************************************************************************
  */

#ifndef __APP_HEALTH_H
#define __APP_HEALTH_H

#include <stdint.h>

typedef enum {
    APP_HEALTH_LVGL = 0,
    APP_HEALTH_MOUSE,
    APP_HEALTH_KEY,
    APP_HEALTH_DRAW,
    APP_HEALTH_LOG,
    APP_HEALTH_TASK_COUNT
} App_HealthTaskId_t;

typedef struct {
    uint32_t fault_mask;
    uint32_t timeout_count;
    uint32_t recovery_count;
} App_HealthStats_t;

void App_HealthInit(void);
void App_HealthBeat(App_HealthTaskId_t id);
void App_HealthTask(void *argument);
void App_HealthGetStats(App_HealthStats_t *out);

#endif /* __APP_HEALTH_H */
