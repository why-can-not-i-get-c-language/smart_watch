#include "battery_adc.h"
#include "board_config.h"
#include <string.h>
/* 电池电压采样：按计划任务实现并经用户烧录验收。 */
watch_status_t battery_adc_read_mv(uint16_t *millivolts)
{
    if(millivolts==NULL) return WATCH_INVALID_ARGUMENT;
    *millivolts=0U;
    return WATCH_NOT_IMPLEMENTED;
}
