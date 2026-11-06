#ifndef WATCH_WATCH_UI_H
#define WATCH_WATCH_UI_H
#include "watch_types.h"
/* 周期推进首次显示自检和基本状态页；掉屏后最多每秒重试初始化。
 * 配合独立的ssd1306_process发送任务使用，不在此函数中整帧阻塞发送。 */
watch_status_t watch_ui_update(uint32_t now_ms);
#endif
