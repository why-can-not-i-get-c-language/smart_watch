#ifndef WATCH_BUTTONS_H
#define WATCH_BUTTONS_H
#include "watch_types.h"
/* 按键消抖和长短按。未实现接口返回WATCH_NOT_IMPLEMENTED，输出无效。 */
watch_status_t buttons_poll(uint32_t now_ms, uint32_t *events);
#endif
