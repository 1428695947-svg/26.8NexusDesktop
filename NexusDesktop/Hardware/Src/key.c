
/**
  ******************************************************************************
  * @file    key.c
  * @brief   按键驱动模块 - 支持短按/长按检测，提供回调函数接口
  * @author  Embedded Expert
  * @version V1.0
  * @date    2024
  ******************************************************************************
  * @attention
  * 1. 该模块使用外部中断或定时器定时扫描检测按键按下，若使用外部中断需注意开启中断
  * 2. 使用定时器中断进行消抖和长按检测，需要在定时器中断函数中调用按键扫描函数
  * 3. 该模块提供标准化的回调函数接口，在主函数中需要注册回调函数接口，并实现所需函数
  * 4. 移植代码的时候需要修改的参数有：按键索引定义和按键句柄数组中的按键引脚
  ******************************************************************************
  */

#include "key.h"
#include "gpio.h"

/* ========================= 私有宏定义 ========================= */
// 按键消抖时间（单位：定时器中断周期，假设1ms一次）
#define KEY_DEBOUNCE_TIME_MS    20      // 20ms消抖时间
#define KEY_LONG_PRESS_TIME_MS  1000    // 1000ms长按判定时间

// 按键数量（按键ID定义见 key.h）
#define KEY_MAX_NUM             1       // 按键数量

// 按键引脚宏（按实际硬件接线修改）
#define JOYKEY_GPIO_Port        GPIOA       // 摇杆Z轴按键端口
#define JOYKEY_Pin              GPIO_PIN_2  // 摇杆Z轴按键引脚

/* ========================= 私有类型定义 ========================= */
// 按键状态机
typedef enum {
    KEY_STATE_RELEASED = 0,     // 按键释放状态
    KEY_STATE_DEBOUNCING,       // 消抖中状态
    KEY_STATE_PRESSED,          // 按键按下状态
    KEY_STATE_LONG_PRESSED      // 长按状态
} Key_State_t;

// 按键事件类型
typedef enum {
    KEY_EVENT_NONE = 0,         // 无事件
    KEY_EVENT_SHORT_PRESS,      // 短按事件
    KEY_EVENT_LONG_PRESS,       // 长按事件
    KEY_EVENT_LONG_PRESS_HOLD,  // 长按保持事件
    KEY_EVENT_RELEASE           // 释放事件
} Key_Event_t;

// 按键结构体
typedef struct {
    GPIO_TypeDef* port;         // GPIO端口
    uint16_t pin;               // GPIO引脚
    Key_State_t state;          // 当前状态
    uint32_t pressStartTime;    // 按下开始时间
    uint8_t isPressed;          // 当前是否按下
    uint8_t longPressReported;  // 长按是否已上报
} Key_Handle_t;

/* ========================= 私有全局变量 ========================= */
// 按键句柄数组，用于储存按键状态
// 说明：原工程遗留的 MODEKEY/ADCKEY 宏在当前工程中未定义且引脚未配置，
//       先以 port=NULL 作预留项（不参与扫描），接入实际按键时替换对应引脚即可
static Key_Handle_t s_keyHandles[KEY_MAX_NUM] = {
    {JOYKEY_GPIO_Port, JOYKEY_Pin, KEY_STATE_RELEASED, 0, 0, 0}   // 按键0：摇杆Z轴（PA2）
};

// 回调函数指针
static Key_ShortPressCallback_t s_shortPressCallback = NULL;
static Key_LongPressCallback_t s_longPressCallback = NULL;
static Key_LongPressHoldCallback_t s_longPressHoldCallback = NULL;
static Key_ReleaseCallback_t s_releaseCallback = NULL;

// 按键扫描周期计数器（在定时器中断中递增）
static volatile uint32_t s_tickCounter = 0;

/* ========================= 私有函数声明 ========================= */
static void Key_ProcessStateMachine(uint8_t keyId);
static uint8_t Key_ReadPin(uint8_t keyId);
static void Key_TriggerEvent(uint8_t keyId, Key_Event_t event);

/* ========================= 公共函数实现 ========================= */

/**
  * @brief  初始化按键模块
  * @retval None
  */
void Key_Init(void)
{
    // 初始化所有按键状态
    for (uint8_t i = 0; i < KEY_MAX_NUM; i++) {
        s_keyHandles[i].state = KEY_STATE_RELEASED;
        s_keyHandles[i].pressStartTime = 0;
        s_keyHandles[i].isPressed = 0;
        s_keyHandles[i].longPressReported = 0;
    }

    // 清空回调函数
    s_shortPressCallback = NULL;
    s_longPressCallback = NULL;
    s_longPressHoldCallback = NULL;
    s_releaseCallback = NULL;

    s_tickCounter = 0;
}

/**
  * @brief  设置短按回调函数
  * @param  callback: 回调函数指针
  * @retval None
  */
void Key_SetShortPressCallback(Key_ShortPressCallback_t callback)
{
    s_shortPressCallback = callback;
}

/**
  * @brief  设置长按回调函数
  * @param  callback: 回调函数指针
  * @retval None
  */
void Key_SetLongPressCallback(Key_LongPressCallback_t callback)
{
    s_longPressCallback = callback;
}

/**
  * @brief  设置长按保持回调函数
  * @param  callback: 回调函数指针
  * @retval None
  */
void Key_SetLongPressHoldCallback(Key_LongPressHoldCallback_t callback)
{
    s_longPressHoldCallback = callback;
}

/**
  * @brief  设置释放回调函数
  * @param  callback: 回调函数指针
  * @retval None
  */
void Key_SetReleaseCallback(Key_ReleaseCallback_t callback)
{
    s_releaseCallback = callback;
}

/**
  * @brief  按键扫描处理（需在定时器中断中调用，建议1ms调用一次）
  * @retval None
  */
void Key_ScanHandler(void)
{
    // 增加时间计数器
    s_tickCounter++;

    // 处理所有按键的状态机
    for (uint8_t i = 0; i < KEY_MAX_NUM; i++) {
        Key_ProcessStateMachine(i);
    }
}

/**
  * @brief  获取当前时间戳（单位：ms）
  * @retval 当前时间戳
  */
uint32_t Key_GetTick(void)
{
    return s_tickCounter;
}

/* ========================= 私有函数实现 ========================= */

/**
  * @brief  读取按键引脚状态
  * @param  keyId: 按键ID
  * @retval 1: 按键按下, 0: 按键释放
  * @note   假设按键按下为低电平（接GND）
  */
static uint8_t Key_ReadPin(uint8_t keyId)
{
    if (keyId >= KEY_MAX_NUM) return 1;

    Key_Handle_t* key = &s_keyHandles[keyId];
    if (key->port == NULL) return 1;   // 预留按键（未配置引脚），视为释放
    return (HAL_GPIO_ReadPin(key->port, key->pin) == GPIO_PIN_RESET);
}

/**
  * @brief  触发按键事件
  * @param  keyId: 按键ID
  * @param  event: 事件类型
  */
static void Key_TriggerEvent(uint8_t keyId, Key_Event_t event)
{
    switch (event) {
        case KEY_EVENT_SHORT_PRESS:
            if (s_shortPressCallback != NULL) {
                s_shortPressCallback(keyId);
            }
            break;

        case KEY_EVENT_LONG_PRESS:
            if (s_longPressCallback != NULL) {
                s_longPressCallback(keyId);
            }
            break;

        case KEY_EVENT_LONG_PRESS_HOLD:
            if (s_longPressHoldCallback != NULL) {
                s_longPressHoldCallback(keyId);
            }
            break;

        case KEY_EVENT_RELEASE:
            if (s_releaseCallback != NULL) {
                s_releaseCallback(keyId);
            }
            break;

        default:
            break;
    }
}

/**
  * @brief  处理按键状态机
  * @param  keyId: 按键ID
  */
static void Key_ProcessStateMachine(uint8_t keyId)
{
    if (keyId >= KEY_MAX_NUM) return;

    Key_Handle_t* key = &s_keyHandles[keyId];
    uint8_t currentState = Key_ReadPin(keyId);

    switch (key->state) {
        /* 释放状态 */
        case KEY_STATE_RELEASED:
            if (currentState == 1) {  // 检测到按下
                key->state = KEY_STATE_DEBOUNCING;
                key->pressStartTime = s_tickCounter;
            }
            break;

        /* 消抖状态 */
        case KEY_STATE_DEBOUNCING:
            if (currentState == 0) {  // 消抖期间释放，回到释放状态
                key->state = KEY_STATE_RELEASED;
            } else if ((s_tickCounter - key->pressStartTime) >= KEY_DEBOUNCE_TIME_MS) {
                // 消抖完成，确认按下
                key->state = KEY_STATE_PRESSED;
                key->isPressed = 1;
                key->longPressReported = 0;
            }
            break;

        /* 按下状态 */
        case KEY_STATE_PRESSED:
            if (currentState == 0) {  // 按键释放
                key->state = KEY_STATE_RELEASED;
                key->isPressed = 0;
                // 触发短按事件（因为还未达到长按时间）
                Key_TriggerEvent(keyId, KEY_EVENT_SHORT_PRESS);
                Key_TriggerEvent(keyId, KEY_EVENT_RELEASE);
            } else {
                // 检查是否达到长按时间
                uint32_t pressDuration = s_tickCounter - key->pressStartTime;
                if (pressDuration >= KEY_LONG_PRESS_TIME_MS) {
                    key->state = KEY_STATE_LONG_PRESSED;
                    // 触发长按事件（仅一次）
                    if (!key->longPressReported) {
                        Key_TriggerEvent(keyId, KEY_EVENT_LONG_PRESS);
                        key->longPressReported = 1;
                    }
                }
            }
            break;

        /* 长按状态 */
        case KEY_STATE_LONG_PRESSED:
            if (currentState == 0) {  // 按键释放
                key->state = KEY_STATE_RELEASED;
                key->isPressed = 0;
                key->longPressReported = 0;
                Key_TriggerEvent(keyId, KEY_EVENT_RELEASE);
            } else {
                // 长按保持状态下，定期触发长按保持事件
                // 可以根据需要调整触发频率
                uint32_t pressDuration = s_tickCounter - key->pressStartTime;
                if ((pressDuration > KEY_LONG_PRESS_TIME_MS) &&
                    ((pressDuration % 100) == 0)) {  // 每100ms触发一次
                    Key_TriggerEvent(keyId, KEY_EVENT_LONG_PRESS_HOLD);
                }
            }
            break;
    }
}

/* ========================= 中断回调函数 ========================= */

/**
  * @brief  外部中断回调函数
  * @param  GPIO_Pin: 触发中断的引脚
  * @note   由于使用了状态机扫描，可以不需要外部中断
  *         但如果需要快速响应，可以保留此函数
  */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    // 这里可以设置标志位，让主循环快速响应
    // 但由于我们有定时扫描，所以这个函数可以留空或用于特殊处理
    (void)GPIO_Pin;  // 避免未使用参数警告
}
