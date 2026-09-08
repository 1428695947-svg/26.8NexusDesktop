/**
  ******************************************************************************
  * @file    app_draw.h
  * @brief   画图应用模块 - 画图应用任务 (任务划分之一)
  * @note    原生 LCD 绘制 + 独立 FreeRTOS 任务, 摇杆+PA2 鼠标操控。
  *          前台状态由查询/切换接口维护，不向其他模块暴露内部变量。
  ******************************************************************************
  */

#ifndef __APP_DRAW_H
#define __APP_DRAW_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

/* ===================== 公共接口 ===================== */
void    App_DrawTask(void *argument);          /* 画图应用任务入口 (freertos 创建) */
void    App_RequestDrawOpen(void);             /* 桌面"画图"按钮请求打开应用 (custom.c 调用) */
uint8_t App_ConsumeDrawOpenRequest(void);      /* 消费打开请求 (LVGL 任务轮询) */
void    App_DrawEnterForeground(void);         /* 由 UI 协调器把画图切到前台 */
uint8_t App_DrawIsForeground(void);             /* 查询是否由画图独占 LCD */
void    App_TouchDraw(void);                   /* 触摸画图: 按下画蓝点, 拖动连成蓝线 */

#ifdef __cplusplus
}
#endif

#endif /* __APP_DRAW_H */
