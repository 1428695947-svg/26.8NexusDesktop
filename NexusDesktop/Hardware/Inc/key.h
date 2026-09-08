/**
  ******************************************************************************
  * @file    key.h
  * @brief   按键驱动模块头文件
  * @version V2.0
  * @date    2026-08-30
  * @note    状态机重构：释放时刻判定法（IDLE/DEBOUNCING/PRESSED/WAIT_DOUBLE），
  *          事件类型：PRESS_DOWN/CLICK/DOUBLE_CLICK/RELEASE，不再支持长按
  ******************************************************************************
  */

#ifndef __KEY_H
#define __KEY_H

#ifdef __cplusplus
extern "C" {
#endif

/* 包含必要的头文件 */
#include "main.h"

/* ========================= 按键ID定义（供回调函数识别按键） ========================= */
#define KEY_ID_0                0       // 按键0
#define KEY_ID_1                1       // 按键1
#define KEY_ID_2                2       // 按键2（摇杆Z轴按键）

/* ========================= 按键事件类型定义 ========================= */
// 注意：枚举取值与应用层 KeyEventMsg_t.eventType 一一对应（0/1/2/3）
typedef enum {
    KEY_EVENT_PRESS_DOWN = 0,   // 按下事件（消抖确认后立即触发）
    KEY_EVENT_CLICK = 1,        // 单击事件（释放后双击窗口超时确认）
    KEY_EVENT_DOUBLE_CLICK = 2, // 双击事件（第二次按下释放时确认）
    KEY_EVENT_RELEASE = 3       // 释放事件（每次释放立即触发）
} Key_Event_t;

/* ========================= 回调函数类型定义 ========================= */
/**
  * @brief  按下回调函数类型
  * @param  keyId: 按键ID（0, 1, 2...）
  * @note   消抖确认后立即触发，运行在定时器中断上下文中
  */
typedef void (*Key_PressDownCallback_t)(uint8_t keyId);

/**
  * @brief  单击回调函数类型
  * @param  keyId: 按键ID
  * @note   释放后双击窗口（500ms）超时未再次按下时触发
  */
typedef void (*Key_ClickCallback_t)(uint8_t keyId);

/**
  * @brief  双击回调函数类型
  * @param  keyId: 按键ID
  * @note   双击窗口内第二次按下并释放时触发
  */
typedef void (*Key_DoubleClickCallback_t)(uint8_t keyId);

/**
  * @brief  释放回调函数类型
  * @param  keyId: 按键ID
  * @note   每次释放立即触发
  */
typedef void (*Key_ReleaseCallback_t)(uint8_t keyId);

/* ========================= 公共函数声明 ========================= */
void Key_Init(void);
void Key_ScanHandler(void);
uint32_t Key_GetTick(void);
uint8_t Key_IsPressed(uint8_t keyId);  /* 读取已消抖的持续按下状态 */

void Key_SetPressDownCallback(Key_PressDownCallback_t callback);
void Key_SetClickCallback(Key_ClickCallback_t callback);
void Key_SetDoubleClickCallback(Key_DoubleClickCallback_t callback);
void Key_SetReleaseCallback(Key_ReleaseCallback_t callback);

#ifdef __cplusplus
}
#endif

#endif /* __KEY_H */
