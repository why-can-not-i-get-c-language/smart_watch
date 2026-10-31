#ifndef WATCH_OTA_H
#define WATCH_OTA_H
#include "watch_types.h"
/* 分片传输、校验和升级请求。未实现接口返回WATCH_NOT_IMPLEMENTED，输出无效。 */
watch_status_t ota_begin(uint32_t size, uint32_t crc);
watch_status_t ota_write(uint32_t offset, const uint8_t *data, size_t length);
watch_status_t ota_finish(void);
#endif
