#ifndef WATCH_WATCH_UI_H
#define WATCH_WATCH_UI_H
#include "watch_types.h"
/* 推进开机显示自检和正常北京时间显示，支持本地日期与时间编辑。
 * 掉屏后重试且恢复当前页面，编辑草稿保留。 */
watch_status_t watch_ui_update(uint32_t now_ms);
/* 分发按键事件给编辑状态机；按键计数由main诊断维护。 */
void watch_ui_notify_button(uint32_t event, uint32_t now_ms);
/* 返回最近的保存/取消状态：WATCH_NOT_READY初始，WATCH_OK保存成功，其他为具体错误码。 */
watch_status_t watch_ui_get_action_status(void);
/* KEY长按循环姿态、计步、MPU、振动、时间页，编辑中不切换。
 * 姿态页只反馈判定结果，本步不自动控制亮灭屏。 */
bool watch_ui_haptic_active(void);
#endif
