#include "bootloader.h"
#include "board_config.h"
#include <string.h>
/* 独立Boot目标及镜像恢复：按计划任务实现并经用户烧录验收。 */
watch_status_t bootloader_run(void)
{
    /* 目标链接与镜像校验未完成，不擦写也不跳转。 */ return WATCH_NOT_IMPLEMENTED;
}
