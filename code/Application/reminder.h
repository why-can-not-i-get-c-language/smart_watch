#ifndef WATCH_REMINDER_H
#define WATCH_REMINDER_H
#include "watch_types.h"
/* 提醒状态机。未实现接口返回WATCH_NOT_IMPLEMENTED，输出无效。 */
watch_status_t reminder_set(uint32_t utc_seconds);
watch_status_t reminder_update(uint32_t utc_seconds);
watch_status_t reminder_cancel(void);
#endif
