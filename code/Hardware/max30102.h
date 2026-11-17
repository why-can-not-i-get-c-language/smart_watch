#ifndef WATCH_MAX30102_H
#define WATCH_MAX30102_H
#include "watch_types.h"
/* 原始PPG读取；时间戳为主控开始读取该批FIFO的时刻，不是传感器转换时刻。
 * valid只表示原始传输有效，不代表已贴肤、心率有效或血氧估算有效。
 * process每次至多一个I2C事务，INT不参与本步轮询。 */
typedef struct {
    watch_status_t status;
    bool ready, identity_read;
    uint8_t part_id, revision_id;
    ppg_sample_t sample;
    uint32_t sample_count, error_count, overflow_count;
    /* 包含溢出计数读数和主动丢弃的FIFO样本；饱和计数只能给出下界。 */
    uint32_t lost_samples_lower_bound, queue_drops;
    uint32_t actual_rate_millihz;
    bool rate_valid, low_signal, saturated;
} max30102_state_t;
watch_status_t max30102_init(void);
watch_status_t max30102_process(uint32_t now_ms);
void max30102_get_state(max30102_state_t *state);
/* 顺序领取缓存中的新样本；无数据或样本过期时清空输出。 */
watch_status_t max30102_read(ppg_sample_t *sample);
/* 外设休眠及恢复属于F09，本步保留NOT_IMPLEMENTED。 */
watch_status_t max30102_shutdown(bool enabled);
#endif
