#ifndef WATCH_UART_PORT_H
#define WATCH_UART_PORT_H
#include "watch_types.h"
/* BLE串口收发。未实现接口返回WATCH_NOT_IMPLEMENTED，输出无效。 */
watch_status_t uart_port_send(const uint8_t *data, size_t length);
watch_status_t uart_port_receive(uint8_t *data, size_t capacity, size_t *received);

/* 获取上一次操作状态 */
watch_status_t uart_port_get_status(void);

/* 获取错误计数 */
uint32_t uart_port_get_error_count(void);

/* 内部初始化（board_uart_init后调用） */
watch_status_t uart_port_init_internal(uint32_t baudrate);

/* ISR回调（由USART1_IRQHandler调用） */
void uart_port_rx_callback(void);
#endif
