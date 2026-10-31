#include "wrist_wake.h"
#include "board_config.h"
#include <string.h>
/* 抬腕姿态及冷却：按计划任务实现并经用户烧录验收。 */
watch_status_t wrist_wake_update(const motion_sample_t *sample, bool *wake)
{
    if(sample==NULL || wake==NULL) return WATCH_INVALID_ARGUMENT;
    *wake=false;
    if(!sample->valid) return WATCH_NOT_READY;
    return WATCH_NOT_IMPLEMENTED;
}
