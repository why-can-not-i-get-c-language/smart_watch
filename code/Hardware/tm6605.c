#include "tm6605.h"
#include "board_config.h"
#include <string.h>
/* 统一LRA振动套装驱动：按计划任务实现并经用户烧录验收。 */
watch_status_t tm6605_init(void)
{
    return WATCH_NOT_IMPLEMENTED;
}

watch_status_t tm6605_play(uint8_t effect)
{
    (void)effect;
    return WATCH_NOT_IMPLEMENTED;
}

watch_status_t tm6605_stop(void)
{
    return WATCH_NOT_IMPLEMENTED;
}

watch_status_t tm6605_sleep(bool enabled)
{
    (void)enabled;
    return WATCH_NOT_IMPLEMENTED;
}
