#ifndef WATCH_RECORD_STORE_H
#define WATCH_RECORD_STORE_H
#include "watch_types.h"
/* 环形历史与断电恢复。未实现接口返回WATCH_NOT_IMPLEMENTED，输出无效。 */
watch_status_t record_store_append(const record_t *record);
watch_status_t record_store_read(uint32_t id, record_t *record);
#endif
