#ifndef WATCH_SSD1306_H
#define WATCH_SSD1306_H
#include "watch_types.h"
/* 屏幕初始化、帧缓冲提交与开关。未实现接口返回WATCH_NOT_IMPLEMENTED，输出无效。 */
watch_status_t ssd1306_init(void);
watch_status_t ssd1306_flush(const uint8_t *frame, size_t length);
watch_status_t ssd1306_set_enabled(bool enabled);
#endif
