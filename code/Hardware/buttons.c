#include "buttons.h"
#include "board_config.h"
#include <string.h>
/* 按键消抖和长短按：按计划任务实现并经用户烧录验收。 */
watch_status_t buttons_poll(uint32_t now_ms, uint32_t *events)
{
    if(events==NULL) return WATCH_INVALID_ARGUMENT;
    *events=0U;
    (void)now_ms;
    return WATCH_NOT_IMPLEMENTED;
}
