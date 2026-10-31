#include "board_port.h"
#include "board_config.h"
#include <string.h>
/* HAL端口、时基与STOP恢复：按计划任务实现并经用户烧录验收。 */
watch_status_t board_init(void)
{
    return WATCH_NOT_IMPLEMENTED;
}

uint32_t board_millis(void)
{
    /* 尚无目标时基，返回值不能用于实物调度。 */ return 0U;
}

watch_status_t board_enter_stop(void)
{
    return WATCH_NOT_IMPLEMENTED;
}
