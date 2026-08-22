/**
  ******************************************************************************
  * @file    joystick.h
  * @brief   PS2双轴摇杆模块头文件（STM32F407VET6 + HAL库）
  * @author  Embedded Expert
  * @version V1.2
  * @date    2026-08-22
  * @attention
  * 公有接口仅 2 个：JOY_Init / JOY_Update
  * 数据读取、归一化、曲线映射等内部流程均为私有实现，见 joystick.c
  ******************************************************************************
  */

#ifndef __JOYSTICK_H
#define __JOYSTICK_H

#ifdef __cplusplus
extern "C" {
#endif

/* ========================= 头文件引用 ========================= */
#include "main.h"
#include "adc.h"

/* ========================= 引脚宏定义 ========================= */
#define JOY_ADC_HANDLE          (&hadc1)
#define JOY_X_ADC_CHANNEL       ADC_CHANNEL_0
#define JOY_Y_ADC_CHANNEL       ADC_CHANNEL_1

/* ========================= 算法参数宏定义 ========================= */
#define JOY_DEAD_ZONE           50      /* 死区阈值（ADC原始值） */
#define JOY_SENSITIVITY         1.2f    /* 灵敏度系数 */
#define JOY_SAMPLE_COUNT        20      /* 校准采样次数 */

/* ========================= 数据结构定义 ========================= */
typedef struct {
    uint16_t x_raw;         /* X轴ADC原始值 */
    uint16_t y_raw;         /* Y轴ADC原始值 */
    float x_norm;           /* X轴归一化值（-1.0~1.0） */
    float y_norm;           /* Y轴归一化值（-1.0~1.0） */
    uint16_t center_x;      /* X轴校准中心值 */
    uint16_t center_y;      /* Y轴校准中心值 */
    uint16_t dead_zone;     /* 死区阈值 */
    float sensitivity;      /* 灵敏度系数 */
} Joystick_HandleTypeDef;

/* ========================= 公有函数声明 ========================= */
// 初始化并校准中心（上电调用一次，校准时摇杆居中）
void JOY_Init(Joystick_HandleTypeDef *hjoy);
// 一帧完整处理：读原始值->归一化->平方曲线（周期任务调用）
void JOY_Update(Joystick_HandleTypeDef *hjoy);

#ifdef __cplusplus
}
#endif

#endif /* __JOYSTICK_H */
