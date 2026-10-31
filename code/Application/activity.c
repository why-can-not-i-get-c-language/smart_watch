#include "activity.h"
#include "board_config.h"
#include <string.h>
/* 低活动时间窗：按计划任务实现并经用户烧录验收。 */
watch_status_t activity_update(uint32_t now_ms, uint32_t steps, bool *remind)
{
    if(remind==NULL) return WATCH_INVALID_ARGUMENT;
    *remind=false;
    (void)now_ms;
    (void)steps;
    return WATCH_NOT_IMPLEMENTED;
}
