/**
  ******************************************************************************
  * @file    app.h
  * @brief   应用层模块头文件
  ******************************************************************************
  */

#ifndef __APP_H
#define __APP_H

#ifdef __cplusplus
extern "C" {
#endif

/* 包含必要的头文件 */
#include "main.h"
#include "tim.h"
#include "key.h"
#include "joystick.h"

/* ========================= 外部变量声明 ========================= */
extern Joystick_HandleTypeDef hjoy;

/* ========================= 公共函数声明 ========================= */
void App_Init(void);
void App_Tick1ms(void);

#ifdef __cplusplus
}
#endif

#endif /* __APP_H */
