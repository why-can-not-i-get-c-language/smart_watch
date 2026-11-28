#include "daily_steps.h"
#include "record_store.h"
#include "time_service.h"
#include "board_port.h"
#include <string.h>

enum { CHECKPOINT_MS=60000U, QUEUE_SIZE=4U };
static daily_steps_state_t current;
static record_t queue[QUEUE_SIZE];
static unsigned queued;
static bool in_flight, scan_started, storage_failed;
static uint32_t scan_id, scan_oldest, expected_id, last_checkpoint_ms;

static uint32_t saturated_add(uint32_t value, uint32_t delta)
{
    return UINT32_MAX - value < delta ? UINT32_MAX : value + delta;
}

static bool day_start(uint32_t utc, uint32_t *start)
{
    if (utc < 946684800UL || utc > 4102444799UL) return false;
    uint64_t day = ((uint64_t)utc + 28800U) / 86400U;
    uint32_t midnight = (uint32_t)(day * 86400U - 28800U);
    /* RTC支持起点为北京时间08:00，首日只记录支持范围内的部分。 */
    *start = midnight < 946684800UL ? 946684800UL : midnight;
    return true;
}

void daily_steps_init(void)
{
    memset(&current, 0, sizeof(current));
    current.recovering = true;
    current.history_status = WATCH_NOT_READY;
    queued = 0U;
    in_flight = scan_started = storage_failed = false;
    scan_id = scan_oldest = expected_id = 0U;
    last_checkpoint_ms = board_millis();
}

watch_status_t daily_steps_checkpoint(void)
{
    if (current.recovering || !current.time_valid) return WATCH_NOT_READY;
    if (storage_failed) return current.history_status;
    if (!current.dirty) return WATCH_OK;
    /* 同一天尚未提交的快照可合并，正在提交的首项不可更改。 */
    for (unsigned i = in_flight ? 1U : 0U; i < queued; ++i) {
        if (queue[i].utc_seconds == current.day_start_utc) {
            queue[i].value = current.steps;
            current.dirty = false;
            current.history_status = WATCH_NOT_READY;
            return WATCH_OK;
        }
    }
    if (queued == QUEUE_SIZE) {
        current.history_status = WATCH_NOT_READY;
        return WATCH_NOT_READY;
    }
    queue[queued++] = (record_t){current.day_start_utc, current.steps, RECORD_STORE_TYPE_DAILY_STEPS};
    current.pending_records = (uint8_t)queued;
    current.dirty = false;
    current.history_status = WATCH_NOT_READY;
    return WATCH_OK;
}

static bool update_date(void)
{
    uint32_t utc = 0U, today = 0U;
    current.time_valid = false;
    current.clock_back = false;
    if (time_service_get(&utc) != WATCH_OK || !day_start(utc, &today) || current.recovering) return false;
    if (current.day_start_utc != 0U && today < current.day_start_utc) {
        /* 校时回到较早日期时暂停日归属，不覆写已经观察到的未来日期。 */
        current.clock_back = true;
        return false;
    }
    current.time_valid = true;
    if (today != current.day_start_utc) {
        if (current.day_start_utc != 0U && current.dirty &&
            daily_steps_checkpoint() != WATCH_OK) {
            current.time_valid = false;
            return false;
        }
        current.day_start_utc = today;
        current.steps = current.last_saved_steps = 0U;
        current.dirty = true;
    }
    return true;
}

void daily_steps_add(uint32_t increment)
{
    if (increment == 0U) return;
    if (update_date()) {
        current.steps = saturated_add(current.steps, increment);
        current.dirty = true;
    } else current.undated_steps = saturated_add(current.undated_steps, increment);
}

static watch_status_t fail_storage(watch_status_t status)
{
    storage_failed = true;
    in_flight = false;
    current.recovering = false;
    current.history_status = status;
    return status;
}

watch_status_t daily_steps_process(uint32_t now_ms)
{
    record_store_state_t history;
    watch_status_t status = record_store_get_state(&history);
    if (status != WATCH_OK) return fail_storage(status);
    if (!history.ready || history.busy) {
        (void)update_date();
        return WATCH_NOT_READY;
    }
    if (current.recovering) {
        if (history.readonly) return fail_storage(history.load_status);
        if (!scan_started) {
            scan_started = true;
            scan_id = history.newest_id;
            scan_oldest = history.oldest_id;
        }
        if (scan_id != 0U && scan_id >= scan_oldest) {
            record_t record;
            status = record_store_read(scan_id, &record);
            if (status == WATCH_NOT_READY) return status;
            if (status == WATCH_IO_ERROR || status == WATCH_TIMEOUT) return fail_storage(status);
            if (status == WATCH_OK && record.type == RECORD_STORE_TYPE_DAILY_STEPS) {
                uint32_t start;
                if (!day_start(record.utc_seconds, &start) || start != record.utc_seconds)
                    return fail_storage(WATCH_INVALID_DATA);
                current.day_start_utc = start;
                current.steps = current.last_saved_steps = record.value;
                current.recovering = false;
            } else if (scan_id == scan_oldest) current.recovering = false;
            else --scan_id;
            /* 恢复期间每轮只读一条，手动事件/缺口跳过，不占用长循环。 */
        } else current.recovering = false;
        if (current.recovering) return WATCH_NOT_READY;
        current.history_status = WATCH_OK;
    }
    if (in_flight) {
        if (history.last_append_status != WATCH_OK) return fail_storage(history.last_append_status);
        record_t actual;
        status = record_store_read(expected_id, &actual);
        if (status == WATCH_NOT_READY) return status;
        if (status != WATCH_OK || actual.utc_seconds != queue[0].utc_seconds ||
            actual.value != queue[0].value || actual.type != queue[0].type)
            return fail_storage(status == WATCH_OK ? WATCH_IO_ERROR : status);
        if (actual.utc_seconds == current.day_start_utc) current.last_saved_steps = actual.value;
        memmove(queue, queue + 1U, (queued - 1U) * sizeof(queue[0]));
        --queued;
        in_flight = false;
        current.pending_records = (uint8_t)queued;
        current.history_status = WATCH_OK;
    }
    (void)update_date();
    if (history.readonly) return fail_storage(history.load_status == WATCH_OK ? WATCH_IO_ERROR : history.load_status);
    if (storage_failed) return current.history_status;
    if (current.time_valid && (uint32_t)(now_ms - last_checkpoint_ms) >= CHECKPOINT_MS) {
        if (daily_steps_checkpoint() == WATCH_OK) last_checkpoint_ms = now_ms;
    }
    if (queued != 0U) {
        status = record_store_append(&queue[0]);
        if (status == WATCH_OK) {
            expected_id = history.count == 0U ? 1U : history.newest_id + 1U;
            in_flight = true;
            current.history_status = WATCH_NOT_READY;
        } else if (status != WATCH_NOT_READY) return fail_storage(status);
        return status;
    }
    return current.history_status;
}

watch_status_t daily_steps_get_state(daily_steps_state_t *state)
{
    if (state == NULL) return WATCH_INVALID_ARGUMENT;
    *state = current;
    return WATCH_OK;
}
