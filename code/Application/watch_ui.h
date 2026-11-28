#ifndef WATCH_WATCH_UI_H
#define WATCH_WATCH_UI_H
#include "watch_types.h"
#include "board_config.h"
/* 推进开机显示自检和正常北京时间显示，支持本地日期与时间编辑。
 * 掉屏后重试且恢复当前页面，编辑草稿保留。
 * 按板级超时配置自动熄屏，配置0则禁用；首个唤醒手势不执行UI功能。 */
watch_status_t watch_ui_update(uint32_t now_ms);
/* 分发按键事件给导航、编辑状态机及功能页；按键计数由main诊断维护。 */
void watch_ui_notify_button(uint32_t event, uint32_t now_ms);
/* 返回最近的保存/取消状态：WATCH_NOT_READY初始，WATCH_OK保存成功，其他为具体错误码。 */
watch_status_t watch_ui_get_action_status(void);
/* HOME为默认时间页，时间页MENU短按或KEY长按进入功能菜单（STEPS/HISTORY/TOOLS）。
 * 功能菜单STEPS/HISTORY/REMINDER/ACTIVITY/TOOLS：KEY短按下一项，SOS短按上一项，
 * MENU短按进入选定项，KEY长按返回HOME。
 * REMINDER页KEY短按将延时草稿+30秒（30..300循环），MENU短按按草稿设一次提醒。
 * MENU长按保存延时，SOS短按取消提醒，SOS长按回读并丢弃草稿。
 * 重启只恢复已保存延时，不自动恢复或设定提醒。
 * ACTIVITY页KEY短按调整5..120分钟（5分钟步长），MENU短按切使能草稿；
 * MENU长按保存并应用，SOS短按确认，SOS长按重读。旧配置默认关闭。
 * ACTIVITY页同时短按MENU+SOS启动一次不保存的15秒快速测试窗口。
 * 低活动通知每个静止窗口一次，活动后复位；定时提醒优先，可抢占低活动序列。
 * 提醒到时唤醒，KEY/SOS在任意页停止并消费整次手势，不改编辑草稿。
 * TOOLS菜单8项（BATTERY/PPG/MPU/WRIST/BLE/HAPTIC/PARAMETERS/FLASH），滚屏显示标题和页脚。
 * TOOLS菜单：KEY/SOS短按上下滚动，MENU短按进入，KEY长按返回功能菜单。
 * 各功能详情页保留原有MENU/SOS操作，KEY长按返回拥有菜单。
 * BATTERY默认显示估算汇总，MENU短按切换原ADC详情，再按返回汇总。
 * STEPS页MENU短按切换SESSION/DAILY PARTIAL；会话页MENU长按清零会话，
 * 日汇总页MENU长按请求保存部分汇总。清会话不清当日汇总。
 * 有效低电时主页底行或其他正常页标题行提示LOW BAT CHARGE；编辑和自检不覆盖。
 * 历史页MENU短按追加真实手动事件，SOS短按重新读取最新记录。
 * Flash页MENU长按启动测试区查空和自检；失败且本次开机归属明确时SOS长按清理。
 * 参数页MENU短按修改草稿计数、MENU长按提交保存、SOS短按重新读取。
 * MPU页KEY短按累加K用于PA0轮询对照；振动页切出时停止效果；BLE页只显示D3收发诊断。
 * 编辑器中KEY长按仍被忽略，保存/取消返回HOME。重连保持导航位置。 */
bool watch_ui_haptic_active(void);
/* 查询BLE诊断页是否激活；业务协议不因页面按键发送裸文本。 */
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
#if WATCH_BLE_NAME_SETUP
/* 专用改名固件独占显示，不进入普通UI、自检或熄屏流程。 */
watch_status_t watch_ui_ble_name_setup(bool attempted, watch_status_t result, uint32_t now_ms);
#endif
#endif
