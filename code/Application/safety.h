#ifndef WATCH_SAFETY_H
#define WATCH_SAFETY_H
#include "watch_types.h"
/* SOS与跌倒人工确认。未实现接口返回WATCH_NOT_IMPLEMENTED，输出无效。 */
watch_status_t safety_handle_buttons(uint32_t events, uint32_t now_ms);
watch_status_t safety_update_motion(const motion_sample_t *sample);
watch_status_t safety_cancel(void);
#endif
