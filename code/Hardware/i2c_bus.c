#include "i2c_bus.h"
#include "board_config.h"
#include <string.h>

/* 7位地址总线与超时：按计划任务实现并经用户烧录验收。 */

#ifdef WATCH_TARGET_STM32

#include "stm32f4xx_hal.h"

/* 全局I2C句柄（本模块拥有） */
static I2C_HandleTypeDef hi2c1 = {0};
static volatile uint8_t i2c_initialized = 0;
static volatile watch_status_t i2c_last_status = WATCH_OK;

/**
 * i2c_bus_init_internal - 初始化I2C1句柄（GPIO已由board_i2c_init配置）
 * 仅在STM32编译时使用
 */
watch_status_t i2c_bus_init_internal(void)
{
    hi2c1.Instance = I2C1;
    hi2c1.Init.ClockSpeed = WATCH_I2C1_SPEED_HZ;
    hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
    hi2c1.Init.OwnAddress1 = 0;
    hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    hi2c1.Init.OwnAddress2 = 0;
    hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;

    if (HAL_I2C_Init(&hi2c1) != HAL_OK) {
        i2c_initialized = 0;
        i2c_last_status = WATCH_NOT_READY;
        return WATCH_NOT_READY;
    }

    i2c_initialized = 1;
    i2c_last_status = WATCH_OK;
    return WATCH_OK;
}

/**
 * i2c_bus_recover - 有限RCC复位+HAL句柄重置，GPIO保留
 */
static void i2c_bus_recover(void)
{
    /* 执行有限RCC复位 */
    __HAL_RCC_I2C1_FORCE_RESET();
    __HAL_RCC_I2C1_RELEASE_RESET();

    /* 重置HAL句柄状态 */
    memset(&hi2c1, 0, sizeof(hi2c1));
    hi2c1.State = HAL_I2C_STATE_RESET;
    hi2c1.Instance = I2C1;

    /* 不改GPIO配置，下次可用 */
}

/**
 * i2c_bus_read - 读取寄存器值（8bit地址，端口做左移）
 * 失败后执行有限RCC复位+重置HAL句柄+init恢复，返回当前错误
 */
watch_status_t i2c_bus_read(uint8_t address7, uint8_t reg, uint8_t *data, size_t length)
{
    if (address7 > 0x7FU || data == NULL || length == 0U || length > UINT16_MAX) {
        i2c_last_status = WATCH_INVALID_ARGUMENT;
        return WATCH_INVALID_ARGUMENT;
    }

    if (!i2c_initialized) {
        i2c_last_status = WATCH_NOT_READY;
        return WATCH_NOT_READY;
    }

    /* address已在本端口左移，不在HAL做 */
    HAL_StatusTypeDef status = HAL_I2C_Mem_Read(&hi2c1, (address7 << 1), reg, I2C_MEMADD_SIZE_8BIT,
                                                  data, (uint16_t)length, WATCH_I2C1_TIMEOUT_MS);
    if (status != HAL_OK) {
        watch_status_t error_status = (status == HAL_TIMEOUT) ? WATCH_TIMEOUT : WATCH_IO_ERROR;

        /* 失败后有限复位 */
        i2c_bus_recover();
        if (i2c_bus_init_internal() != WATCH_OK) {
            i2c_initialized = 0;
        }

        /* 恢复原错误状态 */
        i2c_last_status = error_status;
        return error_status;
    }

    i2c_last_status = WATCH_OK;
    return WATCH_OK;
}

/**
 * i2c_bus_write - 写入寄存器值（8bit地址，端口做左移）
 * 失败后执行有限RCC复位+重置HAL句柄+init恢复，返回当前错误
 */
watch_status_t i2c_bus_write(uint8_t address7, uint8_t reg, const uint8_t *data, size_t length)
{
    if (address7 > 0x7FU || data == NULL || length == 0U || length > UINT16_MAX) {
        i2c_last_status = WATCH_INVALID_ARGUMENT;
        return WATCH_INVALID_ARGUMENT;
    }

    if (!i2c_initialized) {
        i2c_last_status = WATCH_NOT_READY;
        return WATCH_NOT_READY;
    }

    /* address已在本端口左移，不在HAL做 */
    HAL_StatusTypeDef status = HAL_I2C_Mem_Write(&hi2c1, (address7 << 1), reg, I2C_MEMADD_SIZE_8BIT,
                                                   (uint8_t *)data, (uint16_t)length, WATCH_I2C1_TIMEOUT_MS);
    if (status != HAL_OK) {
        watch_status_t error_status = (status == HAL_TIMEOUT) ? WATCH_TIMEOUT : WATCH_IO_ERROR;

        /* 失败后有限复位 */
        i2c_bus_recover();
        if (i2c_bus_init_internal() != WATCH_OK) {
            i2c_initialized = 0;
        }

        /* 恢复原错误状态 */
        i2c_last_status = error_status;
        return error_status;
    }

    i2c_last_status = WATCH_OK;
    return WATCH_OK;
}

/**
 * i2c_bus_probe - 探测I2C设备应答（诊断用，单次10ms超时）
 * 失败后执行有限RCC复位+重置HAL句柄+init恢复，返回当前错误
 */
watch_status_t i2c_bus_probe(uint8_t address7)
{
    if (address7 > 0x7FU) {
        i2c_last_status = WATCH_INVALID_ARGUMENT;
        return WATCH_INVALID_ARGUMENT;
    }

    if (!i2c_initialized) {
        i2c_last_status = WATCH_NOT_READY;
        return WATCH_NOT_READY;
    }

    /* probe单次，10ms超时 */
    HAL_StatusTypeDef status = HAL_I2C_IsDeviceReady(&hi2c1, (address7 << 1), WATCH_I2C1_PROBE_TRIALS, WATCH_I2C1_TIMEOUT_MS);
    if (status != HAL_OK) {
        watch_status_t error_status = (status == HAL_TIMEOUT) ? WATCH_TIMEOUT : WATCH_IO_ERROR;

        /* 失败后有限复位 */
        i2c_bus_recover();
        if (i2c_bus_init_internal() != WATCH_OK) {
            i2c_initialized = 0;
        }

        /* 恢复原错误状态 */
        i2c_last_status = error_status;
        return error_status;
    }

    i2c_last_status = WATCH_OK;
    return WATCH_OK;
}

/**
 * i2c_bus_get_status - 获取上一次操作状态
 */
watch_status_t i2c_bus_get_status(void)
{
    return i2c_last_status;
}

#else  /* !WATCH_TARGET_STM32 - 主机实现 */

watch_status_t i2c_bus_read(uint8_t address7, uint8_t reg, uint8_t *data, size_t length)
{
    if (address7 > 0x7FU || data == NULL || length == 0U || length > UINT16_MAX) {
        return WATCH_INVALID_ARGUMENT;
    }
    memset(data, 0, length);
    return WATCH_NOT_IMPLEMENTED;
}

watch_status_t i2c_bus_write(uint8_t address7, uint8_t reg, const uint8_t *data, size_t length)
{
    if (address7 > 0x7FU || data == NULL || length == 0U || length > UINT16_MAX) {
        return WATCH_INVALID_ARGUMENT;
    }
    (void)reg;
    return WATCH_NOT_IMPLEMENTED;
}

watch_status_t i2c_bus_probe(uint8_t address7)
{
    if (address7 > 0x7FU) {
        return WATCH_INVALID_ARGUMENT;
    }
    return WATCH_NOT_IMPLEMENTED;
}

watch_status_t i2c_bus_get_status(void)
{
    return WATCH_NOT_IMPLEMENTED;
}

#endif  /* WATCH_TARGET_STM32 */
