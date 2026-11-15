#ifndef WATCH_WATCH_UI_H
#define WATCH_WATCH_UI_H
#include "watch_types.h"
/* 推进开机显示自检和正常北京时间显示，支持本地日期与时间编辑。
 * 掉屏后重试且恢复当前页面，编辑草稿保留。
 * 按板级超时配置自动熄屏，配置0则禁用；首个唤醒手势不执行UI功能。 */
watch_status_t watch_ui_update(uint32_t now_ms);
/* 分发按键事件给编辑状态机；按键计数由main诊断维护。 */
void watch_ui_notify_button(uint32_t event, uint32_t now_ms);
/* 返回最近的保存/取消状态：WATCH_NOT_READY初始，WATCH_OK保存成功，其他为具体错误码。 */
watch_status_t watch_ui_get_action_status(void);
/* KEY长按循环BLE、姿态、计步、MPU、振动、时间页，编辑中不切换。
 * 熄屏不退出振动页，按键过滤由主分发入口统一执行。 */
bool watch_ui_haptic_active(void);
/* 供统一按键分发在过滤唤醒手势后判断主动发送请求。 */
bool watch_ui_ble_active(void);
/* 过滤按键事件：首次唤醒完整按键手势被消费，返回0掩码；之后恢复过滤后掩码。
 * 同帧RELEASE|SHORT组合吞掉，已被blocked的键释放时清blocked标志。
 * 不做I2C访问，仅跟踪各键down/blocked状态。 */
uint32_t watch_ui_filter_buttons(uint32_t events, uint32_t now_ms);
/* 查询空闲熄屏目标状态；不表示关屏命令已ACK或实际功耗已测量。 */
bool watch_ui_is_sleeping(void);
/* 唤醒屏幕：由按键或姿态事件调用，延长或重置超时计时，
 * 完整刷新当前页后发送开屏命令；屏幕尚未实际开/驱动未ready时预先刷新。 */
void watch_ui_wake(uint32_t now_ms);
#endif
