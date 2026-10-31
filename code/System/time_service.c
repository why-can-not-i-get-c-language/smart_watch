#include "time_service.h"
#include "board_config.h"
#include <string.h>
/* UTC时间与有效性：按计划任务实现并经用户烧录验收。 */
watch_status_t time_service_get(uint32_t *utc_seconds)
{
    if(utc_seconds==NULL) return WATCH_INVALID_ARGUMENT;
    *utc_seconds=0U;
    return WATCH_NOT_IMPLEMENTED;
}

watch_status_t time_service_set(uint32_t utc_seconds)
{
    (void)utc_seconds;
    return WATCH_NOT_IMPLEMENTED;
}
