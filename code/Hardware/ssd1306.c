#include "ssd1306.h"
#include "board_config.h"
#include <string.h>
/* 屏幕初始化、帧缓冲提交与开关：按计划任务实现并经用户烧录验收。 */
watch_status_t ssd1306_init(void)
{
    return WATCH_NOT_IMPLEMENTED;
}

watch_status_t ssd1306_flush(const uint8_t *frame, size_t length)
{
    if(frame==NULL || length!=1024U) return WATCH_INVALID_ARGUMENT;
    return WATCH_NOT_IMPLEMENTED;
}

watch_status_t ssd1306_set_enabled(bool enabled)
{
    (void)enabled;
    return WATCH_NOT_IMPLEMENTED;
}
