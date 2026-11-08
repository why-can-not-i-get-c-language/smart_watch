#ifndef WATCH_WRIST_WAKE_H
#define WATCH_WRIST_WAKE_H
#include "watch_types.h"

/* 标定状态：未标定、待确认中、已标定 */
typedef enum {
    WRIST_CALIB_IDLE = 0,       /* 未标定 */
    WRIST_CALIB_PENDING = 1,    /* 正在收集参考样本 */
    WRIST_CALIB_DONE = 2,       /* 已标定 */
} wrist_calib_state_t;

/* 武装状态：未武装、已武装 */
typedef enum {
    WRIST_ARMED_IDLE = 0,
    WRIST_ARMED = 1,
} wrist_armed_state_t;

typedef enum {
    WRIST_POSE_UNSET, WRIST_POSE_CALIBRATING, WRIST_POSE_WAIT_AWAY,
    WRIST_POSE_ARMED, WRIST_POSE_HOLD_VIEW, WRIST_POSE_MOVING,
    WRIST_POSE_COOLDOWN, WRIST_POSE_TRIGGERED, WRIST_POSE_SENSOR_ERROR,
    WRIST_POSE_CALIB_FAILED
} wrist_pose_state_t;

typedef struct {
    wrist_calib_state_t calib_state;
    wrist_armed_state_t armed_state;
    wrist_pose_state_t pose;
    watch_status_t status;
    bool in_view;
    uint32_t event_count;       /* 成功wake次数 */
    uint32_t fail_count;        /* 标定失败次数 */
} wrist_wake_status_t;

/* 保留wrist_wake_update(sample, wake)签名。
 * 始终先wake=false；NULL/无效/非有限/过期/重复倒序/长间隙均不触发。
 * 处理uint32回绕。仅在标定完成且满足条件时输出wake=true。 */
watch_status_t wrist_wake_update(const motion_sample_t *sample, bool *wake);

/* 开始替换本次开机参考，稳定采样后才完成；接受请求不等于标定成功。 */
watch_status_t wrist_wake_calibrate(uint32_t now_ms);

/* 初始化/主动复位：清除参考、瞬态和计数。掉线只由update清瞬态，保留参考。 */
watch_status_t wrist_wake_reset(void);

/* 无新样本时仍推进标定超时，无总线访问。 */
watch_status_t wrist_wake_process(uint32_t now_ms);

/* 返回当前状态与诊断计数 */
watch_status_t wrist_wake_get_state(wrist_wake_status_t *status);
#endif
