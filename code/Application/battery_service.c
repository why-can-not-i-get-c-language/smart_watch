#include "battery_service.h"
#include "board_config.h"
#include "battery_adc.h"
#include "board_port.h"
#include <string.h>

/* 本组端点/阈值为项目初始参数，不是FIT0120实测放电曲线。 */
#if WATCH_BATTERY_EST_EMPTY_MV >= WATCH_BATTERY_EST_FULL_MV || \
    WATCH_BATTERY_LOW_MV >= WATCH_BATTERY_RECOVER_MV || \
    WATCH_BATTERY_VALID_MIN_MV > WATCH_BATTERY_EST_EMPTY_MV || \
    WATCH_BATTERY_VALID_MAX_MV < WATCH_BATTERY_EST_FULL_MV
#error Invalid battery estimation thresholds
#endif

static battery_service_state_t current = {.status = WATCH_NOT_READY};
static bool have_sample, have_seen_batch, candidate_active, candidate_low;
static uint32_t last_sample_ms, last_sample_count, candidate_start_ms;

watch_status_t battery_service_update(uint16_t millivolts, battery_state_t *state)
{
    if (state == NULL) { return WATCH_INVALID_ARGUMENT; }
    memset(state, 0, sizeof(*state));
    if (millivolts < WATCH_BATTERY_VALID_MIN_MV ||
        millivolts > WATCH_BATTERY_VALID_MAX_MV) { return WATCH_INVALID_DATA; }
    uint32_t percent = 0U;
    if (millivolts >= WATCH_BATTERY_EST_FULL_MV) { percent = 100U; }
    else if (millivolts > WATCH_BATTERY_EST_EMPTY_MV) {
        uint32_t span = WATCH_BATTERY_EST_FULL_MV - WATCH_BATTERY_EST_EMPTY_MV;
        percent = ((uint32_t)(millivolts - WATCH_BATTERY_EST_EMPTY_MV) * 100U +
                   span / 2U) / span;
    }
    state->millivolts = millivolts;
    state->percent = (uint8_t)percent;
    state->valid = true;
    return WATCH_OK;
}

void battery_service_init(void)
{
    memset(&current, 0, sizeof(current));
    current.status = WATCH_NOT_READY;
    have_sample = have_seen_batch = candidate_active = false;
    candidate_low = false;
    last_sample_ms = last_sample_count = candidate_start_ms = 0U;
}

static watch_status_t invalidate(watch_status_t status)
{
    memset(&current.battery, 0, sizeof(current.battery));
    current.status = status;
    /* 中断连续性，但保留已见批次身份；旧批次不能让失效值重新变有效。 */
    have_sample = false;
    candidate_active = false;
    return status;
}

watch_status_t battery_service_process(uint32_t now_ms)
{
    battery_adc_state_t adc;
    watch_status_t status = battery_adc_get_state(&adc);
    if (status != WATCH_OK || !adc.valid) {
        return invalidate(status == WATCH_OK ? WATCH_NOT_READY : status);
    }
    /* age>=1000ms为陈旧，精确1000ms时即无效。 */
    if ((uint32_t)(now_ms - adc.timestamp_ms) >= WATCH_BATTERY_ADC_STALE_MS) {
        return invalidate(WATCH_TIMEOUT);
    }
    battery_state_t estimate;
    status = battery_service_update(adc.battery_mv, &estimate);
    if (status != WATCH_OK) { return invalidate(status); }
    /* 批次计数可能饱和；联合时间戳去重，并在失效后保留去重标记。 */
    if (have_seen_batch && adc.timestamp_ms == last_sample_ms &&
        adc.sample_count == last_sample_count) {
        return current.status;
    }
    /* 有效样本间隔达到失效界限时，不能连续算低电/恢复时间。 */
    if (have_sample &&
        (uint32_t)(adc.timestamp_ms - last_sample_ms) >= WATCH_BATTERY_ADC_STALE_MS) {
        candidate_active = false;
    }
    have_sample = true;
    have_seen_batch = true;
    last_sample_ms = adc.timestamp_ms;
    last_sample_count = adc.sample_count;
    current.battery = estimate;
    current.timestamp_ms = adc.timestamp_ms;
    current.status = WATCH_OK;

    bool desired_low = current.low;
    if (adc.battery_mv <= WATCH_BATTERY_LOW_MV) { desired_low = true; }
    else if (adc.battery_mv >= WATCH_BATTERY_RECOVER_MV) { desired_low = false; }
    else if (!current.low_known) { desired_low = false; }

    if (current.low_known && desired_low == current.low) {
        candidate_active = false;
    } else if (!candidate_active || candidate_low != desired_low) {
        candidate_active = true;
        candidate_low = desired_low;
        candidate_start_ms = adc.timestamp_ms;
    } else if ((uint32_t)(adc.timestamp_ms - candidate_start_ms) >= WATCH_BATTERY_CONFIRM_MS) {
        if (desired_low && !current.low && current.low_event_count != UINT32_MAX) {
            ++current.low_event_count;
        }
        current.low = desired_low;
        current.low_known = true;
        candidate_active = false;
    }
    return WATCH_OK;
}

watch_status_t battery_service_get_state(battery_service_state_t *state)
{
    if (state == NULL) { return WATCH_INVALID_ARGUMENT; }
    if (current.battery.valid &&
        (uint32_t)(board_millis() - current.timestamp_ms) >= WATCH_BATTERY_ADC_STALE_MS) {
        (void)invalidate(WATCH_TIMEOUT);
    }
    *state = current;
    return current.status;
}
