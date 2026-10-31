#ifndef WATCH_HEALTH_H
#define WATCH_HEALTH_H
#include "watch_types.h"
/* 静止心率、估算血氧与质量。未实现接口返回WATCH_NOT_IMPLEMENTED，输出无效。 */
watch_status_t health_update(const ppg_sample_t *sample, health_result_t *result);
#endif
