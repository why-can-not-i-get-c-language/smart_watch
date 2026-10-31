#ifndef WATCH_ACTIVITY_H
#define WATCH_ACTIVITY_H
#include "watch_types.h"
/* 低活动时间窗。未实现接口返回WATCH_NOT_IMPLEMENTED，输出无效。 */
watch_status_t activity_update(uint32_t now_ms, uint32_t steps, bool *remind);
#endif
