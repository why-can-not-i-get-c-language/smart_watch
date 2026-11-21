#ifndef WATCH_SPI_BUS_H
#define WATCH_SPI_BUS_H
#include "watch_types.h"
#define SPI_BUS_MAX_TRANSFER_SIZE 256U
typedef struct { const uint8_t *tx; uint8_t *rx; size_t length; } spi_bus_segment_t;
/* 最多两段，每段1..256字节；同一次CS低期间连续传输，失败总会释放CS。 */
watch_status_t spi_bus_transaction(const spi_bus_segment_t *segments, size_t count);
/* Flash SPI端口。未实现接口返回WATCH_NOT_IMPLEMENTED，输出无效。
 * 单次传输最多256字节（SPI_MAX_TRANSFER_SIZE）。
 */
watch_status_t spi_bus_transfer(const uint8_t *tx, uint8_t *rx, size_t length);

/* 内部初始化（board_port.c调用） */
watch_status_t spi_bus_init_internal(void);

/* 获取上一次操作状态 */
watch_status_t spi_bus_get_status(void);
#endif
