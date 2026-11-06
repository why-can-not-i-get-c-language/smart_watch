#include "uart_port.h"
#include "board_config.h"
#include <string.h>

/* BLE串口收发：按计划任务实现并经用户烧录验收。 */

#ifdef WATCH_TARGET_STM32

#include "stm32f4xx_hal.h"

/* 全局UART句柄（本模块拥有） */
static UART_HandleTypeDef huart1 = {0};
static volatile uint8_t uart_initialized = 0;
static volatile watch_status_t uart_last_status = WATCH_OK;

/* 环形接收缓冲 */
static uint8_t uart_rx_buffer[WATCH_UART1_RX_BUFFER_SIZE];
static volatile uint16_t uart_rx_head = 0;  /* 写指针（ISR写） */
static volatile uint16_t uart_rx_tail = 0;  /* 读指针（应用读） */
static volatile uint8_t uart_rx_error = 0;  /* 错误标志 */
static volatile uint32_t uart_rx_error_count = 0;

/**
 * uart_port_init_internal - 初始化UART1句柄并启用中断（GPIO已由board_uart_init配置）
 */
watch_status_t uart_port_init_internal(uint32_t baudrate)
{
    if (baudrate == 0) {
        return WATCH_INVALID_ARGUMENT;
    }

    /* 先禁用NVIC中断 */
    HAL_NVIC_DisableIRQ(USART1_IRQn);

    /* 清SR、DR */
    (void)USART1->SR;
    (void)USART1->DR;

    /* 复位ring和标志 */
    uart_rx_head = 0;
    uart_rx_tail = 0;
    uart_rx_error = 0;
    uart_rx_error_count = 0;
    uart_last_status = WATCH_OK;

    huart1.Instance = USART1;
    huart1.Init.BaudRate = baudrate;
    huart1.Init.WordLength = UART_WORDLENGTH_8B;
    huart1.Init.StopBits = UART_STOPBITS_1;
    huart1.Init.Parity = UART_PARITY_NONE;
    huart1.Init.Mode = UART_MODE_TX_RX;
    huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart1.Init.OverSampling = UART_OVERSAMPLING_16;

    if (HAL_UART_Init(&huart1) != HAL_OK) {
        uart_initialized = 0;
        return WATCH_IO_ERROR;
    }

    /* 分别启用RXNE和Error中断 */
    __HAL_UART_ENABLE_IT(&huart1, UART_IT_RXNE);
    __HAL_UART_ENABLE_IT(&huart1, UART_IT_ERR);

    /* 设置initialized后最后启用NVIC */
    uart_initialized = 1;
    HAL_NVIC_SetPriority(USART1_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(USART1_IRQn);

    return WATCH_OK;
}

/**
 * uart_port_send - 发送数据（有限超时）
 */
watch_status_t uart_port_send(const uint8_t *data, size_t length)
{
    if (data == NULL || length == 0U || length > UINT16_MAX) {
        return WATCH_INVALID_ARGUMENT;
    }

    if (!uart_initialized) {
        return WATCH_NOT_READY;
    }

    HAL_StatusTypeDef status = HAL_UART_Transmit(&huart1, (uint8_t *)data, (uint16_t)length, WATCH_UART1_TIMEOUT_MS);
    if (status == HAL_TIMEOUT) {
        uart_last_status = WATCH_TIMEOUT;
        return WATCH_TIMEOUT;
    } else if (status != HAL_OK) {
        uart_last_status = WATCH_IO_ERROR;
        return WATCH_IO_ERROR;
    }

    uart_last_status = WATCH_OK;
    return WATCH_OK;
}

/**
 * uart_port_receive - 非阻塞接收
 * 返回缓冲中的可用字节数
 */
watch_status_t uart_port_receive(uint8_t *data, size_t capacity, size_t *received)
{
    if (data == NULL || capacity == 0U || received == NULL) {
        return WATCH_INVALID_ARGUMENT;
    }

    *received = 0;  /* 入口先置0 */

    if (!uart_initialized) {
        return WATCH_NOT_READY;
    }

    /* 检查错误标志（PRIMASK临界区保护） */
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    uint8_t has_error = uart_rx_error;
    uart_rx_error = 0;
    __set_PRIMASK(primask);

    if (has_error) {
        uart_last_status = WATCH_IO_ERROR;
        return WATCH_IO_ERROR;
    }

    /* 原子读取指针 */
    primask = __get_PRIMASK();
    __disable_irq();
    uint16_t head = uart_rx_head;
    uint16_t tail = uart_rx_tail;
    __set_PRIMASK(primask);

    /* 计算可用数据 */
    uint16_t available;
    if (head >= tail) {
        available = head - tail;
    } else {
        available = WATCH_UART1_RX_BUFFER_SIZE - tail + head;
    }

    size_t to_read = (available < capacity) ? available : capacity;

    if (to_read == 0) {
        return WATCH_OK;
    }

    /* 复制数据 */
    if (tail + to_read <= WATCH_UART1_RX_BUFFER_SIZE) {
        memcpy(data, &uart_rx_buffer[tail], to_read);
        uart_rx_tail = (tail + to_read) % WATCH_UART1_RX_BUFFER_SIZE;
    } else {
        size_t first_part = WATCH_UART1_RX_BUFFER_SIZE - tail;
        memcpy(data, &uart_rx_buffer[tail], first_part);
        memcpy(&data[first_part], uart_rx_buffer, to_read - first_part);
        uart_rx_tail = to_read - first_part;
    }

    *received = to_read;
    uart_last_status = WATCH_OK;
    return WATCH_OK;
}

/**
 * uart_port_get_status - 获取上一次操作状态
 */
watch_status_t uart_port_get_status(void)
{
    return uart_last_status;
}

/**
 * uart_port_get_error_count - 获取错误计数
 */
uint32_t uart_port_get_error_count(void)
{
    return uart_rx_error_count;
}

/**
 * uart_port_rx_callback - ISR回调（USART1_IRQHandler中调用）
 * SR快照，RXNE或error仅一次读DR，错误帧丢弃并计数
 */
void uart_port_rx_callback(void)
{
    if (!uart_initialized) {
        return;
    }

    /* SR快照 */
    uint32_t sr = USART1->SR;

    if (sr & (UART_FLAG_ORE | UART_FLAG_FE | UART_FLAG_NE | UART_FLAG_PE)) {
        /* 错误帧：读DR丢弃，设错误标志 */
        (void)USART1->DR;
        uart_rx_error = 1;
        uart_rx_error_count++;
    } else if (sr & UART_FLAG_RXNE) {
        /* 正常数据：读一次DR入缓冲 */
        uint8_t byte = USART1->DR;

        uint16_t new_head = (uart_rx_head + 1) % WATCH_UART1_RX_BUFFER_SIZE;
        if (new_head != uart_rx_tail) {
            uart_rx_buffer[uart_rx_head] = byte;
            uart_rx_head = new_head;
        } else {
            /* 缓冲满，溢出标记 */
            uart_rx_error = 1;
            uart_rx_error_count++;
        }
    }
}

#else  /* !WATCH_TARGET_STM32 - 主机实现 */

watch_status_t uart_port_send(const uint8_t *data, size_t length)
{
    if (data == NULL || length == 0U || length > UINT16_MAX) {
        return WATCH_INVALID_ARGUMENT;
    }
    return WATCH_NOT_IMPLEMENTED;
}

watch_status_t uart_port_receive(uint8_t *data, size_t capacity, size_t *received)
{
    if (data == NULL || capacity == 0U || received == NULL) {
        return WATCH_INVALID_ARGUMENT;
    }
    *received = 0U;
    return WATCH_NOT_IMPLEMENTED;
}

watch_status_t uart_port_get_status(void)
{
    return WATCH_NOT_IMPLEMENTED;
}

uint32_t uart_port_get_error_count(void)
{
    return 0;
}

void uart_port_rx_callback(void)
{
}

#endif  /* WATCH_TARGET_STM32 */
