/**
 * STM32F411 中断向量表处理 - P01最小化
 * 实现关键中断处理：
 *   - SysTick_Handler：调用HAL_IncTick更新系统时基
 *   - 异常处理（HardFault/NMI/MemManage/BusFault/UsageFault）：PC13故障闪烁
 */

#include "stm32f4xx_hal.h"
#include <stdint.h>

/**
 * SysTick_Handler - 系统刻度中断
 * 由启动代码/HAL自动调用，每1ms触发一次
 * 更新HAL内部计时器
 */
void SysTick_Handler(void)
{
    HAL_IncTick();
}

/**
 * 故障闪烁循环 - 所有异常共用
 * 初始化PC13（可能在board_init前触发异常时未初始化）
 * 进入连续闪烁循环示故障
 */
static void fault_loop(void)
{
    /* 启用GPIOC时钟 */
    __HAL_RCC_GPIOC_CLK_ENABLE();

    /* PC13置高（LED灭，低有效） */
    GPIOC->BSRR = GPIO_PIN_13;

    /* 配置PC13为推挽输出，低速 */
    GPIOC->MODER = (GPIOC->MODER & ~GPIO_MODER_MODER13) | GPIO_MODER_MODER13_0;
    GPIOC->OTYPER &= ~GPIO_PIN_13;
    GPIOC->OSPEEDR &= ~GPIO_OSPEEDER_OSPEEDR13;
    GPIOC->PUPDR &= ~GPIO_PUPDR_PUPDR13;

    /* 连续故障闪烁 */
    for (;;) {
        /* LED亮 */
        GPIOC->BSRR = (GPIO_PIN_13 << 16);

        /* 软件延迟，数十至数百ms */
        volatile uint32_t delay = 300000;
        while (delay--) {
            __NOP();
        }

        /* LED灭 */
        GPIOC->BSRR = GPIO_PIN_13;

        /* 软件延迟 */
        delay = 300000;
        while (delay--) {
            __NOP();
        }
    }
}

/**
 * HardFault_Handler - 硬故障异常处理
 */
void HardFault_Handler(void)
{
    fault_loop();
}

/**
 * NMI_Handler - 非可屏蔽中断
 */
void NMI_Handler(void)
{
    fault_loop();
}

/**
 * MemManage_Handler - 内存管理故障
 */
void MemManage_Handler(void)
{
    fault_loop();
}

/**
 * BusFault_Handler - 总线故障
 */
void BusFault_Handler(void)
{
    fault_loop();
}

/**
 * UsageFault_Handler - 使用法故障
 */
void UsageFault_Handler(void)
{
    fault_loop();
}

/**
 * USART1_IRQHandler - USART1中断处理
 * 处理接收、错误中断
 */
extern void uart_port_rx_callback(void);

void USART1_IRQHandler(void)
{
    uart_port_rx_callback();
}
