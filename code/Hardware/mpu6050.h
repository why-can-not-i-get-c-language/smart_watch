#ifndef WATCH_MPU6050_H
#define WATCH_MPU6050_H
#include "watch_types.h"
/* 传感器读数按配置换算为g和度/秒；时间戳为主控开始读取的毫秒时刻。
 * process推进初始化及采样，每次至多一次有超时的总线操作；INT无需接线。 */
typedef struct {
    watch_status_t status;
    bool ready, identity_read;
    uint8_t who_am_i;
    motion_sample_t sample;
    uint32_t sample_count, error_count, gap_count;
    uint32_t last_interval_ms, actual_rate_millihz;
    bool rate_valid;
} mpu6050_state_t;
watch_status_t mpu6050_init(void);
watch_status_t mpu6050_process(uint32_t now_ms);
void mpu6050_get_state(mpu6050_state_t *state);
/* 只取缓存，每个新样本最多成功领取一次；无新样本返回NOT_READY并清空输出。 */
watch_status_t mpu6050_read(motion_sample_t *sample);
/* 低功耗留后续任务；本接口当前返回NOT_IMPLEMENTED。 */
watch_status_t mpu6050_set_low_power(bool enabled);
#endif
