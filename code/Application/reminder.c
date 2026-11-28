#include "reminder.h"
#include "board_port.h"
#include "time_service.h"
#include "tm6605.h"
#include <string.h>

enum { REMINDER_RING_MS=10000U, REMINDER_START_TIMEOUT_MS=1000U };
static reminder_state_t current;
static uint32_t request_ms;
static reminder_phase_t after_stop;
static watch_status_t after_stop_status;

static bool utc_valid(uint32_t utc)
{
    rtc_calendar_t cal;
    return time_utc_to_calendar(utc, &cal) == WATCH_OK;
}

void reminder_init(void)
{
    memset(&current, 0, sizeof(current));
    request_ms = 0U;
    after_stop = REMINDER_CANCELLED;
    after_stop_status = WATCH_OK;
}

static watch_status_t stop_owned(reminder_phase_t final_phase, watch_status_t final_status)
{
    watch_status_t status = tm6605_stop();
    if (status != WATCH_OK) {
        current.phase = REMINDER_ERROR;
        current.status = status;
        /* 停止未获接受时仍持有马达，阻止手动播放覆盖；用户可再次取消。 */
        return status;
    }
    current.phase = REMINDER_STOPPING;
    current.status = WATCH_NOT_READY;
    after_stop = final_phase;
    after_stop_status = final_status;
    request_ms = board_millis();
    return WATCH_OK;
}

watch_status_t reminder_set(uint32_t utc_seconds)
{
    if (!utc_valid(utc_seconds)) { return WATCH_INVALID_ARGUMENT; }
    if (current.owns_haptic) { return WATCH_NOT_READY; }
    uint32_t now_utc = 0U;
    watch_status_t status = time_service_get(&now_utc);
    if (status != WATCH_OK) { return status; }
    if (!utc_valid(now_utc) || utc_seconds <= now_utc) { return WATCH_INVALID_ARGUMENT; }
    current.target_utc = utc_seconds;
    current.phase = REMINDER_ARMED;
    current.status = WATCH_OK;
    return WATCH_OK;
}

watch_status_t reminder_update(uint32_t utc_seconds)
{
    tm6605_state_t motor;
    uint32_t now_ms = board_millis();
    if (current.phase == REMINDER_STOPPING) {
        watch_status_t status = tm6605_get_state(&motor);
        if (status != WATCH_OK || !motor.busy) {
            bool stopped = status == WATCH_OK && motor.status == WATCH_OK &&
                motor.command == TM6605_STOP;
            current.phase = stopped ? after_stop : REMINDER_ERROR;
            current.status = stopped ? after_stop_status :
                status != WATCH_OK ? status : motor.status == WATCH_OK ? WATCH_IO_ERROR : motor.status;
            current.owns_haptic = false;
        } else if ((uint32_t)(now_ms - request_ms) >= REMINDER_START_TIMEOUT_MS) {
            /* 防止停止排队永久占用；仍保留所有权，允许用户重试取消。 */
            current.phase = REMINDER_ERROR;
            current.status = WATCH_TIMEOUT;
        }
        return current.status;
    }
    if (current.phase != REMINDER_ARMED && current.phase != REMINDER_STARTING &&
        current.phase != REMINDER_RINGING) { return current.status; }

    uint32_t actual = 0U;
    watch_status_t clock_status = time_service_get(&actual);
    if (clock_status != WATCH_OK || !utc_valid(actual) || !utc_valid(utc_seconds)) {
        watch_status_t status = clock_status == WATCH_OK ? WATCH_INVALID_DATA : clock_status;
        if (current.owns_haptic) { return stop_owned(REMINDER_ERROR, status); }
        current.phase = REMINDER_ERROR;
        current.status = status;
        return status;
    }
    /* 不接受调用者伪造/陈旧的时间；允许读取跨秒的1秒差。 */
    uint32_t difference = actual > utc_seconds ? actual - utc_seconds : utc_seconds - actual;
    if (difference > 1U) {
        if (current.owns_haptic) { return stop_owned(REMINDER_ERROR, WATCH_INVALID_DATA); }
        current.phase = REMINDER_ERROR;
        current.status = WATCH_INVALID_DATA;
        return current.status;
    }
    if (current.phase == REMINDER_ARMED) {
        if (actual < current.target_utc) { return WATCH_OK; }
        if (actual - current.target_utc > 5U) {
            current.phase = REMINDER_MISSED;
            current.status = WATCH_TIMEOUT;
            return current.status;
        }
        /* 先消费本次到时，再请求驱动；失败也不在后续调度中自动重播。 */
        current.phase = REMINDER_ERROR;
        current.status = tm6605_alert(REMINDER_RING_MS);
        if (current.status == WATCH_OK) {
            current.phase = REMINDER_STARTING;
            current.owns_haptic = true;
            request_ms = now_ms;
            if (current.trigger_count != UINT32_MAX) { ++current.trigger_count; }
        }
        return current.status;
    }
    watch_status_t status = tm6605_get_state(&motor);
    if (status != WATCH_OK || (!motor.busy && motor.status != WATCH_OK)) {
        current.phase = REMINDER_ERROR;
        current.status = status != WATCH_OK ? status : motor.status;
        current.owns_haptic = false;
    } else if ((uint32_t)(now_ms - request_ms) >= REMINDER_RING_MS) {
        return stop_owned(REMINDER_DONE, WATCH_OK);
    } else if (current.phase == REMINDER_STARTING) {
        if (!motor.busy && motor.ready && motor.alert_active && motor.command == TM6605_ALERT) {
            current.phase = REMINDER_RINGING;
            current.status = WATCH_OK;
        } else if ((uint32_t)(now_ms - request_ms) >= REMINDER_START_TIMEOUT_MS) {
            return stop_owned(REMINDER_ERROR, WATCH_TIMEOUT);
        }
    } else if (!motor.alert_active) {
        /* 被外部停止等情况只结束当前提醒，不恢复播放。 */
        if (motor.busy) { return WATCH_NOT_READY; }
        current.phase = REMINDER_DONE;
        current.owns_haptic = false;
        current.status = WATCH_OK;
    }
    return current.status;
}

watch_status_t reminder_cancel(void)
{
    if (current.phase == REMINDER_STOPPING) { return WATCH_OK; }
    if (current.owns_haptic) { return stop_owned(REMINDER_CANCELLED, WATCH_OK); }
    current.phase = REMINDER_CANCELLED;
    current.status = WATCH_OK;
    return WATCH_OK;
}

watch_status_t reminder_get_state(reminder_state_t *state)
{
    if (state == NULL) { return WATCH_INVALID_ARGUMENT; }
    *state = current;
    return WATCH_OK;
}
