#include "wrist_wake.h"
#include "board_config.h"
#include "board_port.h"
#include <math.h>
#include <limits.h>
#include <string.h>

typedef struct { float x, y, z; } direction_t;
static wrist_wake_status_t current;
static direction_t reference, anchor, sum, away_anchor;
static bool have_time, collecting, away_holding, view_holding, have_event, calib_failed;
static uint32_t last_sample_ms, request_ms, collect_ms, away_ms, view_ms, last_event_ms;
static uint32_t collect_count;

static float dot(direction_t a, direction_t b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

static void clear_tracking(void)
{
    away_holding = false;
    view_holding = false;
    current.armed_state = WRIST_ARMED_IDLE;
    current.in_view = false;
}

static watch_status_t bad_sample(watch_status_t status, bool reset_time)
{
    clear_tracking();
    collecting = false;
    if (reset_time) { have_time = false; }
    current.pose = calib_failed ? WRIST_POSE_CALIB_FAILED : WRIST_POSE_SENSOR_ERROR;
    current.status = calib_failed ? WATCH_TIMEOUT : status;
    return current.status;
}

watch_status_t wrist_wake_reset(void)
{
    memset(&current, 0, sizeof(current));
    reference = (direction_t){0};
    have_time = collecting = away_holding = view_holding = have_event = calib_failed = false;
    current.status = WATCH_NOT_READY;
    return WATCH_OK;
}

watch_status_t wrist_wake_calibrate(uint32_t now_ms)
{
    clear_tracking();
    collecting = false;
    calib_failed = false;
    current.calib_state = WRIST_CALIB_PENDING;
    current.pose = WRIST_POSE_CALIBRATING;
    current.status = WATCH_NOT_READY;
    request_ms = now_ms;
    return WATCH_OK;
}

watch_status_t wrist_wake_process(uint32_t now_ms)
{
    if (current.calib_state == WRIST_CALIB_PENDING &&
        (uint32_t)(now_ms - request_ms) >= WATCH_WRIST_CALIB_TIMEOUT_MS) {
        collecting = false;
        clear_tracking();
        current.calib_state = WRIST_CALIB_IDLE;
        current.pose = WRIST_POSE_CALIB_FAILED;
        current.status = WATCH_TIMEOUT;
        calib_failed = true;
        if (current.fail_count != UINT32_MAX) { current.fail_count++; }
    }
    return current.status;
}

static void collect_reference(direction_t direction, bool quiet, uint32_t now_ms)
{
    current.status = WATCH_NOT_READY;
    current.pose = WRIST_POSE_CALIBRATING;
    if (!quiet) { collecting = false; return; }
    if (!collecting || dot(anchor, direction) < WATCH_WRIST_STABLE_COS) {
        anchor = direction;
        collect_ms = now_ms;
        collect_count = 0U;
        sum = (direction_t){0};
        collecting = true;
    }
    sum.x += direction.x; sum.y += direction.y; sum.z += direction.z;
    collect_count++;
    if ((uint32_t)(now_ms - collect_ms) >= WATCH_WRIST_CALIB_MS &&
        collect_count >= WATCH_WRIST_CALIB_MIN_SAMPLES) {
        float length = sqrtf(dot(sum, sum));
        reference = (direction_t){sum.x / length, sum.y / length, sum.z / length};
        collecting = false;
        clear_tracking();
        current.calib_state = WRIST_CALIB_DONE;
        current.pose = WRIST_POSE_WAIT_AWAY;
        current.in_view = true;
        current.status = WATCH_OK;
    }
}

watch_status_t wrist_wake_update(const motion_sample_t *sample, bool *wake)
{
    if (wake == NULL) { return WATCH_INVALID_ARGUMENT; }
    *wake = false;
    if (sample == NULL) { return WATCH_INVALID_ARGUMENT; }
    (void)wrist_wake_process(board_millis());
    if (!sample->valid) { return bad_sample(WATCH_NOT_READY, true); }
    if (!isfinite(sample->ax_g) || !isfinite(sample->ay_g) || !isfinite(sample->az_g) ||
        !isfinite(sample->gx_dps) || !isfinite(sample->gy_dps) || !isfinite(sample->gz_dps) ||
        fabsf(sample->ax_g) > 8.0f || fabsf(sample->ay_g) > 8.0f || fabsf(sample->az_g) > 8.0f) {
        return bad_sample(WATCH_INVALID_DATA, true);
    }
    if ((uint32_t)(board_millis() - sample->timestamp_ms) > WATCH_WRIST_MAX_GAP_MS) {
        return bad_sample(WATCH_INVALID_DATA, true);
    }
    if (have_time) {
        uint32_t delta = sample->timestamp_ms - last_sample_ms;
        if (delta == 0U || delta > INT32_MAX) { return bad_sample(WATCH_INVALID_DATA, false); }
        if (delta > WATCH_WRIST_MAX_GAP_MS) {
            /* 当前样本仅建立新时间基准；重新离开再返回才可触发。 */
            last_sample_ms = sample->timestamp_ms;
            return bad_sample(WATCH_NOT_READY, false);
        }
    }
    have_time = true;
    last_sample_ms = sample->timestamp_ms;
    if (calib_failed) { return current.status; }
    float magnitude = sqrtf(sample->ax_g * sample->ax_g + sample->ay_g * sample->ay_g +
                            sample->az_g * sample->az_g);
    float gyro_squared = sample->gx_dps * sample->gx_dps + sample->gy_dps * sample->gy_dps +
                         sample->gz_dps * sample->gz_dps;
    bool gravity_ok = magnitude >= WATCH_WRIST_GRAVITY_MIN_G && magnitude <= WATCH_WRIST_GRAVITY_MAX_G;
    if (!gravity_ok) {
        collecting = away_holding = view_holding = false;
        current.in_view = false;
        current.pose = WRIST_POSE_MOVING;
        current.status = WATCH_NOT_READY;
        return current.status;
    }
    direction_t direction = {sample->ax_g / magnitude, sample->ay_g / magnitude, sample->az_g / magnitude};
    if (current.calib_state == WRIST_CALIB_PENDING) {
        collect_reference(direction, gyro_squared <= WATCH_WRIST_CALIB_GYRO_DPS * WATCH_WRIST_CALIB_GYRO_DPS,
                          sample->timestamp_ms);
        return current.status;
    }
    if (current.calib_state != WRIST_CALIB_DONE) {
        current.pose = WRIST_POSE_UNSET;
        current.status = WATCH_NOT_READY;
        return current.status;
    }
    float alignment = dot(reference, direction);
    current.in_view = alignment >= WATCH_WRIST_VIEW_COS;
    current.status = WATCH_OK;
    if (have_event && (uint32_t)(sample->timestamp_ms - last_event_ms) < WATCH_WRIST_COOLDOWN_MS) {
        away_holding = view_holding = false;
        current.armed_state = WRIST_ARMED_IDLE;
        current.pose = WRIST_POSE_COOLDOWN;
        return WATCH_OK;
    }
    if (gyro_squared > WATCH_WRIST_STABLE_GYRO_DPS * WATCH_WRIST_STABLE_GYRO_DPS) {
        away_holding = view_holding = false;
        current.pose = WRIST_POSE_MOVING;
        return WATCH_OK;
    }
    if (alignment <= WATCH_WRIST_AWAY_COS) {
        view_holding = false;
        if (!away_holding || dot(away_anchor, direction) < WATCH_WRIST_STABLE_COS) {
            away_holding = true;
            away_anchor = direction;
            away_ms = sample->timestamp_ms;
        }
        if ((uint32_t)(sample->timestamp_ms - away_ms) >= WATCH_WRIST_HOLD_MS) {
            current.armed_state = WRIST_ARMED;
        }
        current.pose = current.armed_state == WRIST_ARMED ? WRIST_POSE_ARMED : WRIST_POSE_WAIT_AWAY;
    } else if (current.in_view && current.armed_state == WRIST_ARMED) {
        away_holding = false;
        if (!view_holding) { view_holding = true; view_ms = sample->timestamp_ms; }
        current.pose = WRIST_POSE_HOLD_VIEW;
        if ((uint32_t)(sample->timestamp_ms - view_ms) >= WATCH_WRIST_HOLD_MS) {
            *wake = true;
            have_event = true;
            last_event_ms = sample->timestamp_ms;
            clear_tracking();
            current.in_view = true;
            current.pose = WRIST_POSE_TRIGGERED;
            if (current.event_count != UINT32_MAX) { current.event_count++; }
        }
    } else {
        away_holding = view_holding = false;
        current.pose = current.armed_state == WRIST_ARMED ? WRIST_POSE_ARMED : WRIST_POSE_WAIT_AWAY;
    }
    return WATCH_OK;
}

watch_status_t wrist_wake_get_state(wrist_wake_status_t *status)
{
    if (status == NULL) { return WATCH_INVALID_ARGUMENT; }
    *status = current;
    return WATCH_OK;
}
