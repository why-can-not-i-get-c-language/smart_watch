#ifndef WATCH_I2C_BUS_H
#define WATCH_I2C_BUS_H
#include "watch_types.h"
/* 7位地址总线与超时。未实现接口返回WATCH_NOT_IMPLEMENTED，输出无效。 */
watch_status_t i2c_bus_read(uint8_t address7, uint8_t reg, uint8_t *data, size_t length);
watch_status_t i2c_bus_write(uint8_t address7, uint8_t reg, const uint8_t *data, size_t length);
/* 将寄存器和单字节命令组成连续两字节发送包；地址仍为7位。 */
watch_status_t i2c_bus_write_command(uint8_t address7, uint8_t reg, uint8_t command);

/* I2C地址探测（诊断用） */
watch_status_t i2c_bus_probe(uint8_t address7);

/* 内部初始化（board_port.c调用） */
watch_status_t i2c_bus_init_internal(void);

/* 获取上一次操作状态 */
watch_status_t i2c_bus_get_status(void);
#endif
