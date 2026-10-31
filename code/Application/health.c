#include "health.h"
#include "board_config.h"
#include <string.h>
/* 静止心率、估算血氧与质量：按计划任务实现并经用户烧录验收。 */
watch_status_t health_update(const ppg_sample_t *sample, health_result_t *result)
{
    if(sample==NULL || result==NULL) return WATCH_INVALID_ARGUMENT;
    memset(result,0,sizeof(*result));
    if(!sample->valid) return WATCH_NOT_READY;
    return WATCH_NOT_IMPLEMENTED;
}
