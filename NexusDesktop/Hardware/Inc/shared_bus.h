/**
  ******************************************************************************
  * @file    shared_bus.h
  * @brief   SDIO 与 XPT2046 共享引脚模式仲裁
  ******************************************************************************
  */

#ifndef SHARED_BUS_H
#define SHARED_BUS_H

#include <stdint.h>

/** 将 PC8~PC12、PD2 切换为 SDIO 复用功能。 */
void SharedBus_SelectSd(void);

/** 将 PC10~PC12、PD2 恢复为 XPT2046 软件 SPI。 */
void SharedBus_SelectTouch(void);

/** @return 1=共享引脚当前归 SDIO 使用，0=可由触摸驱动使用。 */
uint8_t SharedBus_IsSdSelected(void);

#endif /* SHARED_BUS_H */
