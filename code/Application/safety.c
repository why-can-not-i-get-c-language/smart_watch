#include "safety.h"
#include "board_config.h"
#include <string.h>
/* SOS与跌倒人工确认：按计划任务实现并经用户烧录验收。 */
watch_status_t safety_handle_buttons(uint32_t events, uint32_t now_ms)
{
    (void)events;
    (void)now_ms;
    return WATCH_NOT_IMPLEMENTED;
}

watch_status_t safety_update_motion(const motion_sample_t *sample)
{
    if(sample==NULL) return WATCH_INVALID_ARGUMENT;
    if(!sample->valid) return WATCH_NOT_READY;
    return WATCH_NOT_IMPLEMENTED;
}

watch_status_t safety_cancel(void)
{
    return WATCH_NOT_IMPLEMENTED;
}
