#ifndef WATCH_BATTERY_SERVICE_H
#define WATCH_BATTERY_SERVICE_H
#include "watch_types.h"
/* 估算电量和滞回。未实现接口返回WATCH_NOT_IMPLEMENTED，输出无效。 */
watch_status_t battery_service_update(uint16_t millivolts, battery_state_t *state);
#endif
