#include "settings.h"
#include "board_config.h"
#include <string.h>
/* 带版本参数持久化：按计划任务实现并经用户烧录验收。 */
watch_status_t settings_load(void *data, size_t length)
{
    if(data==NULL || length==0U) return WATCH_INVALID_ARGUMENT;
    memset(data,0,length);
    return WATCH_NOT_IMPLEMENTED;
}

watch_status_t settings_save(const void *data, size_t length)
{
    if(data==NULL || length==0U) return WATCH_INVALID_ARGUMENT;
    return WATCH_NOT_IMPLEMENTED;
}
