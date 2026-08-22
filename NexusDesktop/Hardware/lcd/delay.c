/**
  ******************************************************************************
  * @file    delay.c
  * @brief   简易延时模块实现
  * @note    移植自厂家 Demo (SYSTEM/delay)。
  *          原版基于 SysTick 专用延时, 本工程 HAL 时间基准已由 CubeMX 设为 TIM4,
  *          因此 delay_ms 直接复用 HAL_Delay; delay_us 使用 DWT 周期计数器忙等,
  *          与 FreeRTOS / TIM 中断互不冲突。
  ******************************************************************************
  */

#include "delay.h"

/**
  * @brief  初始化 DWT 周期计数器 (供 delay_us 使用)
  * @note   SystemClock 为 168MHz, 1 个 DWT 计数 = 1/168 us
  */
void delay_init(void)
{
    /* 使能 DWT 访问 (需要 TRCENA) */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

/**
  * @brief  微秒级忙等延时
  * @param  nus: 延时微秒数
  * @note   168MHz 主频下每个周期约 5.95ns; 采用 DWT 计数避免占用 SysTick/TIM
  */
void delay_us(uint32_t nus)
{
    uint32_t start = DWT->CYCCNT;
    uint32_t ticks = nus * (SystemCoreClock / 1000000U);

    while ((DWT->CYCCNT - start) < ticks)
    {
    }
}

/**
  * @brief  毫秒级延时, 复用 HAL 时间基准 (TIM4 -> HAL_GetTick)
  */
void delay_ms(uint16_t nms)
{
    HAL_Delay(nms);
}
