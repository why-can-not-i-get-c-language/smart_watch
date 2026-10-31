#ifndef WATCH_PEDOMETER_H
#define WATCH_PEDOMETER_H
#include "watch_types.h"
/* 计步与按日汇总。未实现接口返回WATCH_NOT_IMPLEMENTED，输出无效。 */
watch_status_t pedometer_update(const motion_sample_t *sample, uint32_t *steps);
watch_status_t pedometer_reset(void);
#endif
