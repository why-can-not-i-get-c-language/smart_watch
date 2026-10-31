#ifndef WATCH_BOARD_PORT_H
#define WATCH_BOARD_PORT_H
#include "watch_types.h"
/* HAL端口、时基与STOP恢复。未实现接口返回WATCH_NOT_IMPLEMENTED，输出无效。 */
watch_status_t board_init(void);
uint32_t board_millis(void);
watch_status_t board_enter_stop(void);
#endif
