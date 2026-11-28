#include "activity.h"
#include "settings.h"
#include "tm6605.h"
#include "board_port.h"
#include <string.h>

enum { ALERT_MS=5000U, MOTOR_WAIT_MS=30000U, COMMAND_TIMEOUT_MS=1000U };
typedef enum { MOTOR_IDLE, MOTOR_START, MOTOR_RING, MOTOR_STOP } motor_phase_t;
static activity_state_t current;
static motor_phase_t motor_phase;
static bool have_baseline, store_seen, save_pending, last_valid, last_empty;
static bool test_active;
static uint32_t last_steps, last_ms, window_started_ms, pending_since, motor_since;
static uint32_t generation, candidate_minutes, save_generation, test_duration_ms;
static bool candidate_enabled;
static watch_status_t last_load_status;
static watch_status_t after_stop_status;

void activity_init(void)
{
    memset(&current, 0, sizeof(current));
    current.minutes = current.saved_minutes = 30U;
    current.store_status = WATCH_NOT_READY;
    motor_phase = MOTOR_IDLE;
    have_baseline = store_seen = save_pending = false;
    test_active = false;
    last_valid = last_empty = false;
    last_load_status = WATCH_NOT_READY;
    after_stop_status = WATCH_OK;
    generation = save_generation = candidate_minutes = test_duration_ms = 0U;
    candidate_enabled = false;
    last_steps = last_ms = window_started_ms = pending_since = motor_since = 0U;
}

static void sync_config(void)
{
    settings_state_t st;
    if (settings_get_state(&st) != WATCH_OK || !st.ready || st.busy) return;
    if (!save_pending && store_seen && generation == st.generation &&
        last_valid == st.valid && last_empty == st.empty && last_load_status == st.last_load_status) return;
    settings_config_t cfg;
    watch_status_t status = settings_config_load(&cfg);
    bool committed = !save_pending || (status == WATCH_OK && st.last_save_status == WATCH_OK &&
        st.generation == save_generation + 1U && cfg.activity_minutes == candidate_minutes &&
        cfg.activity_enabled == candidate_enabled);
    current.store_status = committed ? status : st.last_save_status == WATCH_OK ? WATCH_IO_ERROR : st.last_save_status;
    current.store_phase = !committed || status != WATCH_OK ? REMINDER_STORE_ERROR : st.empty ?
        REMINDER_STORE_DEFAULT : st.length < 24U ? REMINDER_STORE_LEGACY :
        save_pending ? REMINDER_STORE_SAVED : REMINDER_STORE_LOADED;
    save_pending = false;
    store_seen = true;
    generation = st.generation;
    last_valid = st.valid; last_empty = st.empty; last_load_status = st.last_load_status;
    current.config_ready = status == WATCH_OK;
    current.config_persisted = status == WATCH_OK && st.valid && st.length == 24U;
    if (status != WATCH_OK) {
        current.enabled = false;
        current.pending = false;
        have_baseline = false;
        if (current.owns_haptic) (void)activity_acknowledge();
        return;
    }
    current.saved_minutes = cfg.activity_minutes;
    current.saved_enabled = cfg.activity_enabled;
    if (current.minutes != cfg.activity_minutes || current.enabled != cfg.activity_enabled) {
        current.pending = false;
        have_baseline = false;
        current.latched = false;
        if (current.owns_haptic) (void)activity_acknowledge();
    }
    current.minutes = cfg.activity_minutes;
    current.enabled = cfg.activity_enabled;
}

watch_status_t activity_save_config(uint32_t minutes, bool enabled)
{
    if (save_pending) return WATCH_NOT_READY;
    settings_state_t st;
    watch_status_t status = settings_get_state(&st);
    if (status != WATCH_OK) return status;
    status = settings_config_save_activity(minutes, enabled);
    if (status == WATCH_OK) {
        save_pending = true;
        save_generation = st.generation;
        candidate_minutes = minutes;
        candidate_enabled = enabled;
        current.store_phase = REMINDER_STORE_SAVING;
        current.store_status = WATCH_NOT_READY;
    }
    return status;
}

watch_status_t activity_start_test(uint32_t seconds)
{
    if (seconds == 0U || seconds > 60U) return WATCH_INVALID_ARGUMENT;
    if (!current.config_ready || save_pending || current.owns_haptic || current.pending) {
        return WATCH_NOT_READY;
    }
    test_duration_ms = seconds * 1000U;
    test_active = true;
    current.test_active = true;
    current.latched = false;
    current.pending = false;
    current.motor_status = WATCH_OK;
    have_baseline = false;
    return WATCH_OK;
}

watch_status_t activity_reload_settings(void)
{
    if (save_pending) return WATCH_NOT_READY;
    watch_status_t status = settings_init();
    if (status == WATCH_OK) {
        store_seen = false;
        current.config_ready = false;
        current.store_phase = REMINDER_STORE_LOADING;
        current.store_status = WATCH_NOT_READY;
        have_baseline = false;
        if (current.owns_haptic) (void)activity_acknowledge();
        current.pending = false;
    }
    return status;
}

watch_status_t activity_update(uint32_t now_ms, uint32_t steps, bool *remind)
{
    if (remind == NULL) return WATCH_INVALID_ARGUMENT;
    *remind = false;
    if ((!current.enabled && !test_active) || !current.config_ready) return WATCH_NOT_READY;
    if (!have_baseline || steps != last_steps || (uint32_t)(now_ms - last_ms) > 1000U) {
        have_baseline = true;
        window_started_ms = now_ms;
        current.latched = false;
        current.pending = false;
        current.motor_status = WATCH_OK;
    }
    last_steps = steps;
    last_ms = now_ms;
    current.idle_ms = now_ms - window_started_ms;
    uint32_t limit_ms = test_active ? test_duration_ms : current.minutes * 60000U;
    if (!current.latched && current.idle_ms >= limit_ms) {
        current.latched = true;
        current.pending = true;
        pending_since = now_ms;
        *remind = true;
    }
    return WATCH_OK;
}

watch_status_t activity_acknowledge(void)
{
    current.pending = false;
    current.latched = true;
    if (!current.owns_haptic || motor_phase == MOTOR_STOP) return WATCH_OK;
    watch_status_t status = tm6605_stop();
    current.motor_status = status;
    if (status == WATCH_OK) {
        motor_phase = MOTOR_STOP;
        motor_since = board_millis();
        after_stop_status = WATCH_OK;
    }
    return status;
}

watch_status_t activity_process(uint32_t now_ms, bool sensor_valid, uint32_t steps, bool allow_haptic)
{
    sync_config();
    current.test_active = test_active;
    bool due = false;
    if (!sensor_valid || (!current.enabled && !test_active) || !current.config_ready) {
        have_baseline = false;
        current.idle_ms = 0U;
        current.pending = false;
        current.latched = false;
    } else (void)activity_update(now_ms, steps, &due);
    (void)due;
    if (current.owns_haptic && (!sensor_valid || !current.enabled || !current.config_ready ||
        !current.latched || !allow_haptic)) {
        bool keep_latched = current.latched;
        (void)activity_acknowledge();
        if (!keep_latched) current.latched = false;
    }

    tm6605_state_t motor;
    watch_status_t status = tm6605_get_state(&motor);
    if (current.owns_haptic) {
        if (status != WATCH_OK || (!motor.busy && motor.status != WATCH_OK)) {
            current.motor_status = status != WATCH_OK ? status : motor.status;
            current.owns_haptic = false;
            motor_phase = MOTOR_IDLE;
        } else if (motor_phase == MOTOR_STOP) {
            if (!motor.busy) {
                current.motor_status = motor.command == TM6605_STOP ? after_stop_status : WATCH_IO_ERROR;
                current.owns_haptic = false;
                motor_phase = MOTOR_IDLE;
            } else if ((uint32_t)(now_ms - motor_since) >= COMMAND_TIMEOUT_MS) {
                /* 未确认停止仍持有所有权；用户确认可重新发送停止。 */
                current.motor_status = WATCH_TIMEOUT;
                motor_phase = MOTOR_RING;
            }
        } else if ((uint32_t)(now_ms - motor_since) >= ALERT_MS) {
            (void)activity_acknowledge();
        } else if (motor_phase == MOTOR_START) {
            if (!motor.busy && motor.alert_active && motor.command == TM6605_ALERT) motor_phase = MOTOR_RING;
            else if ((uint32_t)(now_ms - motor_since) >= COMMAND_TIMEOUT_MS) {
                (void)activity_acknowledge();
                after_stop_status = WATCH_TIMEOUT;
                current.motor_status = WATCH_TIMEOUT;
            }
        } else if (!motor.busy && !motor.alert_active) {
            current.owns_haptic = false;
            motor_phase = MOTOR_IDLE;
        }
    }
    if (current.pending && !current.owns_haptic) {
        if ((uint32_t)(now_ms - pending_since) >= MOTOR_WAIT_MS) {
            current.pending = false;
            current.motor_status = WATCH_TIMEOUT;
        } else if (allow_haptic && status == WATCH_OK && motor.ready && !motor.busy && !motor.alert_active) {
            status = tm6605_alert(ALERT_MS);
            if (status == WATCH_OK) {
                current.pending = false;
                current.owns_haptic = true;
                motor_phase = MOTOR_START;
                motor_since = now_ms;
                current.motor_status = WATCH_OK;
                if (current.notification_count != UINT32_MAX) ++current.notification_count;
            } else if (status != WATCH_NOT_READY) {
                current.pending = false;
                current.motor_status = status;
            }
        }
    }
    if (test_active && !current.pending && !current.owns_haptic && current.latched) {
        test_active = false;
        current.test_active = false;
    }
    current.phase = current.owns_haptic ? (motor_phase == MOTOR_STOP ? ACTIVITY_STOPPING :
        motor_phase == MOTOR_START ? ACTIVITY_STARTING : ACTIVITY_RINGING) :
        !current.config_ready ? (current.store_phase == REMINDER_STORE_LOADING ? ACTIVITY_LOADING : ACTIVITY_ERROR) :
        !current.enabled ? ACTIVITY_OFF :
        !sensor_valid ? ACTIVITY_WAIT_SENSOR : current.pending ? ACTIVITY_WAIT_MOTOR :
        current.motor_status != WATCH_OK ? ACTIVITY_ERROR : current.latched ? ACTIVITY_NOTIFIED : ACTIVITY_TRACKING;
    return current.motor_status;
}

watch_status_t activity_get_state(activity_state_t *state)
{
    if (state == NULL) return WATCH_INVALID_ARGUMENT;
    *state = current;
    return WATCH_OK;
}
