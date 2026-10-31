#ifndef WATCH_TM6605_H
#define WATCH_TM6605_H
#include "watch_types.h"
/* 统一LRA振动套装驱动。未实现接口返回WATCH_NOT_IMPLEMENTED，输出无效。 */
watch_status_t tm6605_init(void);
watch_status_t tm6605_play(uint8_t effect);
watch_status_t tm6605_stop(void);
watch_status_t tm6605_sleep(bool enabled);
#endif
