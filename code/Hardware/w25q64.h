#ifndef WATCH_W25Q64_H
#define WATCH_W25Q64_H
#include "watch_types.h"
/* 按实际JEDEC适配SPI Flash。未实现接口返回WATCH_NOT_IMPLEMENTED，输出无效。 */
watch_status_t w25q64_identify(uint32_t *jedec_id);
watch_status_t w25q64_read(uint32_t offset, uint8_t *data, size_t length);
watch_status_t w25q64_write(uint32_t offset, const uint8_t *data, size_t length);
watch_status_t w25q64_erase_sector(uint32_t offset);
#endif
