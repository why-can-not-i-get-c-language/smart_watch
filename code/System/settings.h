#ifndef WATCH_SETTINGS_H
#define WATCH_SETTINGS_H
#include "watch_types.h"
/* 带版本参数持久化。未实现接口返回WATCH_NOT_IMPLEMENTED，输出无效。 */
watch_status_t settings_load(void *data, size_t length);
watch_status_t settings_save(const void *data, size_t length);
#endif
