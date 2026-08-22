/**
  ******************************************************************************
  * @file    lcd_spi.c
  * @brief   LCD 专用 SPI 字节收发封装 (HAL 版) - STM32F407VET6
  * @note    由厂家 Demo 的 HARDWARE/SPI/SPI.c 移植:
  *          - SPI1 外设初始化改用 CubeMX 生成的 MX_SPI1_Init() (main 中已调用)
  *          - 字节收发使用 HAL_SPI_TransmitReceive 完成全双工单字节通信
  *          - 速率 84MHz/8 = 10.5MHz, 优先保证稳定
  ******************************************************************************
  */

#include "spi.h"        /* CubeMX 生成: extern SPI_HandleTypeDef hspi1; MX_SPI1_Init() */
#include "lcd_spi.h"

/* SPE 使能标记: HAL_SPI_Init 不会置位 SPE (原 HAL_SPI_TransmitReceive 每次调用前会自行
   使能); 本驱动改为寄存器收发, 故在首次使用前显式使能 SPI 外设一次 */
static uint8_t s_spi_spe_enabled = 0;

/**
  * @brief  SPI1 收发一个字节 (寄存器级轮询, 返回总线收到的数据)
  * @param  Byte: 待发送数据
  * @retval 接收到的数据; 通信异常/超时返回 0xFF 便于调试观察
  * @note   移植说明: 原实现调用 HAL_SPI_TransmitReceive 逐字节收发, 实测每字节开销
  *         ~25us (全屏 320x480x3 = 46 万字节, 一次清屏需要 10s+), 且调试器在传输
  *         中途暂停会把 SPI 状态机卡死 (HAL_MAX_DELAY 永不超时)。
  *         因此字节收发改用寄存器级轮询 (与厂家 Demo 一致), 并加超时保护:
  *         - 等待 TXE 置位 -> 写 DR
  *         - 等待 RXNE 置位 -> 读 DR (全双工主模式下每发 1 字节必收 1 字节)
  *         - 超时(约 6ms)则返回 0xFF, 避免永久挂死
  *         SPI1 外设本身仍由 CubeMX 生成的 MX_SPI1_Init() (HAL) 初始化。
  */
uint8_t SPI_WriteByte(uint8_t Byte)
{
    uint32_t guard;

    /* 首次使用: 使能 SPI 外设 (SPE=1), 否则 TXE/RXNE 永不置位 */
    if (s_spi_spe_enabled == 0)
    {
        SET_BIT(hspi1.Instance->CR1, SPI_CR1_SPE);
        s_spi_spe_enabled = 1;
    }

    /* 等待发送缓冲空 (TXE=1) */
    guard = 0xFFFFFU;
    while ((hspi1.Instance->SR & SPI_SR_TXE) == 0U)
    {
        if (--guard == 0U)
        {
            return 0xFF;
        }
    }

    /* 写入数据寄存器, 启动传输 */
    hspi1.Instance->DR = Byte;

    /* 等待接收缓冲非空 (RXNE=1), 表示本次字节传输完成 */
    guard = 0xFFFFFU;
    while ((hspi1.Instance->SR & SPI_SR_RXNE) == 0U)
    {
        if (--guard == 0U)
        {
            return 0xFF;
        }
    }

    /* 读取接收数据 (同时清除 RXNE) */
    return (uint8_t)(hspi1.Instance->DR);
}
