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

/* 鼠标输入状态 (由 App_MouseUpdate() 维护, 供 LVGL 指针输入设备读取) */
extern volatile int     g_mouse_x;
extern volatile int     g_mouse_y;
extern volatile uint8_t g_mouse_pressed;

/* ========================= 按键事件消息（应用层接口） ========================= */
// 按键回调（TIM5中断上下文）通过 FreeRTOS 队列发送本消息给任务处理；
// eventType 取值与 key.h 中 Key_Event_t 枚举一一对应
typedef struct {
    uint8_t keyId;      // 按键ID（KEY_ID_0 / KEY_ID_1 / KEY_ID_2）
    uint8_t eventType;  // 0:PRESS_DOWN, 1:CLICK, 2:DOUBLE_CLICK, 3:RELEASE
} KeyEventMsg_t;

/* ========================= 公共函数声明 ========================= */
void App_Init(void);
void App_GuiInit(void);            /* GUI Guider 界面初始化 (登录/桌面 + 小猫光标 + 密码 Flash) */
void App_MouseUpdate(void);        /* 摇杆/触摸/PA2 -> 鼠标坐标与按下状态 */
void App_SetMouseSpeed(float speed);/* 设置摇杆鼠标移动速度（px/frame，浮点数） */
void App_ProcessInputEvents(void); /* 消费输入事件队列, 驱动 LVGL 指针点击/长按重复 */
void App_EnterPowerOff(void);      /* 进入熄屏状态 (关机), 摇杆移动或按键按下唤醒 */
uint8_t App_IsMouseConnected(void);/* 查询摇杆(鼠标)是否已连接: 1=已连接 */
void App_Tick1ms(void);
void App_KeyEventTask(void);       /* 按键事件消费任务 (FreeRTOS队列接收并处理) */
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
