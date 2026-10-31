#ifndef WATCH_TIME_SERVICE_H
#define WATCH_TIME_SERVICE_H
#include "watch_types.h"
/* UTC时间与有效性。未实现接口返回WATCH_NOT_IMPLEMENTED，输出无效。 */
watch_status_t time_service_get(uint32_t *utc_seconds);
watch_status_t time_service_set(uint32_t utc_seconds);
#endif
