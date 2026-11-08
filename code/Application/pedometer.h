#ifndef WATCH_PEDOMETER_H
#define WATCH_PEDOMETER_H
#include "watch_types.h"

/* 计步状态：无样本、预热中、可用、清零完成 */
typedef enum {
    PEDOMETER_NO_SAMPLE = 0,    /* 尚未收到有效样本 */
    PEDOMETER_WARMING = 1,      /* 预热中：建立重力基线 */
    PEDOMETER_READY = 2,        /* 可用：检测步数 */
    PEDOMETER_CLEARED = 3,      /* 清零完成；下一有效样本重新预热 */
    PEDOMETER_VERIFYING = 4     /* 已有候选，尚未满足连续节律确认 */
} pedometer_state_t;

/* 获取当前计步状态和累计步数 */
typedef struct {
    pedometer_state_t state;
    uint32_t step_count;
    uint32_t last_update_ms;
    uint8_t pending_steps;     /* 未确认候选，不计入step_count */
    float signal_g;            /* 模长平滑、去基线后的处理值，不是原始测量 */
} pedometer_status_t;

/* steps始终输出本次开机累计值，包括预热/坏样本；不依赖RTC，不保存到Flash。
 * 每个新样本只调用一次；时间重复/倒序、过期或间隙不产生步数。
 * 完整波形与连续节律确认后才补记候选；短程及相似节律晃动仍有识别限制。 */
watch_status_t pedometer_update(const motion_sample_t *sample, uint32_t *steps);
watch_status_t pedometer_reset(void);
watch_status_t pedometer_get_state(pedometer_status_t *status);
#endif
