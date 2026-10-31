#include "mpu6050.h"
#include "board_config.h"
#include <string.h>
/* 带时间运动采样：按计划任务实现并经用户烧录验收。 */
watch_status_t mpu6050_init(void)
{
    return WATCH_NOT_IMPLEMENTED;
}

watch_status_t mpu6050_read(motion_sample_t *sample)
{
    if(sample==NULL) return WATCH_INVALID_ARGUMENT;
    memset(sample,0,sizeof(*sample));
    return WATCH_NOT_IMPLEMENTED;
}

watch_status_t mpu6050_set_low_power(bool enabled)
{
    (void)enabled;
    return WATCH_NOT_IMPLEMENTED;
}
