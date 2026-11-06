#include "spi_bus.h"
#include "board_config.h"
#include <string.h>

/* Flash SPI端口：按计划任务实现并经用户烧录验收。 */

#define SPI_MAX_TRANSFER_SIZE 256

#ifdef WATCH_TARGET_STM32

#include "stm32f4xx_hal.h"

/* 全局SPI句柄（本模块拥有） */
static SPI_HandleTypeDef hspi1 = {0};
static volatile uint8_t spi_initialized = 0;
static volatile watch_status_t spi_last_status = WATCH_OK;

/**
 * spi_bus_init_internal - 初始化SPI1句柄（GPIO已由board_spi_init配置）
 * CS初始化为高（board_spi_init负责预置）
 */
watch_status_t spi_bus_init_internal(void)
{
    hspi1.Instance = SPI1;
    hspi1.Init.Mode = SPI_MODE_MASTER;
    hspi1.Init.Direction = SPI_DIRECTION_2LINES;
    hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
    hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
    hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;
    hspi1.Init.NSS = SPI_NSS_SOFT;
    hspi1.Init.BaudRatePrescaler = WATCH_SPI1_PRESCALER;
    hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
    hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
    hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
    hspi1.Init.CRCPolynomial = 10;

    if (HAL_SPI_Init(&hspi1) != HAL_OK) {
        spi_initialized = 0;
        spi_last_status = WATCH_IO_ERROR;
        return WATCH_IO_ERROR;
    }

    spi_initialized = 1;
    spi_last_status = WATCH_OK;
    return WATCH_OK;
}

/**
 * spi_bus_recover - 有限RCC复位+HAL句柄重置
 */
static void spi_bus_recover(void)
{
    /* CS先拉高（确保未低电平锁定） */
    HAL_GPIO_WritePin(WATCH_SPI1_CS_PORT, WATCH_SPI1_CS_PIN, GPIO_PIN_SET);

    /* 执行有限RCC复位 */
    __HAL_RCC_SPI1_FORCE_RESET();
    __HAL_RCC_SPI1_RELEASE_RESET();

    /* 重置HAL句柄状态 */
    memset(&hspi1, 0, sizeof(hspi1));
    hspi1.State = HAL_SPI_STATE_RESET;
    hspi1.Instance = SPI1;

    /* 不改GPIO配置，下次可用 */
}

/**
 * spi_bus_transfer - SPI1单次传输（软件CS管理，最多256字节）
 * tx: 发送缓冲（可为NULL，则发0xFF）
 * rx: 接收缓冲（可为NULL，则丢弃）
 * length: 传输长度（限制256）
 * 失败后CS拉高、有限RCC复位+重置HAL句柄，返回当前错误；下次可恢复
 */
watch_status_t spi_bus_transfer(const uint8_t *tx, uint8_t *rx, size_t length)
{
    if (length == 0U || length > SPI_MAX_TRANSFER_SIZE || (tx == NULL && rx == NULL)) {
        spi_last_status = WATCH_INVALID_ARGUMENT;
        return WATCH_INVALID_ARGUMENT;
    }

    if (!spi_initialized) {
        spi_last_status = WATCH_NOT_READY;
        return WATCH_NOT_READY;
    }

    /* CS拉低 */
    HAL_GPIO_WritePin(WATCH_SPI1_CS_PORT, WATCH_SPI1_CS_PIN, GPIO_PIN_RESET);

    HAL_StatusTypeDef status = HAL_OK;

    if (tx != NULL && rx != NULL) {
        /* 全双工 */
        status = HAL_SPI_TransmitReceive(&hspi1, (uint8_t *)tx, rx, (uint16_t)length, WATCH_SPI1_TIMEOUT_MS);
    } else if (tx != NULL) {
        /* 仅发送 */
        uint8_t dummy[SPI_MAX_TRANSFER_SIZE] = {0};
        status = HAL_SPI_TransmitReceive(&hspi1, (uint8_t *)tx, dummy, (uint16_t)length, WATCH_SPI1_TIMEOUT_MS);
    } else {
        /* 仅接收，发0xFF */
        uint8_t dummy_tx[SPI_MAX_TRANSFER_SIZE];
        memset(dummy_tx, 0xFF, length);
        status = HAL_SPI_TransmitReceive(&hspi1, dummy_tx, rx, (uint16_t)length, WATCH_SPI1_TIMEOUT_MS);
    }

    /* CS拉高，无论成功或失败 */
    HAL_GPIO_WritePin(WATCH_SPI1_CS_PORT, WATCH_SPI1_CS_PIN, GPIO_PIN_SET);

    if (status != HAL_OK) {
        watch_status_t error_status = (status == HAL_TIMEOUT) ? WATCH_TIMEOUT : WATCH_IO_ERROR;

        /* 失败后有限复位 */
        spi_bus_recover();
        if (spi_bus_init_internal() != WATCH_OK) {
            spi_initialized = 0;
        }

        /* 恢复原错误状态 */
        spi_last_status = error_status;
        return error_status;
    }

    spi_last_status = WATCH_OK;
    return WATCH_OK;
}

/**
 * spi_bus_get_status - 获取上一次操作状态
 */
watch_status_t spi_bus_get_status(void)
{
    return spi_last_status;
}

#else  /* !WATCH_TARGET_STM32 - 主机实现 */

watch_status_t spi_bus_transfer(const uint8_t *tx, uint8_t *rx, size_t length)
{
    if (length == 0U || length > SPI_MAX_TRANSFER_SIZE || (tx == NULL && rx == NULL)) {
        return WATCH_INVALID_ARGUMENT;
    }
    if (rx != NULL) {
        memset(rx, 0, length);
    }
    return WATCH_NOT_IMPLEMENTED;
}

watch_status_t spi_bus_get_status(void)
{
    return WATCH_NOT_IMPLEMENTED;
}

#endif  /* WATCH_TARGET_STM32 */
