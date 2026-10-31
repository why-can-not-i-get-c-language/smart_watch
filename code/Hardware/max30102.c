#include "max30102.h"
#include "board_config.h"
#include <string.h>
/* PPG采样与低功耗：按计划任务实现并经用户烧录验收。 */
watch_status_t max30102_init(void)
{
    return WATCH_NOT_IMPLEMENTED;
}

watch_status_t max30102_read(ppg_sample_t *sample)
{
    if(sample==NULL) return WATCH_INVALID_ARGUMENT;
    memset(sample,0,sizeof(*sample));
    return WATCH_NOT_IMPLEMENTED;
}

watch_status_t max30102_shutdown(bool enabled)
{
    (void)enabled;
    return WATCH_NOT_IMPLEMENTED;
}
