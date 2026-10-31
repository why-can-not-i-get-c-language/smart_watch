#ifndef WATCH_BATTERY_ADC_H
#define WATCH_BATTERY_ADC_H
#include "watch_types.h"
/* 电池电压采样。未实现接口返回WATCH_NOT_IMPLEMENTED，输出无效。 */
watch_status_t battery_adc_read_mv(uint16_t *millivolts);
#endif
