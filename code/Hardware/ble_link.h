#ifndef WATCH_BLE_LINK_H
#define WATCH_BLE_LINK_H
#include "watch_types.h"
/* BLE透传与链路状态。未实现接口返回WATCH_NOT_IMPLEMENTED，输出无效。 */
watch_status_t ble_link_init(void);
watch_status_t ble_link_send(const uint8_t *data, size_t length);
watch_status_t ble_link_connected(bool *connected);
#endif
