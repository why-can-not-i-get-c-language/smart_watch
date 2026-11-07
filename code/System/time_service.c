#include "time_service.h"
#include <string.h>

#define UTC_MIN 946684800UL
#define UTC_MAX 4102444799UL

static bool leap(uint16_t year)
{
    return (year % 4U == 0U && year % 100U != 0U) || year % 400U == 0U;
}

static uint8_t month_days(uint16_t year, uint8_t month)
{
    static const uint8_t days[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (month < 1U || month > 12U) { return 0U; }
    return month == 2U && leap(year) ? 29U : days[month - 1U];
}

uint32_t time_calendar_to_utc(const rtc_calendar_t *cal)
{
    if (cal == NULL || cal->year < 2000U || cal->year > 2099U ||
        cal->month < 1U || cal->month > 12U || cal->day < 1U ||
        cal->day > month_days(cal->year, cal->month) || cal->hour > 23U ||
        cal->minute > 59U || cal->second > 59U) { return 0U; }
    uint32_t days = cal->day - 1U;
    for (uint16_t year = 2000U; year < cal->year; ++year) { days += leap(year) ? 366U : 365U; }
    for (uint8_t month = 1U; month < cal->month; ++month) { days += month_days(cal->year, month); }
    return UTC_MIN + days * 86400UL + (uint32_t)cal->hour * 3600UL +
           (uint32_t)cal->minute * 60UL + cal->second;
}

/* 北京时间(UTC+8)转换：UTC -> 本地显示时间 */
watch_status_t time_utc_to_local(uint32_t utc, rtc_calendar_t *out)
{
    if (out == NULL) { return WATCH_INVALID_ARGUMENT; }
    memset(out, 0, sizeof(*out));
    if (utc > UTC_MAX) { return WATCH_INVALID_ARGUMENT; }
    /* 先转换UTC，验证有效性 */
    if (time_utc_to_calendar(utc, out) != WATCH_OK) { return WATCH_INVALID_ARGUMENT; }
    /* 加8小时处理进位 */
    out->hour += 8U;
    if (out->hour >= 24U) {
        out->hour -= 24U;
        out->day += 1U;
        if (out->day > month_days(out->year, out->month)) {
            out->day = 1U;
            out->month += 1U;
            if (out->month > 12U) {
                out->month = 1U;
                out->year += 1U;
                if (out->year > 2099U) { out->year = 2100U; }
            }
        }
    }
    return WATCH_OK;
}

/* 本地时间(UTC+8) -> UTC */
watch_status_t time_local_to_utc(const rtc_calendar_t *local, uint32_t *utc)
{
    if (utc == NULL) { return WATCH_INVALID_ARGUMENT; }
    *utc = 0U;
    if (local == NULL) { return WATCH_INVALID_ARGUMENT; }
    if (local->year < 2000U || local->year > 2099U) { return WATCH_INVALID_ARGUMENT; }
    /* 本地2000-01-01 08:00前早于RTC支持的UTC起点。 */
    if (local->year == 2000U && local->month == 1U && local->day == 1U && local->hour < 8U) {
        return WATCH_INVALID_ARGUMENT;
    }
    uint32_t local_utc = time_calendar_to_utc(local);
    if (local_utc == 0U) { return WATCH_INVALID_ARGUMENT; }
    if (local_utc < 28800UL) { return WATCH_INVALID_ARGUMENT; }
    uint32_t result = local_utc - 28800UL;
    if (result < UTC_MIN) { return WATCH_INVALID_ARGUMENT; }
    *utc = result;
    return WATCH_OK;
}

watch_status_t time_utc_to_calendar(uint32_t utc, rtc_calendar_t *cal)
{
    if (cal == NULL) { return WATCH_INVALID_ARGUMENT; }
    memset(cal, 0, sizeof(*cal));
    if (utc < UTC_MIN || utc > UTC_MAX) { return WATCH_INVALID_ARGUMENT; }
    uint32_t days = (utc - UTC_MIN) / 86400UL;
    uint32_t seconds = utc % 86400UL;
    cal->year = 2000U;
    while (days >= (leap(cal->year) ? 366U : 365U)) {
        days -= leap(cal->year) ? 366U : 365U;
        ++cal->year;
    }
    cal->month = 1U;
    while (days >= month_days(cal->year, cal->month)) {
        days -= month_days(cal->year, cal->month);
        ++cal->month;
    }
    cal->day = (uint8_t)(days + 1U);
    cal->hour = (uint8_t)(seconds / 3600U);
    cal->minute = (uint8_t)(seconds / 60U % 60U);
    cal->second = (uint8_t)(seconds % 60U);
    return WATCH_OK;
}

#ifdef WATCH_TARGET_STM32
#include "board_config.h"
#include "stm32f4xx_hal.h"

/* DR0/DR1由本驱动独占；反码避免将部分写入当成有效时间。 */
#define VALID_MARK 0x54494D31UL
#define RTC_PRESCALER ((127UL << 16U) | 255UL)
static RTC_HandleTypeDef rtc_handle;
static bool initialized;
static bool cached;
static uint32_t cached_tick, cached_utc;

static watch_status_t hal_result(HAL_StatusTypeDef status)
{
    return status == HAL_OK ? WATCH_OK : status == HAL_TIMEOUT ? WATCH_TIMEOUT : WATCH_IO_ERROR;
}

static void clear_mark(void)
{
    HAL_RTCEx_BKUPWrite(&rtc_handle, RTC_BKP_DR0, 0U);
    HAL_RTCEx_BKUPWrite(&rtc_handle, RTC_BKP_DR1, 0U);
    cached = false;
}

time_clock_source_t time_service_clock_source(void)
{
    uint32_t bdcr = RCC->BDCR;
    uint32_t required = RCC_BDCR_RTCEN | RCC_BDCR_LSEON | RCC_BDCR_LSERDY;
    if (!initialized || (bdcr & required) != required ||
        (bdcr & RCC_BDCR_RTCSEL) != RCC_RTCCLKSOURCE_LSE ||
        (bdcr & RCC_BDCR_LSEBYP) != 0U) { return TIME_CLOCK_NONE; }
    return TIME_CLOCK_LSE;
}

watch_status_t time_service_init(void)
{
    if (initialized) { return time_service_clock_source() == TIME_CLOCK_LSE ? WATCH_OK : WATCH_IO_ERROR; }
    _Static_assert(WATCH_LSE_HZ == 32768UL, "RTC prescaler requires 32768Hz LSE");
    rtc_handle.Instance = RTC;
    __HAL_RCC_PWR_CLK_ENABLE();
    HAL_PWR_EnableBkUpAccess();
    uint32_t start = HAL_GetTick();
    while ((PWR->CR & PWR_CR_DBP) == 0U) {
        if ((uint32_t)(HAL_GetTick() - start) >= 100U) { return WATCH_TIMEOUT; }
    }
    uint32_t bdcr = RCC->BDCR;
    uint32_t required = RCC_BDCR_RTCEN | RCC_BDCR_LSEON | RCC_BDCR_LSERDY;
    bool compatible = (bdcr & required) == required &&
        (bdcr & RCC_BDCR_RTCSEL) == RCC_RTCCLKSOURCE_LSE &&
        (bdcr & RCC_BDCR_LSEBYP) == 0U &&
        (RTC->PRER & (RTC_PRER_PREDIV_A | RTC_PRER_PREDIV_S)) == RTC_PRESCALER &&
        (RTC->CR & (RTC_CR_FMT | RTC_CR_BYPSHAD | RTC_CR_OSEL)) == 0U;
    if (!compatible) {
        /* 不兼容或时钟曾停止时，不能继续相信旧的日期及有效标记。 */
        __HAL_RCC_BACKUPRESET_FORCE();
        __HAL_RCC_BACKUPRESET_RELEASE();
    }
    __HAL_RCC_LSE_CONFIG(RCC_LSE_ON);
    start = HAL_GetTick();
    while (__HAL_RCC_GET_FLAG(RCC_FLAG_LSERDY) == 0U) {
        if ((uint32_t)(HAL_GetTick() - start) >= 5000U) { return WATCH_TIMEOUT; }
    }
    __HAL_RCC_RTC_CONFIG(RCC_RTCCLKSOURCE_LSE);
    __HAL_RCC_RTC_ENABLE();
    rtc_handle.Init.HourFormat = RTC_HOURFORMAT_24;
    rtc_handle.Init.AsynchPrediv = 127U;
    rtc_handle.Init.SynchPrediv = 255U;
    rtc_handle.Init.OutPut = RTC_OUTPUT_DISABLE;
    rtc_handle.Init.OutPutPolarity = RTC_OUTPUT_POLARITY_HIGH;
    rtc_handle.Init.OutPutType = RTC_OUTPUT_TYPE_OPENDRAIN;
    rtc_handle.Lock = HAL_UNLOCKED;
    HAL_StatusTypeDef status;
    if (compatible) {
        /* INITS在2000年为0，不能据它把已经运行的RTC再次初始化。 */
        rtc_handle.State = HAL_RTC_STATE_READY;
        status = HAL_RTC_WaitForSynchro(&rtc_handle);
    } else {
        rtc_handle.State = HAL_RTC_STATE_RESET;
        clear_mark();
        status = HAL_RTC_Init(&rtc_handle);
    }
    if (status != HAL_OK) { return hal_result(status); }
    initialized = true;
    cached = false;
    return WATCH_OK;
}

watch_status_t time_service_get(uint32_t *utc)
{
    if (utc == NULL) { return WATCH_INVALID_ARGUMENT; }
    *utc = 0U;
    if (!initialized) { return WATCH_NOT_READY; }
    if (time_service_clock_source() != TIME_CLOCK_LSE) { return WATCH_IO_ERROR; }
    if (HAL_RTCEx_BKUPRead(&rtc_handle, RTC_BKP_DR0) != VALID_MARK ||
        HAL_RTCEx_BKUPRead(&rtc_handle, RTC_BKP_DR1) != (uint32_t)~VALID_MARK) { return WATCH_NOT_READY; }
    if ((RTC->ISR & RTC_ISR_RSF) == 0U) { return WATCH_IO_ERROR; }
    uint32_t now = HAL_GetTick();
    /* 同一毫秒内复用采样，避免在两个LSE周期内重复访问影子寄存器。
     * 有效标记及真实时钟状态仍在每次调用检查，不能用缓存掩盖失效。 */
    if (cached && now == cached_tick) { *utc = cached_utc; return WATCH_OK; }
    RTC_TimeTypeDef time = {0};
    RTC_DateTypeDef date = {0};
    HAL_StatusTypeDef a = HAL_RTC_GetTime(&rtc_handle, &time, RTC_FORMAT_BIN);
    HAL_StatusTypeDef b = HAL_RTC_GetDate(&rtc_handle, &date, RTC_FORMAT_BIN);
    if (a != HAL_OK || b != HAL_OK) { cached = false; return hal_result(a != HAL_OK ? a : b); }
    rtc_calendar_t cal = {(uint16_t)(2000U + date.Year), date.Month, date.Date,
                           time.Hours, time.Minutes, time.Seconds};
    uint32_t value = time_calendar_to_utc(&cal);
    if (value == 0U || date.WeekDay < 1U || date.WeekDay > 7U ||
        date.WeekDay != (uint8_t)(((value / 86400UL + 3U) % 7U) + 1U)) {
        clear_mark();
        return WATCH_INVALID_DATA;
    }
    cached_tick = now; cached_utc = value; cached = true;
    *utc = value;
    return WATCH_OK;
}

watch_status_t time_service_set(uint32_t utc)
{
    rtc_calendar_t cal;
    if (time_utc_to_calendar(utc, &cal) != WATCH_OK) { return WATCH_INVALID_ARGUMENT; }
    if (!initialized) { return WATCH_NOT_READY; }
    if (time_service_clock_source() != TIME_CLOCK_LSE) { return WATCH_IO_ERROR; }
    clear_mark();
    RTC_TimeTypeDef time = {0};
    RTC_DateTypeDef date = {0};
    time.Hours = cal.hour; time.Minutes = cal.minute; time.Seconds = cal.second;
    time.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
    time.StoreOperation = RTC_STOREOPERATION_RESET;
    date.Year = (uint8_t)(cal.year - 2000U); date.Month = cal.month; date.Date = cal.day;
    date.WeekDay = (uint8_t)(((utc / 86400UL + 3U) % 7U) + 1U);
    HAL_StatusTypeDef status = HAL_RTC_SetTime(&rtc_handle, &time, RTC_FORMAT_BIN);
    if (status == HAL_OK) { status = HAL_RTC_SetDate(&rtc_handle, &date, RTC_FORMAT_BIN); }
    if (status != HAL_OK) { return hal_result(status); }
    HAL_RTCEx_BKUPWrite(&rtc_handle, RTC_BKP_DR1, (uint32_t)~VALID_MARK);
    HAL_RTCEx_BKUPWrite(&rtc_handle, RTC_BKP_DR0, VALID_MARK);
    return WATCH_OK;
}

watch_status_t time_service_invalidate(void)
{
    if (!initialized) { return WATCH_NOT_READY; }
    clear_mark();
    return WATCH_OK;
}
#else
watch_status_t time_service_init(void) { return WATCH_NOT_IMPLEMENTED; }
watch_status_t time_service_get(uint32_t *utc)
{
    if (utc == NULL) { return WATCH_INVALID_ARGUMENT; }
    *utc = 0U;
    return WATCH_NOT_IMPLEMENTED;
}
watch_status_t time_service_set(uint32_t utc) { (void)utc; return WATCH_NOT_IMPLEMENTED; }
watch_status_t time_service_invalidate(void) { return WATCH_NOT_IMPLEMENTED; }
time_clock_source_t time_service_clock_source(void) { return TIME_CLOCK_NONE; }
#endif
