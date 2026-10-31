#ifndef WATCH_MAX30102_H
#define WATCH_MAX30102_H
#include "watch_types.h"
/* PPG采样与低功耗。未实现接口返回WATCH_NOT_IMPLEMENTED，输出无效。 */
watch_status_t max30102_init(void);
watch_status_t max30102_read(ppg_sample_t *sample);
watch_status_t max30102_shutdown(bool enabled);
#endif
