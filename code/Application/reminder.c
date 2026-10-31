#include "reminder.h"
#include "board_config.h"
#include <string.h>
/* 提醒状态机：按计划任务实现并经用户烧录验收。 */
watch_status_t reminder_set(uint32_t utc_seconds)
{
    (void)utc_seconds;
    return WATCH_NOT_IMPLEMENTED;
}

watch_status_t reminder_update(uint32_t utc_seconds)
{
    (void)utc_seconds;
    return WATCH_NOT_IMPLEMENTED;
}

watch_status_t reminder_cancel(void)
{
    return WATCH_NOT_IMPLEMENTED;
}
