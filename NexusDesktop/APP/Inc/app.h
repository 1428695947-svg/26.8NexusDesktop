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
#include "app_mouse.h"
#include "app_power.h"
#include "app_touch.h"
#include "app_draw.h"

/* ========================= 公共函数声明 ========================= */
void App_Init(void);
void App_GuiInit(void);            /* GUI Guider 界面初始化 (登录/桌面 + 小猫光标 + 密码 Flash) */
void App_ApplySettings(void);      /* 应用开机保存的系统设置 (灵敏度/大小/亮度/熄屏) */
void App_Tick1ms(void);
void App_LvglTask(void);           /* LVGL 界面重绘与处理任务 */
void App_CreatePaintUI(void);      /* 创建 LVGL 画画板 UI */

#ifdef __cplusplus
}
#endif

#endif /* __APP_H */
