/**
  ******************************************************************************
  * @file    key.c
  * @brief   按键驱动模块 - 释放时刻判定法状态机（单击/双击），提供回调函数接口
  * @author  Embedded Expert
  * @version V2.0
  * @date    2026-08-30
  ******************************************************************************
  * @attention
  * 1. 使用定时器中断进行消抖和双击判定，需要在定时器中断函数中调用按键扫描函数
  * 2. 状态机采用"释放时刻判定法"：按下沿消抖确认后立即触发 PRESS_DOWN；
  *    释放时先进入 WAIT_DOUBLE，500ms 内再次按下并释放则判定为 DOUBLE_CLICK，
  *    超时未再次按下则判定为 CLICK；每次释放均触发 RELEASE
  * 3. 按键状态（s_keyHandles）与事件标志（双缓冲事件区）分离：
  *    状态机只向写缓冲追加事件，一轮扫描结束后切换缓冲并统一派发，
  *    保证定时器中断中调用 Key_ScanHandler() 时不丢失事件
  * 4. 回调在定时器中断上下文中执行，应用层应使用 FromISR 接口（如 xQueueSendFromISR）
  * 5. 移植代码的时候需要修改的参数有：按键索引定义和按键句柄数组中的按键引脚
  ******************************************************************************
  */

#include "key.h"
#include "gpio.h"

/* ========================= 私有宏定义 ========================= */
// 按键消抖时间（单位：定时器中断周期，假设1ms一次）
#define KEY_DEBOUNCE_TIME_MS    20      // 20ms消抖时间

// 双击判定窗口（释放后等待第二次按下的超时时间）
#define KEY_DOUBLE_CLICK_TIME_MS 500    // 500ms双击窗口

// 按键数量（按键ID定义见 key.h）
#define KEY_MAX_NUM             1       // 按键数量

// 双缓冲事件区：每个缓冲可容纳的事件数（每轮扫描每个按键最多产生3个事件）
#define KEY_EVENT_BUF_SIZE      8       // 事件缓冲容量

// 按键引脚宏（按实际硬件接线修改）
#define JOYKEY_GPIO_Port        GPIOA       // 摇杆Z轴按键端口
#define JOYKEY_Pin              GPIO_PIN_2  // 摇杆Z轴按键引脚

/* ========================= 摇杆ADC模块预留接口 =========================
 * 说明：当前工程摇杆Z轴按键为数字输入（PA2），由 Key_ReadPin() 直接读取GPIO。
 *       若后续摇杆按键改为ADC采样判定（ADKEY），可在此实现：
 *       static uint8_t Key_AdcKeyRead(uint8_t keyId) { ... 读取ADC并返回0/1 ... }
 *       并在 Key_ProcessStateMachine() 中用 Key_AdcKeyRead() 替换 Key_ReadPin()。
 *       注意：本接口仅预留，不参与当前编译/扫描。
 * ==================================================================== */

/* ========================= 私有类型定义 ========================= */
// 按键状态机（释放时刻判定法）
typedef enum {
    KEY_STATE_IDLE = 0,         // 空闲状态
    KEY_STATE_DEBOUNCING,       // 消抖中状态
    KEY_STATE_PRESSED,          // 按下确认状态
    KEY_STATE_WAIT_DOUBLE       // 等待双击状态（500ms超时）
} Key_State_t;

// 按键句柄（状态机数据，与事件标志分离）
typedef struct {
    GPIO_TypeDef* port;         // GPIO端口
    uint16_t pin;               // GPIO引脚
    Key_State_t state;          // 当前状态
    uint32_t pressTime;         // 按下时刻（消抖计时用）
    uint32_t releaseTime;       // 释放时刻（双击窗口计时用）
    uint8_t isPressed;          // 当前是否处于按下确认状态
    uint8_t doubleClickPending; // 双击候选标志（第二次按下待确认）
} Key_Handle_t;

// 事件记录（双缓冲事件区元素）
typedef struct {
    uint8_t keyId;              // 按键ID
    Key_Event_t event;          // 事件类型
} Key_EventRecord_t;

/* ========================= 私有全局变量 ========================= */
// 按键句柄数组，用于储存按键状态机数据
// 说明：原工程遗留的 MODEKEY/ADCKEY 宏在当前工程中未定义且引脚未配置，
//       先以 port=NULL 作预留项（不参与扫描），接入实际按键时替换对应引脚即可
static Key_Handle_t s_keyHandles[KEY_MAX_NUM] = {
    {JOYKEY_GPIO_Port, JOYKEY_Pin, KEY_STATE_IDLE, 0, 0, 0, 0}   // 按键0：摇杆Z轴（PA2）
};

// 回调函数指针
static Key_PressDownCallback_t s_pressDownCallback = NULL;
static Key_ClickCallback_t s_clickCallback = NULL;
static Key_DoubleClickCallback_t s_doubleClickCallback = NULL;
static Key_ReleaseCallback_t s_releaseCallback = NULL;

// 按键扫描周期计数器（在定时器中断中递增）
static volatile uint32_t s_tickCounter = 0;

/* ========================= 双缓冲事件区 =========================
 * 说明：状态机（写侧）与事件派发（读侧）使用 A/B 双缓冲交替工作。
 *       每轮扫描状态机只向"写缓冲"追加事件，扫描结束后切换缓冲：
 *       刚写满的缓冲交给派发阶段，写侧立即切到另一块缓冲继续写入。
 *       由于读写两侧永远不共用同一块缓冲，定时器中断中反复调用
 *       Key_ScanHandler() 也不会覆盖/丢失尚未派发的事件。
 */
static Key_EventRecord_t s_eventBuffer[2][KEY_EVENT_BUF_SIZE];   // A/B双缓冲
static volatile uint8_t s_writeBufIdx = 0;                       // 当前写入缓冲索引
static volatile uint16_t s_eventCount[2] = {0, 0};               // 各缓冲内事件数

/* ========================= 私有函数声明 ========================= */
static void Key_ProcessStateMachine(uint8_t keyId);
static uint8_t Key_ReadPin(uint8_t keyId);
static void Key_TriggerEvent(uint8_t keyId, Key_Event_t event);
static void Key_PostEvent(uint8_t keyId, Key_Event_t event);
static void Key_SwapAndDispatch(void);

/* ========================= 公共函数实现 ========================= */

/**
  * @brief  初始化按键模块
  * @retval None
  */
void Key_Init(void)
{
    // 初始化所有按键状态机数据
    for (uint8_t i = 0; i < KEY_MAX_NUM; i++) {
        s_keyHandles[i].state = KEY_STATE_IDLE;
        s_keyHandles[i].pressTime = 0;
        s_keyHandles[i].releaseTime = 0;
        s_keyHandles[i].isPressed = 0;
        s_keyHandles[i].doubleClickPending = 0;
    }

    // 清空回调函数
    s_pressDownCallback = NULL;
    s_clickCallback = NULL;
    s_doubleClickCallback = NULL;
    s_releaseCallback = NULL;

    // 清空双缓冲事件区
    s_writeBufIdx = 0;
    s_eventCount[0] = 0;
    s_eventCount[1] = 0;

    s_tickCounter = 0;
}

/**
  * @brief  设置按下回调函数
  * @param  callback: 回调函数指针
  * @retval None
  */
void Key_SetPressDownCallback(Key_PressDownCallback_t callback)
{
    s_pressDownCallback = callback;
}

/**
  * @brief  设置单击回调函数
  * @param  callback: 回调函数指针
  * @retval None
  */
void Key_SetClickCallback(Key_ClickCallback_t callback)
{
    s_clickCallback = callback;
}

/**
  * @brief  设置双击回调函数
  * @param  callback: 回调函数指针
  * @retval None
  */
void Key_SetDoubleClickCallback(Key_DoubleClickCallback_t callback)
{
    s_doubleClickCallback = callback;
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
  * @note   内部流程：更新节拍 -> 状态机产生事件（写入写缓冲）
  *                   -> 切换双缓冲并统一派发全部事件
  */
void Key_ScanHandler(void)
{
    // 增加时间计数器
    s_tickCounter++;

    // 处理所有按键的状态机（产生的事件先写入写缓冲）
    for (uint8_t i = 0; i < KEY_MAX_NUM; i++) {
        Key_ProcessStateMachine(i);
    }

    // 一轮扫描结束后切换双缓冲并统一派发事件
    Key_SwapAndDispatch();
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
  * @brief  向当前写缓冲追加一个事件（仅状态机内部调用）
  * @param  keyId: 按键ID
  * @param  event: 事件类型
  * @note   每轮扫描每个按键最多产生3个事件，KEY_EVENT_BUF_SIZE 足够容纳；
  *         若缓冲意外写满则丢弃该事件（理论不会发生）
  */
static void Key_PostEvent(uint8_t keyId, Key_Event_t event)
{
    uint16_t idx = s_eventCount[s_writeBufIdx];

    if (idx >= KEY_EVENT_BUF_SIZE) {
        return;  // 缓冲满，丢弃（理论不会发生）
    }

    s_eventBuffer[s_writeBufIdx][idx].keyId = keyId;
    s_eventBuffer[s_writeBufIdx][idx].event = event;
    s_eventCount[s_writeBufIdx] = (uint16_t)(idx + 1U);
}

/**
  * @brief  切换双缓冲并派发刚产生的事件
  * @retval None
  * @note   把本轮写缓冲交给派发阶段，写侧立即切到另一块缓冲，
  *         保证写/读两侧互不干扰；事件按产生顺序依次触发回调
  */
static void Key_SwapAndDispatch(void)
{
    uint8_t readIdx = s_writeBufIdx;   // 本轮刚写入的缓冲作为读缓冲
    uint16_t count = s_eventCount[readIdx];

    // 写侧立即切到另一块缓冲
    s_writeBufIdx ^= 1U;

    // 按顺序派发全部事件，派发后清空该缓冲
    for (uint16_t i = 0; i < count; i++) {
        Key_TriggerEvent(s_eventBuffer[readIdx][i].keyId, s_eventBuffer[readIdx][i].event);
    }
    s_eventCount[readIdx] = 0;
}

/**
  * @brief  触发按键事件（运行在定时器中断上下文中）
  * @param  keyId: 按键ID
  * @param  event: 事件类型
  */
static void Key_TriggerEvent(uint8_t keyId, Key_Event_t event)
{
    switch (event) {
        case KEY_EVENT_PRESS_DOWN:
            if (s_pressDownCallback != NULL) {
                s_pressDownCallback(keyId);
            }
            break;

        case KEY_EVENT_CLICK:
            if (s_clickCallback != NULL) {
                s_clickCallback(keyId);
            }
            break;

        case KEY_EVENT_DOUBLE_CLICK:
            if (s_doubleClickCallback != NULL) {
                s_doubleClickCallback(keyId);
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
  * @brief  处理按键状态机（释放时刻判定法）
  * @param  keyId: 按键ID
  * @note   状态迁移：
  *         IDLE --按下沿--> DEBOUNCING --消抖完成--> PRESSED(触发PRESS_DOWN)
  *         DEBOUNCING --消抖期间释放--> IDLE（视为抖动）
  *         PRESSED --释放--> WAIT_DOUBLE(触发RELEASE)
  *         WAIT_DOUBLE --500ms超时--> IDLE(触发CLICK)
  *         WAIT_DOUBLE --再次按下--> DEBOUNCING(双击候选)
  *         PRESSED(双击候选) --释放--> IDLE(触发RELEASE + DOUBLE_CLICK)
  */
static void Key_ProcessStateMachine(uint8_t keyId)
{
    if (keyId >= KEY_MAX_NUM) return;

    Key_Handle_t* key = &s_keyHandles[keyId];
    uint8_t currentState = Key_ReadPin(keyId);

    switch (key->state) {
        /* 空闲状态 */
        case KEY_STATE_IDLE:
            if (currentState == 1) {  // 检测到按下沿
                key->state = KEY_STATE_DEBOUNCING;
                key->pressTime = s_tickCounter;
                key->doubleClickPending = 0;
            }
            break;

        /* 消抖状态 */
        case KEY_STATE_DEBOUNCING:
            if (currentState == 0) {  // 消抖期间释放，视为抖动
                if (key->doubleClickPending) {
                    // 第二次按下为抖动：第一次按下在此刻确认为单击
                    key->doubleClickPending = 0;
                    Key_PostEvent(keyId, KEY_EVENT_CLICK);
                }
                key->state = KEY_STATE_IDLE;
            } else if ((s_tickCounter - key->pressTime) >= KEY_DEBOUNCE_TIME_MS) {
                // 消抖完成，确认按下：立即触发按下事件
                key->state = KEY_STATE_PRESSED;
                key->isPressed = 1;
                Key_PostEvent(keyId, KEY_EVENT_PRESS_DOWN);
            }
            break;

        /* 按下确认状态 */
        case KEY_STATE_PRESSED:
            if (currentState == 0) {  // 按键释放：释放时刻判定单击/双击
                key->isPressed = 0;
                Key_PostEvent(keyId, KEY_EVENT_RELEASE);

                if (key->doubleClickPending) {
                    // 双击窗口内的第二次按下释放：确认双击
                    key->doubleClickPending = 0;
                    Key_PostEvent(keyId, KEY_EVENT_DOUBLE_CLICK);
                    key->state = KEY_STATE_IDLE;
                } else {
                    // 第一次按下释放：进入等待双击窗口，单击延后确认
                    key->state = KEY_STATE_WAIT_DOUBLE;
                    key->releaseTime = s_tickCounter;
                }
            }
            break;

        /* 等待双击状态 */
        case KEY_STATE_WAIT_DOUBLE:
            if (currentState == 1) {  // 双击窗口内检测到第二次按下沿
                key->state = KEY_STATE_DEBOUNCING;
                key->pressTime = s_tickCounter;
                key->doubleClickPending = 1;
            } else if ((s_tickCounter - key->releaseTime) >= KEY_DOUBLE_CLICK_TIME_MS) {
                // 双击窗口超时未再次按下：确认单击
                key->state = KEY_STATE_IDLE;
                Key_PostEvent(keyId, KEY_EVENT_CLICK);
            }
            break;
    }
}

/* ========================= 中断回调函数 ========================= */

/**
  * @brief  外部中断回调函数
  * @param  GPIO_Pin: 触发中断的引脚
  * @note   由于使用了定时器扫描，可以不需要外部中断
  *         但如果需要快速响应，可以保留此函数
  */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    // 这里可以设置标志位，让主循环快速响应
    // 但由于我们有定时扫描，所以这个函数可以留空或用于特殊处理
    (void)GPIO_Pin;  // 避免未使用参数警告
}
