/**
  ******************************************************************************
  * @file    lcd_spi.h
  * @brief   LCD 专用 SPI 字节收发封装 (基于 CubeMX 生成的 hspi1) - STM32F407VET6
  * @note    硬件连接:
  *            LCD SCK  -> PA5  (SPI1_SCK)
  *            LCD MOSI -> PA7  (SPI1_MOSI)
  *            LCD MISO -> PA6  (SPI1_MISO, 预留未用)
  *          SPI1 外设与 GPIO 由 CubeMX 生成的 MX_SPI1_Init() (Core/Src/spi.c) 初始化,
  *          速率已调整为 10.5MHz (预分频 8), 本文件仅提供字节收发封装。
  *          注意: 文件命名为 lcd_spi.h 以避开 CubeMX 生成的 Core/Inc/spi.h。
  ******************************************************************************
  */
#ifndef __LCD_SPI_H
#define __LCD_SPI_H

#include "main.h"

uint8_t SPI_WriteByte(uint8_t Byte);   /* 通过 SPI1 收发一个字节, 返回接收数据 */

#endif
