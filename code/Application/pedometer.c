#include "pedometer.h"
#include "board_config.h"
#include <string.h>
/* 计步与按日汇总：按计划任务实现并经用户烧录验收。 */
watch_status_t pedometer_update(const motion_sample_t *sample, uint32_t *steps)
{
    if(sample==NULL || steps==NULL) return WATCH_INVALID_ARGUMENT;
    *steps=0U;
    if(!sample->valid) return WATCH_NOT_READY;
    return WATCH_NOT_IMPLEMENTED;
}

watch_status_t pedometer_reset(void)
{
    return WATCH_NOT_IMPLEMENTED;
}
