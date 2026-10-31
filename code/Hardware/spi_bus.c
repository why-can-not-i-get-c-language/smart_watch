#include "spi_bus.h"
#include "board_config.h"
#include <string.h>
/* Flash SPI端口：按计划任务实现并经用户烧录验收。 */
watch_status_t spi_bus_transfer(const uint8_t *tx, uint8_t *rx, size_t length)
{
    if (length==0U || (tx==NULL && rx==NULL)) return WATCH_INVALID_ARGUMENT;
    if(rx!=NULL) memset(rx,0,length);
    return WATCH_NOT_IMPLEMENTED;
}
