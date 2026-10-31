#include "ble_link.h"
#include "board_config.h"
#include <string.h>
/* BLE透传与链路状态：按计划任务实现并经用户烧录验收。 */
watch_status_t ble_link_init(void)
{
    return WATCH_NOT_IMPLEMENTED;
}

watch_status_t ble_link_send(const uint8_t *data, size_t length)
{
    if(data==NULL || length==0U) return WATCH_INVALID_ARGUMENT;
    return WATCH_NOT_IMPLEMENTED;
}

watch_status_t ble_link_connected(bool *connected)
{
    if(connected==NULL) return WATCH_INVALID_ARGUMENT;
    *connected=false;
    return WATCH_NOT_IMPLEMENTED;
}
