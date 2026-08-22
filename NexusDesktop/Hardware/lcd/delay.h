/**
  ******************************************************************************
  * @file    delay.h
  * @brief   简易延时模块 (HAL 移植: ILI9488 LCD + XPT2046 触摸用)
  * @note    由厂家 Demo 的 SYSTEM/delay 移植而来, 改为基于 HAL_Delay / DWT 实现
  ******************************************************************************
  */
#ifndef __DELAY_H
#define __DELAY_H

#include "main.h"

void delay_init(void);          /* 初始化 DWT 周期计数器 */
void delay_us(uint32_t nus);    /* 微秒延时 (DWT 忙等) */
void delay_ms(uint16_t nms);    /* 毫秒延时 (HAL_Delay) */

#endif
