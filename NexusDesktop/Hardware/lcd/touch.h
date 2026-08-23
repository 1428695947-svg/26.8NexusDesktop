/**
  ******************************************************************************
  * @file    touch.h
  * @brief   XPT2046 电阻触摸屏驱动 (软件模拟 SPI) - STM32F407VET6
  * @note    由厂家 Demo (HARDWARE/TOUCH) 移植, 底层 GPIO 改为 HAL 实现。
  *
  *          触摸接口采用软件模拟 SPI (与厂家 Demo 一致), 引脚分配如下
  *          (按用户实际接线勘误):
  *            T_CLK  -> PC10  (触摸 SPI 时钟, 输出)
  *            T_CS   -> PC11  (触摸片选, 输出)
  *            T_DIN  -> PC12  (触摸 SPI 数据输入 MCU->XPT2046, 输出)
  *            T_DO   -> PD2   (触摸 SPI 数据输出 XPT2046->MCU, 输入上拉)
  *            T_IRQ  -> PD3   (触摸中断/按下检测, EXTI 下降沿, 输入上拉)
  *
  *          接线要求: 模块触摸排针 T_CLK/T_PEN/T_DOUT/T_CS/T_DIN 按上表接入。
  ******************************************************************************
  */
#ifndef __TOUCH_H
#define __TOUCH_H

#include "main.h"

#define TP_PRES_DOWN 0x80   /* 笔按下标志 */
#define TP_CATH_PRES 0x40   /* 笔按下并已捕获标志 */

/* ==================== 触摸引脚定义 (HAL, 用户实际接线) ==================== */
#define TP_CLK_PORT     GPIOC
#define TP_CLK_PIN      GPIO_PIN_10
#define TP_CS_PORT      GPIOC
#define TP_CS_PIN       GPIO_PIN_11
#define TP_DIN_PORT     GPIOC
#define TP_DIN_PIN      GPIO_PIN_12
#define TP_DOUT_PORT    GPIOD
#define TP_DOUT_PIN     GPIO_PIN_2
#define TP_PEN_PORT     GPIOD
#define TP_PEN_PIN      GPIO_PIN_3

#define TCLK_SET()      HAL_GPIO_WritePin(TP_CLK_PORT,  TP_CLK_PIN,  GPIO_PIN_SET)
#define TCLK_CLR()      HAL_GPIO_WritePin(TP_CLK_PORT,  TP_CLK_PIN,  GPIO_PIN_RESET)
#define TDIN_SET()      HAL_GPIO_WritePin(TP_DIN_PORT,  TP_DIN_PIN,  GPIO_PIN_SET)
#define TDIN_CLR()      HAL_GPIO_WritePin(TP_DIN_PORT,  TP_DIN_PIN,  GPIO_PIN_RESET)
#define TCS_SET()       HAL_GPIO_WritePin(TP_CS_PORT,   TP_CS_PIN,   GPIO_PIN_SET)
#define TCS_CLR()       HAL_GPIO_WritePin(TP_CS_PORT,   TP_CS_PIN,   GPIO_PIN_RESET)
#define PEN_READ()      HAL_GPIO_ReadPin(TP_PEN_PORT, TP_PEN_PIN)
#define DOUT_READ()     HAL_GPIO_ReadPin(TP_DOUT_PORT, TP_DOUT_PIN)

/* 触摸设备结构体 (与厂家驱动保持一致) */
typedef struct
{
    uint8_t (*init)(void);
    uint8_t (*scan)(uint8_t);
    void (*adjust)(void);
    uint16_t x0;
    uint16_t y0;
    uint16_t x;
    uint16_t y;
    uint8_t sta;
    float xfac;
    float yfac;
    short xoff;
    short yoff;
    uint8_t touchtype;
} _m_tp_dev;

extern _m_tp_dev tp_dev;

/* ==================== 调试观察变量 (Keil Debug Watch 用) ==================== */
extern volatile uint16_t dbg_tp_x;          /* 触摸 X 原始 AD 值 */
extern volatile uint16_t dbg_tp_y;          /* 触摸 Y 原始 AD 值 */
extern volatile uint8_t  dbg_tp_pressed;    /* 1=按下 0=释放 */
extern volatile uint16_t dbg_tp_x_raw;      /* 未转换的 X 原始值 */
extern volatile uint16_t dbg_tp_y_raw;      /* 未转换的 Y 原始值 */
extern volatile uint32_t g_cal_seq;         /* 当前校准记录序号 (Flash 持久化, 每次校准+1) */

/* ==================== 函数声明 ==================== */
void TP_Write_Byte(uint8_t num);
uint16_t TP_Read_AD(uint8_t CMD);
uint16_t TP_Read_XOY(uint8_t xy);
uint8_t TP_Read_XY(uint16_t *x, uint16_t *y);
uint8_t TP_Read_XY2(uint16_t *x, uint16_t *y);
void TP_Drow_Touch_Point(uint16_t x, uint16_t y, uint16_t color);
void TP_Draw_Big_Point(uint16_t x, uint16_t y, uint16_t color);
uint8_t TP_Scan(uint8_t tp);
void TP_Save_Adjdata(void);
uint8_t TP_Get_Adjdata(void);
void TP_Adjust(void);
uint8_t TP_Init(void);
void TP_Adj_Info_Show(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1,
                      uint16_t x2, uint16_t y2, uint16_t x3, uint16_t y3, uint16_t fac);
void TP_UpdateDebug(void);   /* 移植新增: 扫描触摸并刷新调试全局变量 */

/* ==================== 触摸使能/失能控制 (触摸模块自带) ==================== */
void    TP_Enable(void);     /* 开启触摸: 首次调用自动执行 TP_Init(), 之后任务开始扫描/画图 */
void    TP_Disable(void);    /* 关闭触摸: 停止扫描/画图 (引脚保持已初始化) */
uint8_t TP_IsEnabled(void);  /* 查询触摸是否已开启: 1=开, 0=关 */
void    TP_MultiPointCalibrate(void);  /* 9 点校准 (3x3 网格 + 最小二乘), 结果写 Flash */

#endif
