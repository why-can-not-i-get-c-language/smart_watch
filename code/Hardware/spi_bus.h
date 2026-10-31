#ifndef WATCH_SPI_BUS_H
#define WATCH_SPI_BUS_H
#include "watch_types.h"
/* Flash SPI端口。未实现接口返回WATCH_NOT_IMPLEMENTED，输出无效。 */
watch_status_t spi_bus_transfer(const uint8_t *tx, uint8_t *rx, size_t length);
#endif
