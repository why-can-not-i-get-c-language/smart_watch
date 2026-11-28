#ifndef WATCH_SETTINGS_H
#define WATCH_SETTINGS_H
#include "watch_types.h"
#include <stdbool.h>
#define SETTINGS_MAX_PAYLOAD 128U
#define SETTINGS_REMINDER_MIN_SECONDS 30U
#define SETTINGS_REMINDER_MAX_SECONDS 300U
#define SETTINGS_REMINDER_STEP_SECONDS 30U
#define SETTINGS_ACTIVITY_MIN_MINUTES 5U
#define SETTINGS_ACTIVITY_MAX_MINUTES 120U
#define SETTINGS_ACTIVITY_STEP_MINUTES 5U
/* 共享载荷v3：版本3、原计数、提醒秒、低活动分钟、使能0/1、保留0，
 * 各为小端32位。兼容v1的8字节计数及v2的16字节提醒载荷。
 * 旧格式低活动默认30分钟/关闭；仅显式保存时升级。
 * 不支持的版本/长度/保留值返回错误，属性保存不得覆盖未知载荷。 */
typedef struct {
    uint32_t diagnostic_counter;
    uint32_t reminder_delay_seconds;
    uint32_t activity_minutes;
    bool activity_enabled;
} settings_config_t;
/* 空白介质返回默认值及OK；是否已保存须查看settings_state.valid/empty。
 * 读取失败时输出全零。属性保存仍为异步，并保留另一属性的最新已提交值。 */
watch_status_t settings_config_load(settings_config_t *config);
watch_status_t settings_config_save_counter(uint32_t counter);
watch_status_t settings_config_save_reminder(uint32_t delay_seconds);
watch_status_t settings_config_save_activity(uint32_t minutes, bool enabled);
/* 一笔事务保存全部提醒配置，保留最新诊断计数；OK仍仅表示入队。 */
watch_status_t settings_config_save_reminders(uint32_t delay_seconds, uint32_t minutes, bool enabled);
/* F07-P02双槽参数持久化状态 */
typedef struct {
    bool ready;
    bool busy;
    bool valid;
    bool empty;
    uint32_t generation;
    uint16_t length;
    watch_status_t last_load_status;
    watch_status_t last_save_status;
} settings_state_t;
/* settings_load返回WATCH_OK表示读取合法缓存；NOT_READY可能为未就绪、
 * 介质不可用或真正空白，须以state.empty区分；其他错误保留原错误码。
 * 失败时输出无效。settings_save异步排队，
 * WATCH_OK表示请求已接受并立即复制数据，非已落盘；最终状态通过
 * settings_get_operation_status查询。非空未知数据拒绝保存。 */
watch_status_t settings_load(void *data, size_t length);
watch_status_t settings_save(const void *data, size_t length);
watch_status_t settings_get_operation_status(void);
watch_status_t settings_get_state(settings_state_t *state);
/* 初始化/显式重新读取及5ms推进。内部持有Flash参数锁，忙时拒绝重置。
 * settings_load只读缓存，不发SPI；空白和读取失败通过state.empty区分。
 * 代号按uint32_t回绕；未知非空槽保留并拒绝作为保存目标。 */
watch_status_t settings_init(void);
watch_status_t settings_process(uint32_t now_ms);
#endif
