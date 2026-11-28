#ifndef WATCH_ACTIVITY_H
#define WATCH_ACTIVITY_H
#include "watch_types.h"
#include "reminder.h"
typedef enum {
    ACTIVITY_OFF=0, ACTIVITY_WAIT_SENSOR, ACTIVITY_TRACKING, ACTIVITY_WAIT_MOTOR,
    ACTIVITY_STARTING, ACTIVITY_RINGING, ACTIVITY_STOPPING, ACTIVITY_NOTIFIED, ACTIVITY_ERROR,
    ACTIVITY_LOADING
} activity_phase_t;
typedef struct {
    activity_phase_t phase;
    uint32_t minutes, idle_ms, notification_count;
    bool enabled, latched, owns_haptic, pending;
    reminder_store_phase_t store_phase;
    watch_status_t store_status, motor_status;
    uint32_t saved_minutes;
    bool saved_enabled, config_ready, config_persisted;
} activity_state_t;
void activity_init(void);
/* 单调时钟窗口。steps是当前会话累计；增加/清零重建窗口。
 * 一轮低活动只输出一次remind，不按调用次数累计时间。 */
watch_status_t activity_update(uint32_t now_ms, uint32_t steps, bool *remind);
/* 每5ms调用。无效传感器中断观察连续性；allow_haptic=false为更高优先级占用。 */
watch_status_t activity_process(uint32_t now_ms, bool sensor_valid, uint32_t steps, bool allow_haptic);
watch_status_t activity_acknowledge(void);
watch_status_t activity_save_config(uint32_t minutes, bool enabled);
watch_status_t activity_reload_settings(void);
watch_status_t activity_get_state(activity_state_t *state);
#endif
