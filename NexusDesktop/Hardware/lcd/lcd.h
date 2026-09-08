/**
  ******************************************************************************
  * @file    lcd.h
  * @brief   ILI9488 3.5寸 SPI TFT LCD 驱动 (HAL 版) - STM32F407VET6
  * @note    由厂家 Demo (Demo_STM32F407ZGT6_Hardware_SPI/HARDWARE/LCD) 移植,
  *          底层 GPIO/SPI/延时全部替换为 HAL 实现。
  *
  *          本工程硬件连接 (与厂家 Demo 不同, 已重新映射):
  *            LCD SCK  -> PA5  (SPI1_SCK)
  *            LCD MOSI -> PA7  (SPI1_MOSI)
  *            LCD MISO -> PA6  (SPI1_MISO, 预留未用)
  *            LCD CS   -> PB12
  *            LCD DC   -> PB13
  *            LCD RST  -> PB14
  *            LCD LED  -> PB15 (背光)
  ******************************************************************************
  */
#ifndef __LCD_H
#define __LCD_H

#include "main.h"

/* LCD 尺寸 */
#define LCD_W   320
#define LCD_H   480

/* 显示方向: 0-0度 1-90度 2-180度 3-270度 */
#define USE_HORIZONTAL   0

/* ==================== LCD 控制引脚定义 (PB12~PB15) ==================== */
#define LCD_CS_PORT     GPIOB
#define LCD_CS_PIN      GPIO_PIN_12
#define LCD_RS_PORT     GPIOB
#define LCD_RS_PIN      GPIO_PIN_13
#define LCD_RST_PORT    GPIOB
#define LCD_RST_PIN     GPIO_PIN_14
#define LCD_LED_PORT    GPIOB
#define LCD_LED_PIN     GPIO_PIN_15

/* 控制引脚操作宏 (HAL 库实现) */
#define LCD_CS_SET()    HAL_GPIO_WritePin(LCD_CS_PORT,  LCD_CS_PIN,  GPIO_PIN_SET)
#define LCD_CS_CLR()    HAL_GPIO_WritePin(LCD_CS_PORT,  LCD_CS_PIN,  GPIO_PIN_RESET)
#define LCD_RS_SET()    HAL_GPIO_WritePin(LCD_RS_PORT,  LCD_RS_PIN,  GPIO_PIN_SET)
#define LCD_RS_CLR()    HAL_GPIO_WritePin(LCD_RS_PORT,  LCD_RS_PIN,  GPIO_PIN_RESET)
#define LCD_RST_SET()   HAL_GPIO_WritePin(LCD_RST_PORT, LCD_RST_PIN, GPIO_PIN_SET)
#define LCD_RST_CLR()   HAL_GPIO_WritePin(LCD_RST_PORT, LCD_RST_PIN, GPIO_PIN_RESET)
#define LCD_LED_SET()   HAL_GPIO_WritePin(LCD_LED_PORT, LCD_LED_PIN, GPIO_PIN_SET)
#define LCD_LED_CLR()   HAL_GPIO_WritePin(LCD_LED_PORT, LCD_LED_PIN, GPIO_PIN_RESET)

/* LCD 设备信息结构体 (原厂格式) */
typedef struct
{
    uint16_t width;
    uint16_t height;
    uint16_t id;
    uint8_t  dir;
    uint16_t wramcmd;
    uint16_t setxcmd;
    uint16_t setycmd;
} _lcd_dev;

extern _lcd_dev lcddev;
extern uint16_t POINT_COLOR;    /* 画笔颜色, 默认黑色 */
extern uint16_t BACK_COLOR;     /* 背景颜色, 默认白色 */

/* 常用颜色 */
#define WHITE   0xFFFF
#define BLACK   0x0000
#define BLUE    0x001F
#define BRED    0xF81F
#define GRED    0xFFE0
#define GBLUE   0x07FF
#define RED     0xF800
#define MAGENTA 0xF81F
#define GREEN   0x07E0
#define CYAN    0x7FFF
#define YELLOW  0xFFE0
#define BROWN   0xBC40
#define BRRED   0xFC07
#define GRAY    0x8430

#define DARKBLUE    0x01CF
#define LIGHTBLUE   0x7D7C
#define GRAYBLUE    0x5458
#define LIGHTGREEN  0x841F
#define LIGHTGRAY   0xEF5B
#define LGRAY       0xC618
#define LGRAYBLUE   0xA651
#define LBBLUE      0x2B12

/* ==================== 函数声明 (与厂家驱动保持一致) ==================== */
void LCD_Init(void);
void LCD_DisplayOn(void);
void LCD_DisplayOff(void);
void LCD_DisplaySleepIn(void);
void LCD_DisplaySleepOut(void);
void LCD_Clear(uint16_t Color);
void LCD_SetCursor(uint16_t Xpos, uint16_t Ypos);
void LCD_DrawPoint(uint16_t x, uint16_t y);
uint16_t LCD_ReadPoint(uint16_t x, uint16_t y);
void LCD_DrawLine(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2);
void LCD_DrawRectangle(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2);
void LCD_SetWindows(uint16_t xStar, uint16_t yStar, uint16_t xEnd, uint16_t yEnd);

uint16_t LCD_RD_DATA(void);
void LCD_WriteReg(uint8_t LCD_Reg, uint16_t LCD_RegValue);
void LCD_WR_DATA(uint8_t data);
uint16_t LCD_ReadReg(uint8_t LCD_Reg);
void LCD_WriteRAM_Prepare(void);
void LCD_WriteRAM(uint16_t RGB_Code);
uint16_t LCD_ReadRAM(void);
uint16_t LCD_BGR2RGB(uint16_t c);
void LCD_SetParam(void);
void Lcd_WriteData_16Bit(uint16_t Data);
void LCD_WritePixelsRGB565(const uint16_t *pixels, uint32_t count);
void LCD_direction(uint8_t direction);

#endif
