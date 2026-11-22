#ifndef WATCH_BATTERY_SERVICE_H
#define WATCH_BATTERY_SERVICE_H
#include "watch_types.h"
/* 电压法粗略估算，保持既有接口；无效输入清空输出。
 * 百分比不是容量测量，也不能据此推算剩余续航。 */
watch_status_t battery_service_update(uint16_t millivolts, battery_state_t *state);
typedef struct {
    battery_state_t battery;
    watch_status_t status;
    bool low;                    /* 仅battery.valid且low_known时可用于提示 */
    bool low_known;
    uint32_t timestamp_ms;
    uint32_t low_event_count;     /* 每次确认进入低电时递增，饱和不回绕 */
} battery_service_state_t;

void battery_service_init(void);
/* 由ADC任务推进，只在新的有效批次上确认低电/恢复，不重复累计同一样本。 */
watch_status_t battery_service_process(uint32_t now_ms);
/* 陈旧样本清为无效，但保留已确认的低电锁存用于恢复后的滞回。 */
watch_status_t battery_service_get_state(battery_service_state_t *state);
#endif
