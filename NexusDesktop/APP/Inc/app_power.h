/**
  ******************************************************************************
  * @file    app_power.h
  * @brief   应用电源状态服务 - 熄屏、空闲计时与输入唤醒
  ******************************************************************************
  */

#ifndef __APP_POWER_H
#define __APP_POWER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

void App_PowerInit(void);
void App_SetSleepSec(uint16_t sec);
void App_PowerRecordActivity(void);
void App_PowerPoll(void);
void App_EnterPowerOff(void);

/**
  * @brief  熄屏期间处理输入唤醒
  * @retval 1=调用时处于熄屏状态，本轮输入不应继续传给桌面；0=正常活动状态
  */
uint8_t App_PowerHandleWake(uint8_t button_down,
                            uint8_t pointer_connected,
                            uint8_t pointer_moved,
                            int pointer_x,
                            int pointer_y);

#ifdef __cplusplus
}
#endif

#endif /* __APP_POWER_H */
