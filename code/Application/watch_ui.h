#ifndef WATCH_WATCH_UI_H
#define WATCH_WATCH_UI_H
#include "watch_types.h"
#include <stdint.h>

/* 周期推进首次显示自检和当前按键验收页；掉屏后最多每秒重试初始化。
 * 配合独立的ssd1306_process发送任务使用，不在此函数中整帧阻塞发送。 */
watch_status_t watch_ui_update(uint32_t now_ms);

/* 接收各键组合事件；自检、掉屏期间也保留计数。
 * 当前为F00-P05按键验收测试页，非正式整机功能页。 */
void watch_ui_notify_button(uint32_t event, uint32_t now_ms);

#endif
