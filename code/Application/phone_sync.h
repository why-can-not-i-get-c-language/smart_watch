#ifndef WATCH_PHONE_SYNC_H
#define WATCH_PHONE_SYNC_H
#include "watch_types.h"
/* 手机命令和状态同步。未实现接口返回WATCH_NOT_IMPLEMENTED，输出无效。 */
watch_status_t phone_sync_process(void);
#endif
