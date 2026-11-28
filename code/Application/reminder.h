#ifndef WATCH_REMINDER_H
#define WATCH_REMINDER_H
#include "watch_types.h"
/* 一次性UTC提醒。延时配置保存到Flash，到时目标仅RAM且重启不自动设定。 */
typedef enum {
    REMINDER_STORE_LOADING=0, REMINDER_STORE_DEFAULT, REMINDER_STORE_LEGACY,
    REMINDER_STORE_LOADED, REMINDER_STORE_SAVING, REMINDER_STORE_SAVED, REMINDER_STORE_ERROR
} reminder_store_phase_t;
typedef enum {
    REMINDER_OFF=0, REMINDER_ARMED, REMINDER_STARTING, REMINDER_RINGING,
    REMINDER_STOPPING, REMINDER_DONE, REMINDER_CANCELLED, REMINDER_MISSED, REMINDER_ERROR
} reminder_phase_t;
typedef struct {
    reminder_phase_t phase;
    watch_status_t status;
    uint32_t target_utc, trigger_count;
    bool owns_haptic;
    reminder_store_phase_t store_phase;
    watch_status_t store_status;
    uint32_t saved_delay_seconds, settings_generation;
    bool store_ready, delay_persisted;
} reminder_state_t;
/* 仅启动时调用，不发送马达命令。 */
void reminder_init(void);
/* 必须有真实有效RTC且目标严格晚于当前时间；无效请求不覆盖原提醒。
 * 正在振动/停止时拒绝重设，须先取消；不修改RTC。 */
watch_status_t reminder_set(uint32_t utc_seconds);
/* 参数由time_service_get提供；0表示读取失败。内部仍核对真实RTC。
 * 超过目标5秒才被调度到时标MISSED，避免校时大幅前跳后补振。
 * 10秒短段序列只请求一次；入队与驱动ACK、物理振动分开判断。 */
watch_status_t reminder_update(uint32_t utc_seconds);
/* ARMED直接取消；持有马达时发一次停止并等待ACK，失败不宣称已停止。 */
watch_status_t reminder_cancel(void);
watch_status_t reminder_get_state(reminder_state_t *state);
/* 保存参数不改变已设置的target；返回OK只表示排队，最终查store_phase。
 * 显式读回会重新扫描参数槽，与其他Flash操作互斥，不更改RTC或设定提醒。 */
watch_status_t reminder_save_delay(uint32_t delay_seconds);
watch_status_t reminder_reload_settings(void);
#endif
