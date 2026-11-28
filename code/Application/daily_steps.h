#ifndef WATCH_DAILY_STEPS_H
#define WATCH_DAILY_STEPS_H
#include "watch_types.h"
/* 固定UTC+8日界线。只累计已确认算法步数，不改写开机会话计数。
 * 当日值是已观察部分：断电及未提交窗口不补造步数，不能保证覆盖整天。 */
typedef struct {
    uint32_t day_start_utc, steps, undated_steps, last_saved_steps;
    uint8_t pending_records;
    bool recovering, time_valid, clock_back, dirty;
    watch_status_t history_status;
} daily_steps_state_t;
void daily_steps_init(void);
/* 仅由计步器新增确认步数时调用；无时间或恢复期间记入undated，不倒填日期。 */
void daily_steps_add(uint32_t increment);
/* 每5ms在record_store_process后调用。每轮至多读取一条历史，不等待Flash。 */
watch_status_t daily_steps_process(uint32_t now_ms);
/* 将当日部分汇总排队；OK表示排队/已在队列，不表示落盘。 */
watch_status_t daily_steps_checkpoint(void);
watch_status_t daily_steps_get_state(daily_steps_state_t *state);
#endif
