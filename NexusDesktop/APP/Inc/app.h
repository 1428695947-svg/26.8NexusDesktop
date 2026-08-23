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
void App_JoystickTask(void);
void App_LvglTask(void);           /* LVGL 界面重绘与处理任务 */
void App_CreatePaintUI(void);      /* 创建 LVGL 画画板 UI */
void App_TouchMonitorTask(void);   /* 触摸监控任务 (调试用, 周期刷新 dbg_tp_* 变量) */
void App_TouchDraw(void);          /* 触摸画图: 按下画蓝点, 拖动连成蓝线 */
void App_TouchShowDbg(void);       /* 触摸硬件自检: 屏显引脚电平 + 原始 AD 值 */
void App_TouchCalibrate(void);     /* 多点校准(3x3=9点, 最小二乘拟合), 结果存 Flash 最近一条 */
uint8_t App_TouchIsCalibrated(void);/* 查询是否已有掉电保存的校准数据 */

#ifdef __cplusplus
}
#endif

#endif /* __APP_H */
