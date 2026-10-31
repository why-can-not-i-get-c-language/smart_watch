#ifndef WATCH_BLE_PROTOCOL_H
#define WATCH_BLE_PROTOCOL_H
#include "watch_types.h"
/* 有界帧编解码与CRC。未实现接口返回WATCH_NOT_IMPLEMENTED，输出无效。 */
watch_status_t ble_protocol_feed(const uint8_t *data, size_t length);
watch_status_t ble_protocol_encode(uint8_t type, uint16_t sequence, const uint8_t *payload, size_t length, uint8_t *out, size_t capacity, size_t *written);
#endif
