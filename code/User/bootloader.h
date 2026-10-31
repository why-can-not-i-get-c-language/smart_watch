#ifndef WATCH_BOOTLOADER_H
#define WATCH_BOOTLOADER_H
#include "watch_types.h"
/* 独立Boot目标及镜像恢复。未实现接口返回WATCH_NOT_IMPLEMENTED，输出无效。 */
watch_status_t bootloader_run(void);
#endif
