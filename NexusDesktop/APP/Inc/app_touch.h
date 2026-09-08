/**
  ******************************************************************************
  * @file    app_touch.h
  * @brief   触摸诊断与校准服务接口
  ******************************************************************************
  */

#ifndef __APP_TOUCH_H
#define __APP_TOUCH_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

void App_TouchMonitorTask(void);
void App_TouchShowDbg(void);
void App_TouchCalibrate(void);
uint8_t App_TouchIsCalibrated(void);

#ifdef __cplusplus
}
#endif

#endif /* __APP_TOUCH_H */
