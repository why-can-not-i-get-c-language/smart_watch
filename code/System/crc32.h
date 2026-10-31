#ifndef WATCH_CRC32_H
#define WATCH_CRC32_H
#include "watch_types.h"
/* CRC-32/ISO-HDLC：初值与末异或均为0xFFFFFFFF。 */
watch_status_t crc32_compute(const uint8_t *data, size_t length, uint32_t *result);
#endif
