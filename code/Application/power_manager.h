#ifndef WATCH_POWER_MANAGER_H
#define WATCH_POWER_MANAGER_H
#include "watch_types.h"
/* 低功耗状态协调。未实现接口返回WATCH_NOT_IMPLEMENTED，输出无效。 */
watch_status_t power_manager_update(uint32_t now_ms);
watch_status_t power_manager_resume(void);
#endif
