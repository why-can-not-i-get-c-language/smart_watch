#include "pedometer.h"
#include "board_config.h"
#include "board_port.h"
#include <math.h>
#include <limits.h>
#include <string.h>

/* 本步只保存RAM累计；日界线和历史存储留后续任务。 */
static pedometer_status_t current;
typedef enum { WAVE_NONE, WAVE_VALLEY, WAVE_RISING, WAVE_PEAK } wave_phase_t;
static struct {
    bool have_sample, have_candidate, have_period, confirmed;
    uint32_t last_sample_ms, warmup_started_ms, last_candidate_ms, period_ms;
    wave_phase_t wave;
    uint32_t wave_started_ms, valley_started_ms, peak_started_ms, peak_ms;
    float minimum, maximum;
    float gravity, filtered;
} detector;

static void restart(pedometer_state_t state)
{
    memset(&detector, 0, sizeof(detector));
    current.state = state;
    current.last_update_ms = 0U;
    current.signal_g = 0.0f;
    current.pending_steps = 0U;
}

static void add_steps(uint32_t increment)
{
    current.step_count = UINT32_MAX - current.step_count < increment ?
                         UINT32_MAX : current.step_count + increment;
}

static void clear_sequence(void)
{
    detector.have_candidate = false;
    detector.have_period = false;
    detector.confirmed = false;
    current.pending_steps = 0U;
}

static void start_valley(uint32_t now_ms, float signal)
{
    detector.wave = WAVE_VALLEY;
    detector.wave_started_ms = now_ms;
    detector.valley_started_ms = now_ms;
    detector.minimum = signal;
}

static void accept_candidate(uint32_t peak_ms)
{
    uint32_t interval = peak_ms - detector.last_candidate_ms;
    bool regular = detector.have_candidate && interval >= WATCH_PEDOMETER_MIN_INTERVAL_MS &&
                   interval <= WATCH_PEDOMETER_MAX_INTERVAL_MS;
    if (regular && detector.have_period) {
        uint32_t tolerance = detector.period_ms * WATCH_PEDOMETER_CADENCE_TOLERANCE_PERCENT / 100U;
        if (tolerance < WATCH_PEDOMETER_CADENCE_TOLERANCE_MS) { tolerance = WATCH_PEDOMETER_CADENCE_TOLERANCE_MS; }
        uint32_t difference = interval > detector.period_ms ? interval - detector.period_ms : detector.period_ms - interval;
        regular = difference <= tolerance;
    }
    if (!regular) {
        clear_sequence();
        current.pending_steps = 1U;
    } else {
        detector.period_ms = detector.have_period ? (3U * detector.period_ms + interval) / 4U : interval;
        detector.have_period = true;
        if (detector.confirmed) {
            add_steps(1U);
        } else {
            current.pending_steps++;
            if (current.pending_steps >= WATCH_PEDOMETER_CONFIRM_STEPS) {
                add_steps(current.pending_steps);
                current.pending_steps = 0U;
                detector.confirmed = true;
            }
        }
    }
    detector.last_candidate_ms = peak_ms;
    detector.have_candidate = true;
}

static void process_wave(uint32_t now_ms, float signal)
{
    if (detector.wave != WAVE_NONE &&
        (uint32_t)(now_ms - detector.wave_started_ms) > WATCH_PEDOMETER_MAX_WAVE_MS) {
        detector.wave = WAVE_NONE;
        clear_sequence();
    }
    switch (detector.wave) {
    case WAVE_NONE:
        if (signal <= WATCH_PEDOMETER_VALLEY_THRESHOLD) { start_valley(now_ms, signal); }
        break;
    case WAVE_VALLEY:
        if (signal < detector.minimum) { detector.minimum = signal; }
        if (signal > WATCH_PEDOMETER_VALLEY_THRESHOLD) {
            uint32_t duration = now_ms - detector.valley_started_ms;
            if (duration < WATCH_PEDOMETER_MIN_VALLEY_MS || duration > WATCH_PEDOMETER_MAX_VALLEY_MS) {
                detector.wave = WAVE_NONE;
                clear_sequence();
            } else { detector.wave = WAVE_RISING; }
        }
        break;
    case WAVE_RISING:
        if (signal <= WATCH_PEDOMETER_VALLEY_THRESHOLD) {
            clear_sequence();
            start_valley(now_ms, signal);
        } else if (signal >= WATCH_PEDOMETER_PEAK_THRESHOLD) {
            detector.wave = WAVE_PEAK;
            detector.peak_started_ms = now_ms;
            detector.peak_ms = now_ms;
            detector.maximum = signal;
        }
        break;
    case WAVE_PEAK:
        if (signal > detector.maximum) { detector.maximum = signal; detector.peak_ms = now_ms; }
        if (signal <= WATCH_PEDOMETER_PEAK_END_THRESHOLD) {
            uint32_t duration = now_ms - detector.peak_started_ms;
            if (duration >= WATCH_PEDOMETER_MIN_PEAK_MS && duration <= WATCH_PEDOMETER_MAX_PEAK_MS &&
                detector.maximum - detector.minimum >= WATCH_PEDOMETER_MIN_SWING_G) {
                accept_candidate(detector.peak_ms);
            } else { clear_sequence(); }
            detector.wave = WAVE_NONE;
            if (signal <= WATCH_PEDOMETER_VALLEY_THRESHOLD) { start_valley(now_ms, signal); }
        } else if ((uint32_t)(now_ms - detector.peak_started_ms) > WATCH_PEDOMETER_MAX_PEAK_MS) {
            detector.wave = WAVE_NONE;
            clear_sequence();
        }
        break;
    }
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
        (uint32_t)(sample->timestamp_ms - detector.last_candidate_ms) >
        WATCH_PEDOMETER_MAX_INTERVAL_MS + WATCH_PEDOMETER_MAX_PEAK_MS) {
        clear_sequence();
    }
    /* 候选必须走完整峰谷，并满足幅差、持续时间及连续节律。
     * 参考AN-2554的连续候选确认思路，未移植其驱动或精度结论。 */
    process_wave(sample->timestamp_ms, current.signal_g);
    if (current.pending_steps != 0U) { current.state = PEDOMETER_VERIFYING; }
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
