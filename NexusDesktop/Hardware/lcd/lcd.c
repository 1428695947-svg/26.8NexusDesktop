/**
  ******************************************************************************
  * @file    lcd.c
  * @brief   ILI9488 3.5寸 SPI TFT LCD 驱动 (HAL 版) - STM32F407VET6
  * @note    由厂家 Demo (Demo_STM32F407ZGT6_Hardware_SPI/HARDWARE/LCD/lcd.c) 移植。
  *          移植改动:
  *            1. 全部底层 GPIO 操作 -> HAL_GPIO_WritePin (引脚映射见 lcd.h)
  *            2. 全部 SPI 收发 -> HAL (Hardware/spi/lcd_spi.c, 基于 CubeMX hspi1)
  *            3. 全部延时 -> Hardware/delay/delay.c (HAL_Delay + DWT)
  *            4. 类型 u8/u16/u32 -> uint8_t/uint16_t/uint32_t
  *          ILI9488 初始化序列 (LCD_Init 内 0xF7/0xC0/.../0x29) 保持厂家原样, 未做任何修改。
  ******************************************************************************
  */

#include "lcd.h"
#include "lcd_spi.h"
#include "delay.h"
#include <stdlib.h>

/* LCD 设备对象, 默认竖屏 */
_lcd_dev lcddev;
uint16_t POINT_COLOR = 0x0000;
uint16_t BACK_COLOR  = 0xFFFF;
uint16_t DeviceCode;

/**
  * @brief  写 8 位命令
  */
void LCD_WR_REG(uint8_t data)
{
    LCD_CS_CLR();
    LCD_RS_CLR();
    SPI_WriteByte(data);
    LCD_CS_SET();
}

/**
  * @brief  写 8 位数据
  */
void LCD_WR_DATA(uint8_t data)
{
    LCD_CS_CLR();
    LCD_RS_SET();
    SPI_WriteByte(data);
    LCD_CS_SET();
}

/**
  * @brief  写寄存器
  */
void LCD_WriteReg(uint8_t LCD_Reg, uint16_t LCD_RegValue)
{
    LCD_WR_REG(LCD_Reg);
    LCD_WR_DATA(LCD_RegValue);
}

/**
  * @brief  准备写 GRAM
  */
void LCD_WriteRAM_Prepare(void)
{
    LCD_WR_REG(lcddev.wramcmd);
}

/**
  * @brief  写 16 位像素数据 (ILI9488 18bit 模式: R5/G6/B5 -> 每像素 3 字节)
  */
void Lcd_WriteData_16Bit(uint16_t Data)
{
    LCD_WR_DATA((Data >> 8) & 0xF8);  /* RED */
    LCD_WR_DATA((Data >> 3) & 0xFC);  /* GREEN */
    LCD_WR_DATA(Data << 3);           /* BLUE */
}

/**
  * @brief  在指定位置画一个点
  */
void LCD_DrawPoint(uint16_t x, uint16_t y)
{
    LCD_SetCursor(x, y);
    Lcd_WriteData_16Bit(POINT_COLOR);
}

/**
  * @brief  清屏
  */
void LCD_Clear(uint16_t Color)
{
    uint32_t i, m;
    LCD_SetWindows(0, 0, lcddev.width - 1, lcddev.height - 1);
    LCD_CS_CLR();
    LCD_RS_SET();
    for (i = 0; i < lcddev.height; i++)
    {
        for (m = 0; m < lcddev.width; m++)
        {
            Lcd_WriteData_16Bit(Color);
        }
    }
    LCD_CS_SET();
}

/**
  * @brief  LCD 控制引脚 GPIO 初始化 (HAL)
  * @note   PB12=CS, PB13=DC, PB14=RST, PB15=LED 均配置为推挽输出
  */
void LCD_GPIOInit(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOB_CLK_ENABLE();

    GPIO_InitStruct.Pin   = GPIO_PIN_12 | GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15;
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull  = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    /* 默认状态: 片选释放, 复位释放, 背光点亮 */
    LCD_CS_SET();
    LCD_RST_SET();
    LCD_LED_SET();
}

/**
  * @brief  LCD 硬件复位
  */
void LCD_RESET(void)
{
    LCD_RST_CLR();
    delay_ms(100);
    LCD_RST_SET();
    delay_ms(50);
}

/**
  * @brief  LCD 初始化 (ILI9488 初始化序列与厂家驱动完全一致)
  */
void LCD_Init(void)
{
    /* 注意: SPI1 外设由 CubeMX 生成的 MX_SPI1_Init() 初始化 (main 中调用), 速率 10.5MHz */
    LCD_GPIOInit();     /* LCD GPIO 初始化 */
    LCD_RESET();        /* LCD 复位 */

/* ==================== ILI9488 初始化序列 (厂家原样保留) ==================== */
    LCD_WR_REG(0xF7);
    LCD_WR_DATA(0xA9);
    LCD_WR_DATA(0x51);
    LCD_WR_DATA(0x2C);
    LCD_WR_DATA(0x82);
    LCD_WR_REG(0xC0);
    LCD_WR_DATA(0x11);
    LCD_WR_DATA(0x09);
    LCD_WR_REG(0xC1);
    LCD_WR_DATA(0x41);
    LCD_WR_REG(0xC5);
    LCD_WR_DATA(0x00);
    LCD_WR_DATA(0x0A);
    LCD_WR_DATA(0x80);
    LCD_WR_REG(0xB1);
    LCD_WR_DATA(0xB0);
    LCD_WR_DATA(0x11);
    LCD_WR_REG(0xB4);
    LCD_WR_DATA(0x02);
    LCD_WR_REG(0xB6);
    LCD_WR_DATA(0x02);
    LCD_WR_DATA(0x42);
    LCD_WR_REG(0xB7);
    LCD_WR_DATA(0xC6);
    LCD_WR_REG(0xBE);
    LCD_WR_DATA(0x00);
    LCD_WR_DATA(0x04);
    LCD_WR_REG(0xE9);
    LCD_WR_DATA(0x00);
    LCD_WR_REG(0x36);
    LCD_WR_DATA((1 << 3) | (0 << 7) | (1 << 6) | (1 << 5));
    LCD_WR_REG(0x3A);
    LCD_WR_DATA(0x66);
    LCD_WR_REG(0xE0);
    LCD_WR_DATA(0x00);
    LCD_WR_DATA(0x07);
    LCD_WR_DATA(0x10);
    LCD_WR_DATA(0x09);
    LCD_WR_DATA(0x17);
    LCD_WR_DATA(0x0B);
    LCD_WR_DATA(0x41);
    LCD_WR_DATA(0x89);
    LCD_WR_DATA(0x4B);
    LCD_WR_DATA(0x0A);
    LCD_WR_DATA(0x0C);
    LCD_WR_DATA(0x0E);
    LCD_WR_DATA(0x18);
    LCD_WR_DATA(0x1B);
    LCD_WR_DATA(0x0F);
    LCD_WR_REG(0xE1);
    LCD_WR_DATA(0x00);
    LCD_WR_DATA(0x17);
    LCD_WR_DATA(0x1A);
    LCD_WR_DATA(0x04);
    LCD_WR_DATA(0x0E);
    LCD_WR_DATA(0x06);
    LCD_WR_DATA(0x2F);
    LCD_WR_DATA(0x45);
    LCD_WR_DATA(0x43);
    LCD_WR_DATA(0x02);
    LCD_WR_DATA(0x0A);
    LCD_WR_DATA(0x09);
    LCD_WR_DATA(0x32);
    LCD_WR_DATA(0x36);
    LCD_WR_DATA(0x0F);
    LCD_WR_REG(0x11);
    delay_ms(120);
    LCD_WR_REG(0x29);
/* ==================== ILI9488 初始化序列结束 ==================== */

    LCD_direction(USE_HORIZONTAL);  /* 设置显示方向 */
    LCD_LED_SET();                  /* 点亮背光 */
    /* 移植优化: 厂家驱动在初始化末尾做一次全屏白清屏 (约 46 万字节 SPI 写)。
       本工程 main 在 LCD_Init 后立即 LCD_Clear(RED) 验证, 为避免开机多等数秒,
       此处不再冗余清屏。ILI9488 寄存器初始化序列未做任何修改。 */
}

/**
  * @brief  设置显示窗口
  */
void LCD_SetWindows(uint16_t xStar, uint16_t yStar, uint16_t xEnd, uint16_t yEnd)
{
    LCD_WR_REG(lcddev.setxcmd);
    LCD_WR_DATA(xStar >> 8);
    LCD_WR_DATA(0x00FF & xStar);
    LCD_WR_DATA(xEnd >> 8);
    LCD_WR_DATA(0x00FF & xEnd);

    LCD_WR_REG(lcddev.setycmd);
    LCD_WR_DATA(yStar >> 8);
    LCD_WR_DATA(0x00FF & yStar);
    LCD_WR_DATA(yEnd >> 8);
    LCD_WR_DATA(0x00FF & yEnd);

    LCD_WriteRAM_Prepare();
}

/**
  * @brief  设置光标
  */
void LCD_SetCursor(uint16_t Xpos, uint16_t Ypos)
{
    LCD_SetWindows(Xpos, Ypos, Xpos, Ypos);
}

/**
  * @brief  设置显示方向 (0~3)
  */
void LCD_direction(uint8_t direction)
{
    lcddev.setxcmd = 0x2A;
    lcddev.setycmd = 0x2B;
    lcddev.wramcmd = 0x2C;

    switch (direction)
    {
    case 0:
        lcddev.width = LCD_W;
        lcddev.height = LCD_H;
        LCD_WriteReg(0x36, (1 << 3) | (0 << 6) | (0 << 7)); /* BGR=1, MY=0, MX=0, MV=0 */
        break;
    case 1:
        lcddev.width = LCD_H;
        lcddev.height = LCD_W;
        LCD_WriteReg(0x36, (1 << 3) | (0 << 7) | (1 << 6) | (1 << 5)); /* BGR=1, MY=1, MX=0, MV=1 */
        break;
    case 2:
        lcddev.width = LCD_W;
        lcddev.height = LCD_H;
        LCD_WriteReg(0x36, (1 << 3) | (1 << 6) | (1 << 7)); /* BGR=1, MY=0, MX=0, MV=0 */
        break;
    case 3:
        lcddev.width = LCD_H;
        lcddev.height = LCD_W;
        LCD_WriteReg(0x36, (1 << 3) | (1 << 7) | (1 << 5)); /* BGR=1, MY=1, MX=0, MV=1 */
        break;
    default:
        break;
    }
}

/**
  * @brief  读取 LCD 数据 (MISO, 预留功能)
  */
uint16_t LCD_RD_DATA(void)
{
    return 0xFFFF;
}

/**
  * @brief  写 GRAM
  */
void LCD_WriteRAM(uint16_t RGB_Code)
{
    Lcd_WriteData_16Bit(RGB_Code);
}

/**
  * @brief  读 GRAM (预留功能)
  */
uint16_t LCD_ReadRAM(void)
{
    return 0xFFFF;
}

/**
  * @brief  读寄存器 (预留功能)
  */
uint16_t LCD_ReadReg(uint8_t LCD_Reg)
{
    (void)LCD_Reg;
    return 0xFFFF;
}

/**
  * @brief  读点 (预留功能)
  */
uint16_t LCD_ReadPoint(uint16_t x, uint16_t y)
{
    (void)x;
    (void)y;
    return 0xFFFF;
}

/**
  * @brief  BGR -> RGB 转换 (预留功能)
  */
uint16_t LCD_BGR2RGB(uint16_t c)
{
    return c;
}

/**
  * @brief  参数设置 (预留)
  */
void LCD_SetParam(void)
{
}

/**
  * @brief  显示开 (预留: 0x29 已在初始化中执行)
  */
void LCD_DisplayOn(void)
{
    LCD_WR_REG(0x29);
}

/**
  * @brief  显示关 (预留)
  */
void LCD_DisplayOff(void)
{
    LCD_WR_REG(0x28);
}
