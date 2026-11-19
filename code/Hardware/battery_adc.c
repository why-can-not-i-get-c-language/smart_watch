#include "battery_adc.h"
#include "board_config.h"
#include "board_port.h"
#include <string.h>

#ifdef WATCH_TARGET_STM32
#include "stm32f4xx_hal.h"

/* ADC1由本驱动独占；四次调度完成一对样本，切换通道后首采丢弃。 */
static ADC_HandleTypeDef hadc;
static battery_adc_state_t state = {.status = WATCH_NOT_READY};
static uint32_t sum_pin, sum_ref, batch_start_ms, retry_ms;
static unsigned phase, pairs;
static bool initialized, batch_active, retry_pending;

static void reset_batch(void)
{
    sum_pin = 0U; sum_ref = 0U; pairs = 0U; phase = 0U;
    batch_active = false;
}

static watch_status_t fail(watch_status_t status, uint32_t now_ms)
{
    state.status = status;
    state.valid = false;
    state.vdda_mv = 0U; state.pin_mv = 0U; state.battery_mv = 0U;
    reset_batch();
    initialized = false;
    retry_pending = true;
    retry_ms = now_ms;
    if (state.error_count != UINT32_MAX) { ++state.error_count; }
    return status;
}

static watch_status_t hal_error(HAL_StatusTypeDef status)
{
    return status == HAL_TIMEOUT ? WATCH_TIMEOUT : WATCH_IO_ERROR;
}

static watch_status_t configure(uint32_t now_ms)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_ADC1_CLK_ENABLE();
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin = WATCH_BATTERY_ADC_PIN;
    gpio.Mode = GPIO_MODE_ANALOG;
    gpio.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(WATCH_BATTERY_ADC_PORT, &gpio);
    if (hadc.Instance == ADC1 && HAL_ADC_DeInit(&hadc) != HAL_OK) {
        return fail(WATCH_IO_ERROR, now_ms);
    }
    memset(&hadc, 0, sizeof(hadc));
    hadc.Instance = ADC1;
    hadc.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV4;
    hadc.Init.Resolution = ADC_RESOLUTION_12B;
    hadc.Init.DataAlign = ADC_DATAALIGN_RIGHT;
    hadc.Init.ScanConvMode = DISABLE;
    hadc.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
    hadc.Init.ContinuousConvMode = DISABLE;
    hadc.Init.NbrOfConversion = 1U;
    hadc.Init.DiscontinuousConvMode = DISABLE;
    hadc.Init.ExternalTrigConv = ADC_SOFTWARE_START;
    hadc.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
    hadc.Init.DMAContinuousRequests = DISABLE;
    HAL_StatusTypeDef status = HAL_ADC_Init(&hadc);
    if (status != HAL_OK) { return fail(hal_error(status), now_ms); }
    /* 复用F411厂商库地址：0x1FFF7A2A，校准条件VDDA=3.3V。 */
    state.vref_cal = *VREFINT_CAL_ADDR;
    if (state.vref_cal == 0U || state.vref_cal > 4095U) {
        return fail(WATCH_INVALID_DATA, now_ms);
    }
    ADC_ChannelConfTypeDef config = {0};
    config.Channel = ADC_CHANNEL_VREFINT;
    config.Rank = 1U;
    config.SamplingTime = ADC_SAMPLETIME_480CYCLES;
    status = HAL_ADC_ConfigChannel(&hadc, &config);
    if (status != HAL_OK) { return fail(hal_error(status), now_ms); }
    /* 此处只使能VREFINT；首个有效参考采样在后续多次5ms调度后，满足10us启动。 */
    reset_batch();
    initialized = true;
    retry_pending = false;
    state.status = WATCH_NOT_READY;
    return WATCH_OK;
}

watch_status_t battery_adc_init(void)
{
    memset(&state, 0, sizeof(state));
    state.status = WATCH_NOT_READY;
    initialized = false;
    retry_pending = false;
    reset_batch();
    return configure(board_millis());
}

static void expire(uint32_t now_ms)
{
    if (state.valid && (uint32_t)(now_ms - state.timestamp_ms) >= WATCH_BATTERY_ADC_STALE_MS) {
        (void)fail(WATCH_TIMEOUT, now_ms);
    }
}

watch_status_t battery_adc_process(uint32_t now_ms)
{
    expire(now_ms);
    if (!initialized) {
        if (retry_pending && (uint32_t)(now_ms - retry_ms) < WATCH_BATTERY_ADC_RETRY_MS) {
            return state.status;
        }
        watch_status_t status = configure(now_ms);
        return status == WATCH_OK ? WATCH_NOT_READY : status;
    }
    if (batch_active && (uint32_t)(now_ms - batch_start_ms) >= WATCH_BATTERY_ADC_BATCH_TIMEOUT_MS) {
        return fail(WATCH_TIMEOUT, now_ms);
    }
    if (!batch_active) { batch_active = true; batch_start_ms = now_ms; }
    ADC_ChannelConfTypeDef config = {0};
    config.Channel = phase < 2U ? WATCH_BATTERY_ADC_CHANNEL : ADC_CHANNEL_VREFINT;
    config.Rank = 1U;
    config.SamplingTime = ADC_SAMPLETIME_480CYCLES; /* 当前4MHz ADC时钟，采样120us。 */
    HAL_StatusTypeDef status = HAL_ADC_ConfigChannel(&hadc, &config);
    if (status == HAL_OK) { status = HAL_ADC_Start(&hadc); }
    if (status == HAL_OK) { status = HAL_ADC_PollForConversion(&hadc, WATCH_BATTERY_ADC_TIMEOUT_MS); }
    uint32_t raw = status == HAL_OK ? HAL_ADC_GetValue(&hadc) : 0U;
    HAL_StatusTypeDef stop = HAL_ADC_Stop(&hadc);
    if (status == HAL_OK) { status = stop; }
    if (status != HAL_OK) { return fail(hal_error(status), now_ms); }
    if ((phase & 1U) == 0U) { ++phase; return state.status; }
    if (raw > 4095U || (phase == 1U && raw == 4095U) || (phase == 3U && raw == 0U)) {
        return fail(WATCH_INVALID_DATA, now_ms);
    }
    if (phase == 1U) {
        sum_pin += raw;
        phase = 2U;
        return state.status;
    }
    sum_ref += raw;
    phase = 0U;
    if (++pairs < WATCH_BATTERY_ADC_AVERAGING) { return state.status; }
    /* 直接用累加和换算，减少先取平均再取整的误差；输出四舍五入到mV。 */
    uint64_t reference = (uint64_t)3300U * state.vref_cal;
    uint64_t vdda = (reference * pairs + sum_ref / 2U) / sum_ref;
    uint64_t denominator = (uint64_t)sum_ref * 4095U;
    uint64_t numerator = (uint64_t)sum_pin * reference;
    uint64_t pin = (numerator + denominator / 2U) / denominator;
    uint64_t battery_den = denominator * WATCH_BATTERY_DIV_BOTTOM_K;
    uint64_t battery = (numerator * (WATCH_BATTERY_DIV_TOP_K + WATCH_BATTERY_DIV_BOTTOM_K) + battery_den / 2U) / battery_den;
    int64_t corrected = ((int64_t)battery * WATCH_BATTERY_ADC_GAIN_PPM + 500000LL) / 1000000LL + WATCH_BATTERY_ADC_OFFSET_MV;
    if (vdda < 1700U || vdda > 3600U || pin > UINT16_MAX || corrected < 0 || corrected > UINT16_MAX) {
        return fail(WATCH_INVALID_DATA, now_ms);
    }
    state.raw_pin = (uint16_t)((sum_pin + pairs / 2U) / pairs);
    state.raw_vref = (uint16_t)((sum_ref + pairs / 2U) / pairs);
    state.vdda_mv = (uint16_t)vdda;
    state.pin_mv = (uint16_t)pin;
    state.battery_mv = (uint16_t)corrected;
    state.timestamp_ms = now_ms;
    state.valid = true;
    state.status = WATCH_OK;
    if (state.sample_count != UINT32_MAX) { ++state.sample_count; }
    reset_batch();
    return WATCH_OK;
}

watch_status_t battery_adc_get_state(battery_adc_state_t *out)
{
    if (out == NULL) { return WATCH_INVALID_ARGUMENT; }
    expire(board_millis());
    *out = state;
    return state.status;
}

watch_status_t battery_adc_read_mv(uint16_t *millivolts)
{
    if (millivolts == NULL) { return WATCH_INVALID_ARGUMENT; }
    expire(board_millis());
    *millivolts = state.valid ? state.battery_mv : 0U;
    return state.status;
}
#else
watch_status_t battery_adc_init(void) { return WATCH_NOT_IMPLEMENTED; }
watch_status_t battery_adc_process(uint32_t now_ms)
{
    (void)now_ms;
    return WATCH_NOT_IMPLEMENTED;
}
watch_status_t battery_adc_get_state(battery_adc_state_t *out)
{
    if (out == NULL) { return WATCH_INVALID_ARGUMENT; }
    memset(out, 0, sizeof(*out));
    out->status = WATCH_NOT_IMPLEMENTED;
    return WATCH_NOT_IMPLEMENTED;
}
watch_status_t battery_adc_read_mv(uint16_t *millivolts)
{
    if (millivolts == NULL) { return WATCH_INVALID_ARGUMENT; }
    *millivolts = 0U;
    return WATCH_NOT_IMPLEMENTED;
}
#endif
