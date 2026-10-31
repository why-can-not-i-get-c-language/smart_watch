#ifndef WATCH_WATCH_UI_H
#define WATCH_WATCH_UI_H
#include "watch_types.h"
/* 页面与有效状态展示。未实现接口返回WATCH_NOT_IMPLEMENTED，输出无效。 */
watch_status_t watch_ui_update(uint32_t now_ms);
#endif
