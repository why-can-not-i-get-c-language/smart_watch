#ifndef WATCH_BUTTONS_H
#define WATCH_BUTTONS_H
#include "watch_types.h"

/* 按键事件掩码 */
#define BUTTON_BOARD_PRESS   (1U << 0)
#define BUTTON_BOARD_RELEASE (1U << 1)
#define BUTTON_BOARD_SHORT   (1U << 2)
#define BUTTON_BOARD_LONG    (1U << 3)

#define BUTTON_MENU_PRESS    (1U << 4)
#define BUTTON_MENU_RELEASE  (1U << 5)
#define BUTTON_MENU_SHORT    (1U << 6)
#define BUTTON_MENU_LONG     (1U << 7)

#define BUTTON_SOS_PRESS     (1U << 8)
#define BUTTON_SOS_RELEASE   (1U << 9)
#define BUTTON_SOS_SHORT     (1U << 10)
#define BUTTON_SOS_LONG      (1U << 11)

/* 按键初始化：配置GPIO为内部上拉输入 */
watch_status_t buttons_init(void);

/* 每5ms轮询，各键4位事件可组合，释放可同时包含RELEASE和SHORT。
 * 开机先稳定释放30ms后接收按键；长按1000ms仅触发一次。
 * 无事件或失败时输出0，未初始化返回WATCH_NOT_READY。 */
watch_status_t buttons_poll(uint32_t now_ms, uint32_t *events);

#endif
