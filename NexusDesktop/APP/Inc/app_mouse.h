/**
  ******************************************************************************
  * @file    app_mouse.h
  * @brief   鼠标/输入模块 - 鼠标ADC读取任务 (任务划分之一)
  * @note    摇杆 ADC 采样 + 触摸 + PA2 按键 -> 鼠标坐标与按下状态。
  *          g_mouse_x/g_mouse_y/g_mouse_pressed 为与图形刷新(LVGL)任务共享的变量。
  ******************************************************************************
  */

#ifndef __APP_MOUSE_H
#define __APP_MOUSE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include "key.h"

/* ===================== 共享变量 (与 LVGL 图形刷新/画图任务共享) ===================== */
extern volatile int     g_mouse_x;         /* 鼠标 X 坐标 */
extern volatile int     g_mouse_y;         /* 鼠标 Y 坐标 */
extern volatile uint8_t g_mouse_pressed;   /* 左键按下状态 (1=按下) */

/* ===================== 公共接口 ===================== */
void     App_MouseInit(void);            /* 初始化摇杆和输入模块状态 */
void     App_InputTick1ms(void);          /* TIM5 ISR 中调用的输入节拍 */
void     App_MouseTask(void);            /* 鼠标ADC读取任务入口 (freertos 创建) */
void     App_MouseUpdate(void);          /* 摇杆/触摸/PA2 -> 鼠标坐标与按下状态 */
void     App_ProcessInputEvents(void);   /* 消费输入事件队列, 驱动 LVGL 指针 (仅LVGL任务上下文) */
void     App_InputReadPointer(int *x, int *y, uint8_t *pressed, uint8_t *more); /* LVGL逐个读取指针边沿 */
void     App_SetMouseSpeed(float speed); /* 设置摇杆鼠标移动速度 */
uint8_t  App_IsMouseConnected(void);     /* 查询摇杆(鼠标)是否已连接 */
uint8_t  App_MouseBtnDown(void);         /* 查询 PA2 鼠标左键是否按下 */
void     App_GetInputStats(uint32_t *events, uint32_t *dropped); /* 输入事件统计 */
void     App_InputGetQueueStats(uint8_t *current, uint8_t *peak); /* 按键队列占用统计 */
void     App_InputGetPointerStats(uint8_t *current, uint8_t *peak, uint32_t *consumed); /* 指针边沿队列统计 */
void     App_InputFlush(void);           /* 清空残留输入事件 (画图切回桌面时) */
void     App_InputHandleKeyEvent(uint8_t key_id, Key_Event_t event); /* 任务上下文处理实体键事件 */
void     App_InputRecordKeyQueueResult(uint8_t queued); /* ISR上下文记录按键队列投递结果 */
void     App_MouseGetVector(float *x, float *y); /* 读取摇杆归一化方向 (供画图应用) */
void     App_KeyEventTask(void);          /* 实体按键事件队列消费任务 */

#ifdef __cplusplus
}
#endif

#endif /* __APP_MOUSE_H */
