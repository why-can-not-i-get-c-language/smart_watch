#include "record_store.h"
#include "board_config.h"
#include <string.h>
/* 环形历史与断电恢复：按计划任务实现并经用户烧录验收。 */
watch_status_t record_store_append(const record_t *record)
{
    if(record==NULL) return WATCH_INVALID_ARGUMENT;
    return WATCH_NOT_IMPLEMENTED;
}

watch_status_t record_store_read(uint32_t id, record_t *record)
{
    if(record==NULL) return WATCH_INVALID_ARGUMENT;
    (void)id;
    memset(record,0,sizeof(*record));
    return WATCH_NOT_IMPLEMENTED;
}
