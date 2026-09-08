/**
  ******************************************************************************
  * @file    shared_bus.c
  * @brief   SDIO 与 XPT2046 共享引脚模式仲裁
  * @note    板级接线使 PC10/PC11/PC12/PD2 同时连接 SDIO 和触摸控制器。
  *          上层必须先取得存储互斥量，再把共享引脚交给 SDIO；释放存储
  *          互斥量前恢复触摸模式。状态标志的更新顺序用于阻止抢占期间误扫触摸。
  ******************************************************************************
  */

#include "shared_bus.h"

#include "stm32f4xx_hal.h"

static volatile uint8_t s_sd_selected = 0U;

void SharedBus_SelectSd(void)
{
    GPIO_InitTypeDef gpio = {0};

    /* 先声明 SD 所有权，避免配置过程中被较高优先级触摸任务抢占。 */
    s_sd_selected = 1U;

    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();

    /* 仅使用 1-bit SDIO。PC11 必须保持为高电平触摸片选，否则 XPT2046
       会驱动与 SDIO_CMD 共线的 PD2，导致卡命令无响应。 */
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_11, GPIO_PIN_SET);
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_10, GPIO_PIN_RESET);
    gpio.Pin = GPIO_PIN_10 | GPIO_PIN_11;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    HAL_GPIO_Init(GPIOC, &gpio);

    gpio.Pin = GPIO_PIN_8;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF12_SDIO;
    HAL_GPIO_Init(GPIOC, &gpio);

    gpio.Pin = GPIO_PIN_12;
    gpio.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOC, &gpio);

    gpio.Pin = GPIO_PIN_2;
    gpio.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOD, &gpio);
}

void SharedBus_SelectTouch(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();

    /* 先准备输出锁存值，再切换模式，避免触摸控制器出现伪时钟或伪片选。 */
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_11 | GPIO_PIN_12, GPIO_PIN_SET);
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_10, GPIO_PIN_RESET);

    gpio.Pin = GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_12;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    HAL_GPIO_Init(GPIOC, &gpio);

    gpio.Pin = GPIO_PIN_2;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOD, &gpio);

    /* 引脚完全恢复后才允许触摸任务继续访问。 */
    s_sd_selected = 0U;
}

uint8_t SharedBus_IsSdSelected(void)
{
    return s_sd_selected;
}
