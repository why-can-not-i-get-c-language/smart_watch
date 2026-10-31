#ifndef WATCH_MPU6050_H
#define WATCH_MPU6050_H
#include "watch_types.h"
/* 带时间运动采样。未实现接口返回WATCH_NOT_IMPLEMENTED，输出无效。 */
watch_status_t mpu6050_init(void);
watch_status_t mpu6050_read(motion_sample_t *sample);
watch_status_t mpu6050_set_low_power(bool enabled);
#endif
