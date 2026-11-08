#include "pedometer.h"
#include "board_config.h"
#include "board_port.h"
#include <math.h>
#include <limits.h>
#include <string.h>

/* 本步只保存RAM累计；日界线和历史存储留后续任务。 */
static pedometer_status_t current;
static struct {
    bool have_sample, armed, have_candidate, confirmed;
    uint32_t last_sample_ms, warmup_started_ms, last_candidate_ms;
    float gravity, filtered;
} detector;

static void restart(pedometer_state_t state)
{
    memset(&detector, 0, sizeof(detector));
    current.state = state;
    current.last_update_ms = 0U;
    current.signal_g = 0.0f;
}

static void add_steps(uint32_t increment)
{
    current.step_count = UINT32_MAX - current.step_count < increment ?
                         UINT32_MAX : current.step_count + increment;
}

watch_status_t pedometer_update(const motion_sample_t *sample, uint32_t *steps)
{
    if (steps == NULL) { return WATCH_INVALID_ARGUMENT; }
    *steps = current.step_count;
    if (sample == NULL) { return WATCH_INVALID_ARGUMENT; }
    if (!sample->valid) {
        restart(PEDOMETER_NO_SAMPLE);
        return WATCH_NOT_READY;
    }
    if (!isfinite(sample->ax_g) || !isfinite(sample->ay_g) || !isfinite(sample->az_g) ||
        fabsf(sample->ax_g) > 8.0f || fabsf(sample->ay_g) > 8.0f || fabsf(sample->az_g) > 8.0f) {
        restart(PEDOMETER_NO_SAMPLE);
        return WATCH_INVALID_DATA;
    }
    /* 主控读取时间戳与当前毫秒时基同源，过期或未来样本不可重新消费。 */
    if ((uint32_t)(board_millis() - sample->timestamp_ms) > WATCH_PEDOMETER_MAX_GAP_MS) {
        restart(PEDOMETER_NO_SAMPLE);
        return WATCH_INVALID_DATA;
    }
    if (detector.have_sample) {
        uint32_t delta = sample->timestamp_ms - detector.last_sample_ms;
        if (delta == 0U || delta > INT32_MAX) { return WATCH_INVALID_DATA; }
        if (delta > WATCH_PEDOMETER_MAX_GAP_MS) { restart(PEDOMETER_WARMING); }
    }
    float magnitude = sqrtf(sample->ax_g * sample->ax_g + sample->ay_g * sample->ay_g +
                            sample->az_g * sample->az_g);
    if (!detector.have_sample) {
        detector.have_sample = true;
        detector.warmup_started_ms = sample->timestamp_ms;
        detector.gravity = magnitude;
        detector.filtered = magnitude;
        current.state = PEDOMETER_WARMING;
    }
    detector.last_sample_ms = sample->timestamp_ms;
    current.last_update_ms = sample->timestamp_ms;
    detector.filtered += WATCH_PEDOMETER_ACCEL_SMOOTH * (magnitude - detector.filtered);
    detector.gravity += WATCH_PEDOMETER_GRAVITY_SMOOTH * (magnitude - detector.gravity);
    current.signal_g = detector.filtered - detector.gravity;
    if ((uint32_t)(sample->timestamp_ms - detector.warmup_started_ms) < WATCH_PEDOMETER_WARMUP_MS) {
        return WATCH_NOT_READY;
    }
    current.state = PEDOMETER_READY;
    if (detector.have_candidate &&
        (uint32_t)(sample->timestamp_ms - detector.last_candidate_ms) > WATCH_PEDOMETER_MAX_INTERVAL_MS) {
        detector.have_candidate = false;
        detector.confirmed = false;
    }
    /* 谷后上升跨越峰阈值才形成候选；模长对静止安装倾角不敏感。 */
    if (current.signal_g <= WATCH_PEDOMETER_VALLEY_THRESHOLD) { detector.armed = true; }
    if (detector.armed && current.signal_g >= WATCH_PEDOMETER_PEAK_THRESHOLD) {
        detector.armed = false;
        uint32_t interval = sample->timestamp_ms - detector.last_candidate_ms;
        if (detector.have_candidate && interval >= WATCH_PEDOMETER_MIN_INTERVAL_MS &&
            interval <= WATCH_PEDOMETER_MAX_INTERVAL_MS) {
            /* 第二个候选确认节律后补记首个；之后每个合理候选加一步。 */
            add_steps(detector.confirmed ? 1U : 2U);
            detector.confirmed = true;
        } else {
            detector.confirmed = false;
        }
        /* 过快候选也更新时间，不能每隔一个快速冲击就误当成正常节律。 */
        detector.last_candidate_ms = sample->timestamp_ms;
        detector.have_candidate = true;
    }
    *steps = current.step_count;
    return WATCH_OK;
}

watch_status_t pedometer_reset(void)
{
    current.step_count = 0U;
    restart(PEDOMETER_CLEARED);
    return WATCH_OK;
}

watch_status_t pedometer_get_state(pedometer_status_t *status)
{
    if (status == NULL) { return WATCH_INVALID_ARGUMENT; }
    *status = current;
    return WATCH_OK;
}
