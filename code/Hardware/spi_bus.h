#ifndef WATCH_SPI_BUS_H
#define WATCH_SPI_BUS_H
#include "watch_types.h"
/* Flash SPI端口。未实现接口返回WATCH_NOT_IMPLEMENTED，输出无效。
 * 单次传输最多256字节（SPI_MAX_TRANSFER_SIZE）。
 */
watch_status_t spi_bus_transfer(const uint8_t *tx, uint8_t *rx, size_t length);

/* 内部初始化（board_port.c调用） */
watch_status_t spi_bus_init_internal(void);

/* 获取上一次操作状态 */
watch_status_t spi_bus_get_status(void);
#endif
