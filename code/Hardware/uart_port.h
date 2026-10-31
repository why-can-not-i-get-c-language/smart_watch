#ifndef WATCH_UART_PORT_H
#define WATCH_UART_PORT_H
#include "watch_types.h"
/* BLE串口收发。未实现接口返回WATCH_NOT_IMPLEMENTED，输出无效。 */
watch_status_t uart_port_send(const uint8_t *data, size_t length);
watch_status_t uart_port_receive(uint8_t *data, size_t capacity, size_t *received);
#endif
