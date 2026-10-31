#include "uart_port.h"
#include "board_config.h"
#include <string.h>
/* BLE串口收发：按计划任务实现并经用户烧录验收。 */
watch_status_t uart_port_send(const uint8_t *data, size_t length)
{
    if(data==NULL || length==0U) return WATCH_INVALID_ARGUMENT;
    return WATCH_NOT_IMPLEMENTED;
}

watch_status_t uart_port_receive(uint8_t *data, size_t capacity, size_t *received)
{
    if(data==NULL || capacity==0U || received==NULL) return WATCH_INVALID_ARGUMENT;
    *received=0U;
    return WATCH_NOT_IMPLEMENTED;
}
