#include "i2c_bus.h"
#include "board_config.h"
#include <string.h>
/* 7位地址总线与超时：按计划任务实现并经用户烧录验收。 */
watch_status_t i2c_bus_read(uint8_t address7, uint8_t reg, uint8_t *data, size_t length)
{
    if (address7>0x7FU || data==NULL || length==0U) return WATCH_INVALID_ARGUMENT;
    (void)reg;
    memset(data,0,length);
    return WATCH_NOT_IMPLEMENTED;
}

watch_status_t i2c_bus_write(uint8_t address7, uint8_t reg, const uint8_t *data, size_t length)
{
    if (address7>0x7FU || data==NULL || length==0U) return WATCH_INVALID_ARGUMENT;
    (void)reg;
    return WATCH_NOT_IMPLEMENTED;
}
