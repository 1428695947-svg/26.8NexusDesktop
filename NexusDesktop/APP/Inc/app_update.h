/**
  ******************************************************************************
  * @file    app_update.h
  * @brief   系统版本与伪 OTA 演示应用
  ******************************************************************************
  */

#ifndef __APP_UPDATE_H
#define __APP_UPDATE_H

#include <stdint.h>

#define APP_FW_VERSION         "1.0.0"
#define APP_FW_DEMO_VERSION    "1.0.1-demo"

typedef enum {
    APP_UPDATE_IDLE = 0,
    APP_UPDATE_CHECKING,
    APP_UPDATE_AVAILABLE,
    APP_UPDATE_DOWNLOADING,
    APP_UPDATE_VERIFYING,
    APP_UPDATE_READY
} App_UpdateState_t;

void App_UpdateOpen(void);
void App_UpdateClose(void);
App_UpdateState_t App_UpdateGetState(void);
const char *App_UpdateGetStateText(void);

#endif /* __APP_UPDATE_H */
