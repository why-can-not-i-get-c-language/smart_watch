#ifndef WATCH_BATTERY_ADC_H
#define WATCH_BATTERY_ADC_H
#include "watch_types.h"
#include <stdint.h>
#include <stdbool.h>

/* F08-P01 电池电压ADC诊断状态 */
typedef struct {
    watch_status_t status;         /* 当前采集状态 */
    bool valid;                    /* 当前测量结果有效性 */
    uint16_t raw_pin;             /* 最近ADC引脚原始值 0..4095 */
    uint16_t raw_vref;            /* 最近VREFINT原始值 0..4095 */
    uint16_t vref_cal;            /* 出厂VREFINT校准值 */
    uint16_t vdda_mv;             /* 推算VDDA电压mV */
    uint16_t pin_mv;              /* 引脚电压mV */
    uint16_t battery_mv;          /* 电池电压mV（考虑分压） */
    uint32_t timestamp_ms;        /* 样本时间戳 */
    uint32_t sample_count;        /* 完整批次计数 */
    uint32_t error_count;         /* 错误累计 */
} battery_adc_state_t;

/* 初始化ADC，仅在WATCH_TARGET_STM32平台生效 */
watch_status_t battery_adc_init(void);

/* 周期处理：每5ms调用一次，执行至多一次ADC转换；采集16对样本后发布。
 * 每对含引脚通道与VREFINT通道，每次通道切换后丢弃首次采样。 */
watch_status_t battery_adc_process(uint32_t now_ms);

/* 获取最近有效测量及诊断状态；陈旧样本自动失效。 */
watch_status_t battery_adc_get_state(battery_adc_state_t *state);

/* 读取最近电池电压；无效时输出0，不保留上次有效值。 */
watch_status_t battery_adc_read_mv(uint16_t *millivolts);

#endif
