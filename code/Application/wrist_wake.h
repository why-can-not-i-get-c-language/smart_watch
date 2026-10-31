#ifndef WATCH_WRIST_WAKE_H
#define WATCH_WRIST_WAKE_H
#include "watch_types.h"
/* 抬腕姿态及冷却。未实现接口返回WATCH_NOT_IMPLEMENTED，输出无效。 */
watch_status_t wrist_wake_update(const motion_sample_t *sample, bool *wake);
#endif
