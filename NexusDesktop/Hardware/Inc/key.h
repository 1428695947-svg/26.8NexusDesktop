
/**
  ******************************************************************************
  * @file    key.h
  * @brief   按键驱动模块头文件
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

/* ========================= 回调函数类型定义 ========================= */
/**
  * @brief  短按回调函数类型
  * @param  keyId: 按键ID（0, 1, 2...）
  */
typedef void (*Key_ShortPressCallback_t)(uint8_t keyId);

/**
  * @brief  长按回调函数类型
  * @param  keyId: 按键ID
  */
typedef void (*Key_LongPressCallback_t)(uint8_t keyId);

/**
  * @brief  长按保持回调函数类型
  * @param  keyId: 按键ID
  * @note   在长按期间会周期性触发
  */
typedef void (*Key_LongPressHoldCallback_t)(uint8_t keyId);


/**
  * @brief  释放回调函数类型
  * @param  keyId: 按键ID
  */
typedef void (*Key_ReleaseCallback_t)(uint8_t keyId);

/* ========================= 公共函数声明 ========================= */
void Key_Init(void);
void Key_ScanHandler(void);
uint32_t Key_GetTick(void);

void Key_SetShortPressCallback(Key_ShortPressCallback_t callback);
void Key_SetLongPressCallback(Key_LongPressCallback_t callback);
void Key_SetLongPressHoldCallback(Key_LongPressHoldCallback_t callback);
void Key_SetReleaseCallback(Key_ReleaseCallback_t callback);

#ifdef __cplusplus
}
#endif

#endif /* __KEY_H */
