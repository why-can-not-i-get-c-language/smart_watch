#include "w25q64.h"
#include "board_config.h"
#include <string.h>
/* 按实际JEDEC适配SPI Flash：按计划任务实现并经用户烧录验收。 */
watch_status_t w25q64_identify(uint32_t *jedec_id)
{
    if(jedec_id==NULL) return WATCH_INVALID_ARGUMENT;
    *jedec_id=0U;
    return WATCH_NOT_IMPLEMENTED;
}

watch_status_t w25q64_read(uint32_t offset, uint8_t *data, size_t length)
{
    if(data==NULL || length==0U || offset>=WATCH_EXT_FLASH_SIZE || length>WATCH_EXT_FLASH_SIZE-offset) return WATCH_INVALID_ARGUMENT;
    memset(data,0,length);
    return WATCH_NOT_IMPLEMENTED;
}

watch_status_t w25q64_write(uint32_t offset, const uint8_t *data, size_t length)
{
    if(data==NULL || length==0U || offset>=WATCH_EXT_FLASH_SIZE || length>WATCH_EXT_FLASH_SIZE-offset) return WATCH_INVALID_ARGUMENT;
    return WATCH_NOT_IMPLEMENTED;
}

watch_status_t w25q64_erase_sector(uint32_t offset)
{
    if(offset>=WATCH_EXT_FLASH_SIZE || offset%4096U!=0U) return WATCH_INVALID_ARGUMENT;
    return WATCH_NOT_IMPLEMENTED;
}
