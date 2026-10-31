#include "power_manager.h"
#include "board_config.h"
#include <string.h>
/* 低功耗状态协调：按计划任务实现并经用户烧录验收。 */
watch_status_t power_manager_update(uint32_t now_ms)
{
    (void)now_ms;
    return WATCH_NOT_IMPLEMENTED;
}

watch_status_t power_manager_resume(void)
{
    return WATCH_NOT_IMPLEMENTED;
}
