#include "battery_service.h"
#include "board_config.h"
#include <string.h>
/* 估算电量和滞回：按计划任务实现并经用户烧录验收。 */
watch_status_t battery_service_update(uint16_t millivolts, battery_state_t *state)
{
    if(state==NULL) return WATCH_INVALID_ARGUMENT;
    memset(state,0,sizeof(*state));
    (void)millivolts;
    return WATCH_NOT_IMPLEMENTED;
}
