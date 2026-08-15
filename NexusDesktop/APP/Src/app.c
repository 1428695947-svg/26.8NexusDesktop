/**
  ******************************************************************************
  * @file    app.c
  * @brief   应用层模块 - 模块初始化装配与按键回调
  * @author  Embedded Expert
  * @version V1.0
  * @date    2026-08-22
  ******************************************************************************
  * @attention
  * 1. 应用层只负责装配：按键/摇杆/定时器各驱动模块互不依赖
  * 2. 按键回调运行在TIM5中断上下文，通知任务需使用FromISR接口
  * 3. 集中在此处初始化，避免CubeMX重新生成main.c时中文注释乱码
  ******************************************************************************
  */

#include "app.h"

/* ========================= 私有全局变量 ========================= */
// 摇杆模块实例（由应用层持有）
Joystick_HandleTypeDef hjoy;

/* ========================= 私有函数声明 ========================= */
static void App_JoystickKeyCallback(uint8_t keyId);
static void App_KeyLongPressCallback(uint8_t keyId);
static void App_KeyLongPressHoldCallback(uint8_t keyId);
static void App_KeyReleaseCallback(uint8_t keyId);

/* ========================= 公共函数实现 ========================= */

/**
  * @brief  应用层初始化（在main中调用）
  * @retval None
  * @note   完成各驱动模块初始化、按键回调注册与TIM5中断启动
  */
void App_Init(void)
{
    /* 驱动模块初始化 */
    Key_Init();
    JOY_Init(&hjoy);

    /* 注册按键回调（短按/长按/长按保持/释放） */
    Key_SetShortPressCallback(App_JoystickKeyCallback);
    Key_SetLongPressCallback(App_KeyLongPressCallback);
    Key_SetLongPressHoldCallback(App_KeyLongPressHoldCallback);
    Key_SetReleaseCallback(App_KeyReleaseCallback);

    /* 启动TIM5 1ms周期中断，驱动按键扫描 */
    HAL_TIM_Base_Start_IT(&htim5);
}

/**
  * @brief  1ms节拍处理（在TIM5中断回调中调用）
  * @retval None
  */
void App_Tick1ms(void)
{
    Key_ScanHandler();
}

/* ========================= 私有函数实现 ========================= */

/**
  * @brief  按键短按回调
  * @param  keyId: 按键ID
  * @note   运行在TIM5中断上下文中
  */
static void App_JoystickKeyCallback(uint8_t keyId)
{
    if (keyId == KEY_ID_2) {        // 摇杆Z轴按键：注入鼠标左键点击
        JOY_InjectClick();
    }
}

/**
  * @brief  按键长按回调（预留）
  * @param  keyId: 按键ID
  */
static void App_KeyLongPressCallback(uint8_t keyId)
{
    (void)keyId;
    // TODO: 长按功能，按需实现
}

/**
  * @brief  按键长按保持回调（预留）
  * @param  keyId: 按键ID
  */
static void App_KeyLongPressHoldCallback(uint8_t keyId)
{
    (void)keyId;
    // TODO: 长按保持功能，按需实现
}

/**
  * @brief  按键释放回调（预留）
  * @param  keyId: 按键ID
  */
static void App_KeyReleaseCallback(uint8_t keyId)
{
    (void)keyId;
    // TODO: 释放功能，按需实现
}
