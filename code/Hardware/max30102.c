#include "max30102.h"
#include "board_config.h"
#include "board_port.h"
#include "i2c_bus.h"
#include <string.h>

/* MAX30102手册物理页11至20：SpO2模式RED在前、IR在后。
 * 100Hz、平均1、411us/18bit、4096nA；LED 0x24约7.2mA仅为配置参考。
 * 本步不判断心率或血氧有效性，不依赖未经验证的模块INT。 */
enum { PPG_QUEUE_SIZE=16, PPG_BATCH_SIZE=4, PPG_STALE_MS=200, PPG_RETRY_MS=1000 };
typedef enum {
    PPG_OFF, PPG_ID, PPG_RESET, PPG_RESET_WAIT, PPG_SHUTDOWN,
    PPG_SET_INTERRUPTS, PPG_SET_FIFO, PPG_SET_SPO2, PPG_SET_LEDS,
    PPG_CLEAR_FIFO, PPG_VERIFY_INTERRUPTS, PPG_VERIFY_CONFIG,
    PPG_VERIFY_LEDS, PPG_CLEAR_STATUS, PPG_START, PPG_VERIFY_MODE,
    PPG_CHECK, PPG_READ_FIFO, PPG_RETRY
} ppg_phase_t;
static ppg_phase_t phase;
static max30102_state_t current;
static ppg_sample_t queue[PPG_QUEUE_SIZE];
static unsigned queue_head, queue_count, pending_samples;
static uint32_t phase_started_ms, last_poll_ms, last_data_ms;
static uint32_t rate_started_ms, rate_samples;

static void invalidate(void)
{
    memset(&current.sample, 0, sizeof(current.sample));
    current.low_signal = false;
    current.saturated = false;
    queue_head = queue_count = pending_samples = 0U;
}

static watch_status_t fail(watch_status_t status, uint32_t now_ms)
{
    current.queue_drops += queue_count;
    invalidate();
    current.ready = false;
    current.status = status;
    current.error_count++;
    current.rate_valid = false;
    current.actual_rate_millihz = 0U;
    phase = PPG_RETRY;
    phase_started_ms = now_ms;
    return status;
}

static void expire(uint32_t now_ms)
{
    if (current.sample.valid && (uint32_t)(now_ms - last_data_ms) >= PPG_STALE_MS) {
        /* 获取缓存也检查过期，不依赖主循环持续调用process。 */
        memset(&current.sample, 0, sizeof(current.sample));
        current.queue_drops += queue_count;
        queue_head = queue_count = 0U;
        current.low_signal = current.saturated = false;
        current.rate_valid = false;
        current.actual_rate_millihz = 0U;
        if (current.ready) { current.status = WATCH_NOT_READY; }
    }
}

static watch_status_t write_byte(uint8_t reg, uint8_t value)
{
    return i2c_bus_write(WATCH_PPG_ADDR7, reg, &value, 1U);
}

static uint32_t channel(const uint8_t *bytes)
{
    return (((uint32_t)bytes[0] << 16U) | ((uint32_t)bytes[1] << 8U) | bytes[2]) & 0x3FFFFU;
}

watch_status_t max30102_init(void)
{
    memset(&current, 0, sizeof(current));
    queue_head = queue_count = 0U;
    invalidate();
    current.status = WATCH_NOT_READY;
    phase = PPG_ID;
    phase_started_ms = last_poll_ms = last_data_ms = board_millis();
    rate_started_ms = phase_started_ms;
    rate_samples = 0U;
    return WATCH_NOT_READY;
}

watch_status_t max30102_process(uint32_t now_ms)
{
    uint8_t data[PPG_BATCH_SIZE * 6U] = {0};
    watch_status_t status;
    expire(now_ms);
    if (phase == PPG_OFF) { return WATCH_NOT_READY; }
    if (phase == PPG_RETRY) {
        if ((uint32_t)(now_ms - phase_started_ms) < PPG_RETRY_MS) { return current.status; }
        phase = PPG_ID;
        current.status = WATCH_NOT_READY;
    }
    if ((phase == PPG_CHECK || phase == PPG_READ_FIFO) &&
        (uint32_t)(now_ms - last_data_ms) >= PPG_RETRY_MS) {
        return fail(WATCH_TIMEOUT, now_ms);
    }
    switch (phase) {
    case PPG_ID:
        current.identity_read = false;
        status = i2c_bus_read(WATCH_PPG_ADDR7, 0xFEU, data, 2U);
        if (status != WATCH_OK) { return fail(status, now_ms); }
        current.revision_id = data[0]; current.part_id = data[1];
        current.identity_read = true;
        if (current.part_id != 0x15U) { return fail(WATCH_INVALID_DATA, now_ms); }
        phase = PPG_RESET;
        break;
    case PPG_RESET:
        status = write_byte(0x09U, 0x40U);
        if (status != WATCH_OK) { return fail(status, now_ms); }
        phase_started_ms = board_millis();
        phase = PPG_RESET_WAIT;
        break;
    case PPG_RESET_WAIT:
        if ((uint32_t)(now_ms - phase_started_ms) >= 500U) { return fail(WATCH_TIMEOUT, now_ms); }
        status = i2c_bus_read(WATCH_PPG_ADDR7, 0x09U, data, 1U);
        if (status != WATCH_OK) { return fail(status, now_ms); }
        if ((data[0] & 0x40U) == 0U) { phase = PPG_SHUTDOWN; }
        break;
    case PPG_SHUTDOWN:
        status = write_byte(0x09U, 0x83U);
        if (status != WATCH_OK) { return fail(status, now_ms); }
        phase = PPG_SET_INTERRUPTS;
        break;
    case PPG_SET_INTERRUPTS:
        data[0] = 0xE0U; data[1] = 0U;
        status = i2c_bus_write(WATCH_PPG_ADDR7, 0x02U, data, 2U);
        if (status != WATCH_OK) { return fail(status, now_ms); }
        phase = PPG_SET_FIFO;
        break;
    case PPG_SET_FIFO:
        status = write_byte(0x08U, 0x0FU);
        if (status != WATCH_OK) { return fail(status, now_ms); }
        phase = PPG_SET_SPO2;
        break;
    case PPG_SET_SPO2:
        status = write_byte(0x0AU, 0x27U);
        if (status != WATCH_OK) { return fail(status, now_ms); }
        phase = PPG_SET_LEDS;
        break;
    case PPG_SET_LEDS:
        data[0] = data[1] = 0x24U;
        status = i2c_bus_write(WATCH_PPG_ADDR7, 0x0CU, data, 2U);
        if (status != WATCH_OK) { return fail(status, now_ms); }
        phase = PPG_CLEAR_FIFO;
        break;
    case PPG_CLEAR_FIFO:
        status = i2c_bus_write(WATCH_PPG_ADDR7, 0x04U, data, 3U);
        if (status != WATCH_OK) { return fail(status, now_ms); }
        phase = PPG_VERIFY_INTERRUPTS;
        break;
    case PPG_VERIFY_INTERRUPTS:
        status = i2c_bus_read(WATCH_PPG_ADDR7, 0x02U, data, 2U);
        if (status != WATCH_OK) { return fail(status, now_ms); }
        if (data[0] != 0xE0U || data[1] != 0U) { return fail(WATCH_INVALID_DATA, now_ms); }
        phase = PPG_VERIFY_CONFIG;
        break;
    case PPG_VERIFY_CONFIG:
        status = i2c_bus_read(WATCH_PPG_ADDR7, 0x08U, data, 3U);
        if (status != WATCH_OK) { return fail(status, now_ms); }
        if (data[0] != 0x0FU || data[1] != 0x83U || data[2] != 0x27U) {
            return fail(WATCH_INVALID_DATA, now_ms);
        }
        phase = PPG_VERIFY_LEDS;
        break;
    case PPG_VERIFY_LEDS:
        status = i2c_bus_read(WATCH_PPG_ADDR7, 0x0CU, data, 2U);
        if (status != WATCH_OK) { return fail(status, now_ms); }
        if (data[0] != 0x24U || data[1] != 0x24U) { return fail(WATCH_INVALID_DATA, now_ms); }
        phase = PPG_CLEAR_STATUS;
        break;
    case PPG_CLEAR_STATUS:
        status = i2c_bus_read(WATCH_PPG_ADDR7, 0x00U, data, 2U);
        if (status != WATCH_OK) { return fail(status, now_ms); }
        phase = PPG_START;
        break;
    case PPG_START:
        status = write_byte(0x09U, 0x03U);
        if (status != WATCH_OK) { return fail(status, now_ms); }
        phase = PPG_VERIFY_MODE;
        break;
    case PPG_VERIFY_MODE:
        status = i2c_bus_read(WATCH_PPG_ADDR7, 0x09U, data, 1U);
        if (status != WATCH_OK) { return fail(status, now_ms); }
        if (data[0] != 0x03U) { return fail(WATCH_INVALID_DATA, now_ms); }
        current.ready = true;
        current.status = WATCH_NOT_READY;
        rate_started_ms = last_data_ms = last_poll_ms = board_millis();
        rate_samples = 0U;
        phase = PPG_CHECK;
        break;
    case PPG_CHECK: {
        if ((uint32_t)(now_ms - last_poll_ms) < 5U) { return WATCH_NOT_READY; }
        last_poll_ms = now_ms;
        status = i2c_bus_read(WATCH_PPG_ADDR7, 0x00U, data, 7U);
        if (status != WATCH_OK) { return fail(status, now_ms); }
        if ((data[0] & 0x01U) != 0U) { return fail(WATCH_INVALID_DATA, now_ms); }
        if (data[2] != 0xE0U || data[3] != 0U) { return fail(WATCH_INVALID_DATA, now_ms); }
        unsigned available = (unsigned)((data[4] - data[6]) & 31U);
        uint8_t overflow = data[5] & 31U;
        bool full = available == 0U && (data[0] & 0x80U) != 0U;
        if (overflow != 0U || full || available >= 28U) {
            current.overflow_count++;
            /* 溢出寄存器饱和31，故累计的是已观察到的下界，不能宣称精确丢样数。 */
            current.lost_samples_lower_bound += overflow + (full ? 32U : available);
            return fail(WATCH_INVALID_DATA, now_ms);
        }
        if ((data[0] & 0x20U) != 0U) {
            /* 环境光抵消溢出不作为当前有效数据继续使用，重新开始配置。 */
            return fail(WATCH_INVALID_DATA, now_ms);
        }
        if (available == 0U) { return WATCH_NOT_READY; }
        pending_samples = available > PPG_BATCH_SIZE ? PPG_BATCH_SIZE : available;
        phase = PPG_READ_FIFO;
        break;
    }
    case PPG_READ_FIFO: {
        /* 调度长暂停后先重新读溢出计数，避免FIFO读取自动清零计数而漏报。 */
        if ((uint32_t)(now_ms - last_poll_ms) >= 20U) {
            pending_samples = 0U;
            phase = PPG_CHECK;
            return WATCH_NOT_READY;
        }
        uint32_t read_started_ms = board_millis();
        unsigned count = pending_samples;
        status = i2c_bus_read(WATCH_PPG_ADDR7, 0x07U, data, count * 6U);
        if (status != WATCH_OK) { return fail(status, now_ms); }
        /* 同一批样本共享主控读取时刻；不反推或冒充传感器转换时间。 */
        for (unsigned i = 0U; i < count; ++i) {
            ppg_sample_t sample = {channel(&data[i * 6U]), channel(&data[i * 6U + 3U]), read_started_ms, true};
            if (queue_count == PPG_QUEUE_SIZE) {
                queue_head = (queue_head + 1U) % PPG_QUEUE_SIZE;
                queue_count--; current.queue_drops++;
            }
            queue[(queue_head + queue_count) % PPG_QUEUE_SIZE] = sample;
            queue_count++;
            current.sample = sample;
        }
        last_data_ms = read_started_ms;
        current.sample_count += count;
        rate_samples += count;
        current.low_signal = current.sample.red < 5000U && current.sample.infrared < 5000U;
        current.saturated = current.sample.red >= 0x3FFF0U || current.sample.infrared >= 0x3FFF0U;
        current.status = WATCH_OK;
        uint32_t elapsed = read_started_ms - rate_started_ms;
        if (elapsed >= 1000U) {
            current.actual_rate_millihz = (uint32_t)(((uint64_t)rate_samples * 1000000U) / elapsed);
            current.rate_valid = true;
            rate_started_ms = read_started_ms; rate_samples = 0U;
        }
        pending_samples = 0U;
        phase = PPG_CHECK;
        return WATCH_OK;
    }
    default:
        return WATCH_NOT_READY;
    }
    return WATCH_NOT_READY;
}

void max30102_get_state(max30102_state_t *state)
{
    expire(board_millis());
    if (state != NULL) { *state = current; }
}

watch_status_t max30102_read(ppg_sample_t *sample)
{
    if (sample == NULL) { return WATCH_INVALID_ARGUMENT; }
    memset(sample, 0, sizeof(*sample));
    uint32_t now_ms = board_millis();
    expire(now_ms);
    if (!current.ready || !current.sample.valid) {
        return current.status == WATCH_OK ? WATCH_NOT_READY : current.status;
    }
    /* 即使最新样本新鲜，队列中的旧批次也可能已过期。 */
    while (queue_count != 0U && (uint32_t)(now_ms - queue[queue_head].timestamp_ms) >= PPG_STALE_MS) {
        queue_head = (queue_head + 1U) % PPG_QUEUE_SIZE;
        queue_count--; current.queue_drops++;
    }
    if (queue_count == 0U) { return WATCH_NOT_READY; }
    *sample = queue[queue_head];
    queue_head = (queue_head + 1U) % PPG_QUEUE_SIZE;
    queue_count--;
    return WATCH_OK;
}

watch_status_t max30102_shutdown(bool enabled)
{
    (void)enabled;
    return WATCH_NOT_IMPLEMENTED;
}
