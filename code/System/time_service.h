#ifndef WATCH_TIME_SERVICE_H
#define WATCH_TIME_SERVICE_H
#include "watch_types.h"

/* UTC日历，只支持2000-01-01至2099-12-31；不包含时区转换。 */
typedef struct {
    uint16_t year;
    uint8_t month, day, hour, minute, second;
} rtc_calendar_t;
typedef enum { TIME_CLOCK_NONE = 0, TIME_CLOCK_LSE } time_clock_source_t;

/* 上电初始化一次；LSE等待最多5秒，无可用时钟时明确返回错误。 */
watch_status_t time_service_init(void);
/* 输出Unix UTC秒；错误或未设置时先清零输出。 */
watch_status_t time_service_get(uint32_t *utc_seconds);
/* 设置成功后才置备份域有效标记；不支持范围外日期。 */
watch_status_t time_service_set(uint32_t utc_seconds);
watch_status_t time_service_invalidate(void);
time_clock_source_t time_service_clock_source(void);

/* 纯日历换算在主机及目标上共用；失败输出全零/返回INVALID_ARGUMENT。 */
watch_status_t time_utc_to_calendar(uint32_t utc_seconds, rtc_calendar_t *cal);
/* 非法日历返回0；合法支持范围内不会出现0秒。 */
uint32_t time_calendar_to_utc(const rtc_calendar_t *cal);

/* 固定北京时间UTC+8，无夏令时；以下转换不读写硬件RTC。 */
/* UTC范围仍受RTC限制；本地显示上界可到2100-01-01 07:59:59。 */
watch_status_t time_utc_to_local(uint32_t utc, rtc_calendar_t *out);
/* 本地编辑范围2000至2099年；2000-01-01 08:00前不在RTC支持范围内。
 * 失败清零utc输出，不写RTC。 */
watch_status_t time_local_to_utc(const rtc_calendar_t *local, uint32_t *utc);
#endif
