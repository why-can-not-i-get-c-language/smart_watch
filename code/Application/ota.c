#include "ota.h"
#include "board_config.h"
#include <string.h>
/* 分片传输、校验和升级请求：按计划任务实现并经用户烧录验收。 */
watch_status_t ota_begin(uint32_t size, uint32_t crc)
{
    if(size==0U || size>WATCH_APP_SIZE) return WATCH_INVALID_ARGUMENT;
    (void)crc;
    return WATCH_NOT_IMPLEMENTED;
}

watch_status_t ota_write(uint32_t offset, const uint8_t *data, size_t length)
{
    if(data==NULL || length==0U || offset>=WATCH_APP_SIZE || length>WATCH_APP_SIZE-offset) return WATCH_INVALID_ARGUMENT;
    return WATCH_NOT_IMPLEMENTED;
}

watch_status_t ota_finish(void)
{
    return WATCH_NOT_IMPLEMENTED;
}
