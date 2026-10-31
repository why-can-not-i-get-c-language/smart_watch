#ifndef WATCH_I2C_BUS_H
#define WATCH_I2C_BUS_H
#include "watch_types.h"
/* 7位地址总线与超时。未实现接口返回WATCH_NOT_IMPLEMENTED，输出无效。 */
watch_status_t i2c_bus_read(uint8_t address7, uint8_t reg, uint8_t *data, size_t length);
watch_status_t i2c_bus_write(uint8_t address7, uint8_t reg, const uint8_t *data, size_t length);
#endif
