#include "board_port.h"
#include "board_config.h"
#include "i2c_bus.h"
#include "spi_bus.h"
#include "uart_port.h"
#include <string.h>

/* HAL端口、时基与STOP恢复：按计划任务实现并经用户烧录验收。 */

#ifdef WATCH_TARGET_STM32

#include "stm32f4xx_hal.h"

/**
 * board_init - P01最小化HAL初始化（STM32目标实现）
 * 初始化顺序：
 *   1. 启用GPIOC时钟（含读回屏障）
 *   2. PC13先置高（LED灭），避免初始低电平毛刺
 *   3. 配置PC13为推挽输出，低速
 *   4. HAL_Init（时钟、SysTick基础）
 *   5. RCC配置：HSI16（16MHz），无PLL，无HSE/LSE
 *   6. 时钟树：SYSCLK/AHB/APB1/APB2各16MHz，FLASH_LATENCY_0
 * 返回：WATCH_OK表示成功，WATCH_IO_ERROR表示任何步骤失败
 */
watch_status_t board_init(void)
{
    HAL_StatusTypeDef hal_status;

    /* 第1步：启用GPIOC时钟（含读回屏障） */
    __HAL_RCC_GPIOC_CLK_ENABLE();
    (void)GPIOC;  /* 读回屏障 */

    /* 第2步：PC13先置高（LED灭，低有效），避免初始低电平毛刺 */
    GPIOC->BSRR = GPIO_PIN_13;

    /* 第3步：配置PC13为推挽输出，低速 */
    GPIOC->MODER = (GPIOC->MODER & ~GPIO_MODER_MODER13) | GPIO_MODER_MODER13_0;
    GPIOC->OTYPER &= ~GPIO_PIN_13;
    GPIOC->OSPEEDR &= ~GPIO_OSPEEDER_OSPEEDR13;
    GPIOC->PUPDR &= ~GPIO_PUPDR_PUPDR13;

    /* 第4步：初始化HAL库 */
    hal_status = HAL_Init();
    if (hal_status != HAL_OK) {
        return WATCH_IO_ERROR;
    }

    /* 第5步：配置RCC - 使用HSI16（内部时钟），不启用HSE/LSE */
    RCC_OscInitTypeDef rcc_osc_init = {0};
    rcc_osc_init.OscillatorType = RCC_OSCILLATORTYPE_HSI;
    rcc_osc_init.HSIState = RCC_HSI_ON;
    rcc_osc_init.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    rcc_osc_init.PLL.PLLState = RCC_PLL_NONE;

    hal_status = HAL_RCC_OscConfig(&rcc_osc_init);
    if (hal_status != HAL_OK) {
        return WATCH_IO_ERROR;
    }

    /* 第6步：配置时钟树 */
    RCC_ClkInitTypeDef rcc_clk_init = {0};
    rcc_clk_init.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                              RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    rcc_clk_init.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
    rcc_clk_init.AHBCLKDivider = RCC_SYSCLK_DIV1;
    rcc_clk_init.APB1CLKDivider = RCC_HCLK_DIV1;
    rcc_clk_init.APB2CLKDivider = RCC_HCLK_DIV1;

    hal_status = HAL_RCC_ClockConfig(&rcc_clk_init, FLASH_LATENCY_0);
    if (hal_status != HAL_OK) {
        return WATCH_IO_ERROR;
    }

    return WATCH_OK;
}

/**
 * board_millis - 获取系统毫秒计数（STM32实现）
 * 使用HAL提供的SysTick计数器
 */
uint32_t board_millis(void)
{
    return HAL_GetTick();
}

/**
 * board_enter_stop - 进入STOP低功耗模式
 * P01：未实现；P02+需要时钟恢复管理
 */
watch_status_t board_enter_stop(void)
{
    return WATCH_NOT_IMPLEMENTED;
}

/**
 * board_i2c_init - 初始化I2C1 GPIO和时钟，调用i2c_bus_init_internal配置HAL
 */
watch_status_t board_i2c_init(void)
{
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_I2C1_CLK_ENABLE();

    GPIO_InitTypeDef gpio_init = {0};
    gpio_init.Pin = WATCH_I2C1_SCL_PIN | WATCH_I2C1_SDA_PIN;
    gpio_init.Mode = GPIO_MODE_AF_OD;
    gpio_init.Pull = GPIO_NOPULL;
    gpio_init.Speed = GPIO_SPEED_FREQ_LOW;
    gpio_init.Alternate = WATCH_I2C1_SCL_AF;
    HAL_GPIO_Init(WATCH_I2C1_SCL_PORT, &gpio_init);

    return i2c_bus_init_internal();
}

/**
 * board_spi_init - 初始化SPI1 GPIO和时钟，调用spi_bus_init_internal配置HAL
 * CS初始化为输出并先置高，防止启动时低电平干扰
 */
watch_status_t board_spi_init(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_SPI1_CLK_ENABLE();

    /* CS先预置高（在配置为输出前） */
    HAL_GPIO_WritePin(WATCH_SPI1_CS_PORT, WATCH_SPI1_CS_PIN, GPIO_PIN_SET);

    /* 配置SCK/MISO/MOSI为AF */
    GPIO_InitTypeDef gpio_init = {0};
    gpio_init.Pin = WATCH_SPI1_SCK_PIN | WATCH_SPI1_MISO_PIN | WATCH_SPI1_MOSI_PIN;
    gpio_init.Mode = GPIO_MODE_AF_PP;
    gpio_init.Pull = GPIO_NOPULL;
    gpio_init.Speed = GPIO_SPEED_FREQ_HIGH;
    gpio_init.Alternate = WATCH_SPI1_SCK_AF;
    HAL_GPIO_Init(WATCH_SPI1_SCK_PORT, &gpio_init);

    /* 配置CS为输出（已先置高） */
    gpio_init.Pin = WATCH_SPI1_CS_PIN;
    gpio_init.Mode = GPIO_MODE_OUTPUT_PP;
    gpio_init.Alternate = 0;
    HAL_GPIO_Init(WATCH_SPI1_CS_PORT, &gpio_init);

    return spi_bus_init_internal();
}

/**
 * board_uart_init - 初始化USART1 GPIO和时钟，调用uart_port_init_internal配置HAL
 */
watch_status_t board_uart_init(uint32_t baudrate)
{
    if (baudrate == 0) {
        return WATCH_INVALID_ARGUMENT;
    }

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_USART1_CLK_ENABLE();

    GPIO_InitTypeDef gpio_init = {0};
    gpio_init.Pin = WATCH_UART1_TX_PIN | WATCH_UART1_RX_PIN;
    gpio_init.Mode = GPIO_MODE_AF_PP;
    gpio_init.Pull = GPIO_PULLUP;
    gpio_init.Speed = GPIO_SPEED_FREQ_HIGH;
    gpio_init.Alternate = WATCH_UART1_TX_AF;
    HAL_GPIO_Init(WATCH_UART1_TX_PORT, &gpio_init);

    return uart_port_init_internal(baudrate);
}

#else  /* !WATCH_TARGET_STM32 - 主机实现 */

watch_status_t board_init(void)
{
    return WATCH_NOT_IMPLEMENTED;
}

uint32_t board_millis(void)
{
    return 0U;
}

watch_status_t board_enter_stop(void)
{
    return WATCH_NOT_IMPLEMENTED;
}

watch_status_t board_i2c_init(void)
{
    return WATCH_NOT_IMPLEMENTED;
}

watch_status_t board_spi_init(void)
{
    return WATCH_NOT_IMPLEMENTED;
}

watch_status_t board_uart_init(uint32_t baudrate)
{
    (void)baudrate;
    return WATCH_NOT_IMPLEMENTED;
}

#endif  /* WATCH_TARGET_STM32 */
