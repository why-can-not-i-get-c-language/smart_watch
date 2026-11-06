#ifndef WATCH_WATCH_UI_H
#define WATCH_WATCH_UI_H
#include "watch_types.h"
/* 推进开机显示自检和当前RTC验收页，掉屏后重试且恢复RTC页。
 * 菜单/SOS的RTC样例操作由主入口分发，本接口只绘制真实读取结果。 */
watch_status_t watch_ui_update(uint32_t now_ms);
/* 保留三键组合事件统计，测试页面切换不改变输入事件语义。 */
void watch_ui_notify_button(uint32_t event, uint32_t now_ms);
#endif
