#ifndef WATCH_WATCH_APP_H
#define WATCH_WATCH_APP_H
#include "watch_types.h"
/* 初始化与任务装配。未实现接口返回WATCH_NOT_IMPLEMENTED，输出无效。 */
watch_status_t watch_app_init(void);
watch_status_t watch_app_process(void);
#endif
