#include "ble_protocol.h"
#include "board_config.h"
#include <string.h>
/* 有界帧编解码与CRC：按计划任务实现并经用户烧录验收。 */
watch_status_t ble_protocol_feed(const uint8_t *data, size_t length)
{
    if(data==NULL || length==0U) return WATCH_INVALID_ARGUMENT;
    return WATCH_NOT_IMPLEMENTED;
}

watch_status_t ble_protocol_encode(uint8_t type, uint16_t sequence, const uint8_t *payload, size_t length, uint8_t *out, size_t capacity, size_t *written)
{
    if(out==NULL || written==NULL || capacity==0U || (payload==NULL && length!=0U)) return WATCH_INVALID_ARGUMENT;
    *written=0U;
    (void)type;
    (void)sequence;
    return WATCH_NOT_IMPLEMENTED;
}
