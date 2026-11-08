#include "mpu6050.h"
#include "board_config.h"
#include "board_port.h"
#include "i2c_bus.h"
#include <string.h>

/* 寄存器手册Rev4.0：DLPF=4使内部采样基准为1kHz，分频19得到50Hz。
 * ±8g为4096LSB/g，±250度/秒为131LSB/(度/秒)。 */
_Static_assert(WATCH_MPU_SAMPLE_HZ == 50U && WATCH_MPU_SAMPLE_MS == 20U,
               "MPU sampling configuration must remain 50Hz");
static const uint8_t sampling_config[] = {19U, 4U, 0U, 0x10U};
typedef enum {
    MPU_OFF, MPU_IDENTIFY, MPU_RESET, MPU_RESET_WAIT, MPU_WAKE,
    MPU_WAKE_WAIT, MPU_AXES, MPU_CONFIG, MPU_INTERRUPT,
    MPU_VERIFY_POWER, MPU_VERIFY_AXES, MPU_VERIFY_CONFIG,
    MPU_VERIFY_INTERRUPT, MPU_RUNNING, MPU_RETRY
} mpu_phase_t;
static mpu_phase_t phase;
static mpu6050_state_t current;
static uint32_t phase_started_ms, last_poll_ms, last_sample_ms;
static uint32_t rate_started_ms, rate_count;
static bool unread_sample, have_previous_sample;

static watch_status_t fail(watch_status_t status)
{
    current.status = status;
    current.ready = false;
    memset(&current.sample, 0, sizeof(current.sample));
    unread_sample = false;
    have_previous_sample = false;
    current.actual_rate_millihz = 0U;
    current.rate_valid = false;
    current.last_interval_ms = 0U;
    current.error_count++;
    phase = MPU_RETRY;
    phase_started_ms = board_millis();
    return status;
}

static watch_status_t write_byte(uint8_t reg, uint8_t value)
{
    return i2c_bus_write(WATCH_MPU_ADDR7, reg, &value, 1U);
}

static float signed_value(const uint8_t *bytes, float scale)
{
    /* 显式扩展符号，避免超范围无符号转有符号的实现相关行为。 */
    int32_t value = (int32_t)(((uint32_t)bytes[0] << 8) | bytes[1]);
    if (value >= 32768) { value -= 65536; }
    return (float)value / scale;
}

watch_status_t mpu6050_init(void)
{
    memset(&current, 0, sizeof(current));
    current.status = WATCH_NOT_READY;
    phase = MPU_IDENTIFY;
    unread_sample = false;
    have_previous_sample = false;
    return WATCH_NOT_READY;
}

watch_status_t mpu6050_process(uint32_t now_ms)
{
    uint8_t data[15] = {0};
    watch_status_t status = WATCH_NOT_READY;
    if (phase == MPU_OFF) { return WATCH_NOT_READY; }
    if (phase == MPU_RETRY) {
        if ((uint32_t)(now_ms - phase_started_ms) < WATCH_MPU_RETRY_MS) { return current.status; }
        phase = MPU_IDENTIFY;
        current.status = WATCH_NOT_READY;
    }
    switch (phase) {
    case MPU_IDENTIFY:
        current.identity_read = false;
        status = i2c_bus_read(WATCH_MPU_ADDR7, 0x75U, data, 1U);
        if (status != WATCH_OK) { return fail(status); }
        current.identity_read = true;
        current.who_am_i = data[0];
        if (data[0] != 0x68U) { return fail(WATCH_INVALID_DATA); }
        phase = MPU_RESET;
        break;
    case MPU_RESET:
        status = write_byte(0x6BU, 0x80U);
        if (status != WATCH_OK) { return fail(status); }
        phase_started_ms = board_millis();
        phase = MPU_RESET_WAIT;
        break;
    case MPU_RESET_WAIT:
        if ((uint32_t)(now_ms - phase_started_ms) < 100U) { return WATCH_NOT_READY; }
        status = i2c_bus_read(WATCH_MPU_ADDR7, 0x6BU, data, 1U);
        if (status != WATCH_OK) { return fail(status); }
        if ((data[0] & 0x80U) != 0U) {
            if ((uint32_t)(now_ms - phase_started_ms) >= 500U) { return fail(WATCH_TIMEOUT); }
            return WATCH_NOT_READY;
        }
        phase = MPU_WAKE;
        break;
    case MPU_WAKE:
        status = write_byte(0x6BU, 0x01U);
        if (status != WATCH_OK) { return fail(status); }
        phase_started_ms = board_millis();
        phase = MPU_WAKE_WAIT;
        break;
    case MPU_WAKE_WAIT:
        if ((uint32_t)(now_ms - phase_started_ms) < 100U) { return WATCH_NOT_READY; }
        phase = MPU_AXES;
        return WATCH_NOT_READY;
    case MPU_AXES:
        status = write_byte(0x6CU, 0U);
        if (status != WATCH_OK) { return fail(status); }
        phase = MPU_CONFIG;
        break;
    case MPU_CONFIG:
        status = i2c_bus_write(WATCH_MPU_ADDR7, 0x19U, sampling_config, sizeof(sampling_config));
        if (status != WATCH_OK) { return fail(status); }
        phase = MPU_INTERRUPT;
        break;
    case MPU_INTERRUPT:
        /* 仅轮询DATA_RDY；没有配置STM32 EXTI，也无需连接INT。 */
        status = write_byte(0x38U, 0x01U);
        if (status != WATCH_OK) { return fail(status); }
        phase = MPU_VERIFY_POWER;
        break;
    case MPU_VERIFY_POWER:
        status = i2c_bus_read(WATCH_MPU_ADDR7, 0x6BU, data, 1U);
        if (status != WATCH_OK) { return fail(status); }
        if (data[0] != 0x01U) { return fail(WATCH_INVALID_DATA); }
        phase = MPU_VERIFY_AXES;
        break;
    case MPU_VERIFY_AXES:
        status = i2c_bus_read(WATCH_MPU_ADDR7, 0x6CU, data, 1U);
        if (status != WATCH_OK) { return fail(status); }
        if (data[0] != 0U) { return fail(WATCH_INVALID_DATA); }
        phase = MPU_VERIFY_CONFIG;
        break;
    case MPU_VERIFY_CONFIG:
        status = i2c_bus_read(WATCH_MPU_ADDR7, 0x19U, data, sizeof(sampling_config));
        if (status != WATCH_OK) { return fail(status); }
        if (memcmp(data, sampling_config, sizeof(sampling_config)) != 0) { return fail(WATCH_INVALID_DATA); }
        phase = MPU_VERIFY_INTERRUPT;
        break;
    case MPU_VERIFY_INTERRUPT:
        status = i2c_bus_read(WATCH_MPU_ADDR7, 0x38U, data, 1U);
        if (status != WATCH_OK) { return fail(status); }
        if (data[0] != 0x01U) { return fail(WATCH_INVALID_DATA); }
        current.ready = true;
        phase_started_ms = now_ms;
        last_poll_ms = now_ms - WATCH_MPU_SAMPLE_MS;
        rate_started_ms = now_ms;
        rate_count = 0U;
        phase = MPU_RUNNING;
        break;
    case MPU_RUNNING: {
        /* 窗口覆盖真实经过时间，调度延误会体现在实际频率和间隔中。 */
        if ((uint32_t)(now_ms - rate_started_ms) >= 1000U) {
            uint32_t elapsed = now_ms - rate_started_ms;
            current.actual_rate_millihz = (uint32_t)(((uint64_t)rate_count * 1000000U) / elapsed);
            current.rate_valid = true;
            rate_count = 0U;
            rate_started_ms = now_ms;
        }
        if (current.sample.valid && (uint32_t)(now_ms - last_sample_ms) > WATCH_MPU_STALE_MS) {
            memset(&current.sample, 0, sizeof(current.sample));
            unread_sample = false;
            current.status = WATCH_NOT_READY;
        }
        if ((uint32_t)(now_ms - last_poll_ms) < WATCH_MPU_SAMPLE_MS) { return current.status; }
        last_poll_ms = now_ms;
        uint32_t read_started_ms = board_millis();
        /* 0x3A状态与0x3B起的六轴数据连续读取，不重复消费旧样本。 */
        status = i2c_bus_read(WATCH_MPU_ADDR7, 0x3AU, data, sizeof(data));
        if (status != WATCH_OK) { return fail(status); }
        if ((data[0] & 0x01U) == 0U) {
            /* 应答正常但长时间不出新数据也重新配置，覆盖传感器独立复位。 */
            uint32_t since = have_previous_sample ? last_sample_ms : phase_started_ms;
            if ((uint32_t)(now_ms - since) >= WATCH_MPU_RETRY_MS) { return fail(WATCH_TIMEOUT); }
            return WATCH_NOT_READY;
        }
        current.sample.ax_g = signed_value(&data[1], 4096.0f);
        current.sample.ay_g = signed_value(&data[3], 4096.0f);
        current.sample.az_g = signed_value(&data[5], 4096.0f);
        /* data[7..8]为内部温度，本功能不采集或展示温度。 */
        current.sample.gx_dps = signed_value(&data[9], 131.0f);
        current.sample.gy_dps = signed_value(&data[11], 131.0f);
        current.sample.gz_dps = signed_value(&data[13], 131.0f);
        current.sample.timestamp_ms = read_started_ms;
        current.sample.valid = true;
        if (have_previous_sample) {
            current.last_interval_ms = read_started_ms - last_sample_ms;
            if (current.last_interval_ms > 2U * WATCH_MPU_SAMPLE_MS) { current.gap_count++; }
        }
        have_previous_sample = true;
        last_sample_ms = read_started_ms;
        current.sample_count++;
        rate_count++;
        unread_sample = true;
        current.status = WATCH_OK;
        return WATCH_OK;
    }
    default:
        return WATCH_NOT_READY;
    }
    return WATCH_NOT_READY;
}

void mpu6050_get_state(mpu6050_state_t *state)
{
    if (state != NULL) { *state = current; }
}

watch_status_t mpu6050_read(motion_sample_t *sample)
{
    if (sample == NULL) { return WATCH_INVALID_ARGUMENT; }
    memset(sample, 0, sizeof(*sample));
    if (!current.ready || !current.sample.valid) {
        return current.status == WATCH_OK ? WATCH_NOT_READY : current.status;
    }
    if (!unread_sample) { return WATCH_NOT_READY; }
    *sample = current.sample;
    unread_sample = false;
    return WATCH_OK;
}

watch_status_t mpu6050_set_low_power(bool enabled)
{
    (void)enabled;
    return WATCH_NOT_IMPLEMENTED;
}
