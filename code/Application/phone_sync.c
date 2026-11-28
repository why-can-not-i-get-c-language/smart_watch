#include "phone_sync.h"
#include "ble_protocol.h"
#include "ble_link.h"
#include "board_port.h"
#include "uart_port.h"
#include "time_service.h"
#include "pedometer.h"
#include "mpu6050.h"
#include "battery_adc.h"
#include <string.h>
#include <limits.h>

enum { CMD_INFO = 1, CMD_STATUS = 2, CMD_TIME = 3 };
enum { OK = 0, BAD_LENGTH, BAD_VALUE, UNSUPPORTED, NOT_READY, IO_ERROR,
       BUSY, SEQ_CONFLICT, REQUEST_EXPIRED, INTERNAL, TIMEOUT };
enum { DATA_UNSUPPORTED = 0, DATA_MISSING, DATA_VALID, DATA_INVALID, DATA_STALE };
enum { IDLE = 0, EXECUTE, READBACK };
#define TX_SLOTS 4U
#define UTC_MIN 946684800UL
#define UTC_MAX 4102444799UL

typedef struct {
    uint8_t bytes[BLE_PROTOCOL_MAX_FRAME];
    size_t length, offset;
} tx_frame_t;
static tx_frame_t tx[TX_SLOTS];
static unsigned tx_head, tx_count;
static ble_protocol_frame_t request;
static uint8_t cached[BLE_PROTOCOL_MAX_FRAME], uid_bytes[12];
static size_t cached_length;
static char firmware_version[32];
static uint8_t firmware_length, phase;
static uint32_t completed_ms, uart_errors, target_utc, last_tx_ms, written_ms;
static bool initialized, cache_present, tx_started;
static phone_sync_state_t state;
#if WATCH_F06_STATUS_SILENCE_TEST
static bool status_silence_test, request_silenced;
static uint32_t status_silence_deadline;
#endif

static void increment(uint32_t *value)
{
    if (*value != UINT32_MAX) { ++*value; }
}

static void put16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8U);
}

static void put32(uint8_t *p, uint32_t v)
{
    for (unsigned i = 0U; i < 4U; ++i) { p[i] = (uint8_t)(v >> (i * 8U)); }
}

static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8U |
           (uint32_t)p[2] << 16U | (uint32_t)p[3] << 24U;
}

static uint8_t wire_error(watch_status_t result)
{
    switch (result) {
    case WATCH_OK: return OK;
    case WATCH_INVALID_ARGUMENT: return BAD_VALUE;
    case WATCH_NOT_IMPLEMENTED: return UNSUPPORTED;
    case WATCH_NOT_READY: return NOT_READY;
    case WATCH_IO_ERROR: case WATCH_INVALID_DATA: return IO_ERROR;
    case WATCH_TIMEOUT: return TIMEOUT;
    default: return INTERNAL;
    }
}

static bool same_request(const ble_protocol_frame_t *a, const ble_protocol_frame_t *b)
{
    return a->kind == b->kind && a->command == b->command &&
           a->sequence == b->sequence && a->session == b->session &&
           a->length == b->length && memcmp(a->payload, b->payload, a->length) == 0;
}

static void enqueue(const uint8_t *bytes, size_t length)
{
    /* 同一完整应答已在发送队列时不重复排队，也不从中间重新起发。 */
    for (unsigned i = 0U; i < tx_count; ++i) {
        tx_frame_t *slot = &tx[(tx_head + i) % TX_SLOTS];
        if (slot->length == length && memcmp(slot->bytes, bytes, length) == 0) { return; }
    }
    if (tx_count == TX_SLOTS) { increment(&state.queue_drops); return; }
    tx_frame_t *slot = &tx[(tx_head + tx_count) % TX_SLOTS];
    memcpy(slot->bytes, bytes, length);
    slot->length = length; slot->offset = 0U;
    ++tx_count;
}

static void temporary_error(const ble_protocol_frame_t *frame, uint8_t error)
{
    /* 有在途执行时保留一个队列槽给最终应答；临时错误不覆盖去重缓存。 */
    if (tx_count >= TX_SLOTS - (phase != IDLE ? 1U : 0U)) {
        increment(&state.queue_drops); return;
    }
    ble_protocol_frame_t reply = {0};
    uint8_t bytes[18]; size_t length = 0U;
    reply.kind = 1U; reply.command = frame->command;
    reply.sequence = frame->sequence; reply.session = frame->session;
    reply.length = 1U; reply.payload[0] = error;
    if (ble_protocol_encode(&reply, bytes, sizeof(bytes), &length) == WATCH_OK) {
        enqueue(bytes, length);
    }
}

static void receive_frame(const ble_protocol_frame_t *frame, void *context)
{
    (void)context;
    if (frame->kind != 0U) { return; }
    increment(&state.received);
    if (frame->session == 0U || frame->sequence == 0U) {
        temporary_error(frame, BAD_VALUE); return;
    }
    bool new_session = frame->session != state.session;
    if (phase != IDLE) {
        if (new_session) {
            temporary_error(frame, frame->command == CMD_INFO && frame->length == 0U &&
                            frame->sequence == 1U ? BUSY : NOT_READY);
        } else if (frame->sequence == request.sequence) {
            if (!same_request(frame, &request)) { temporary_error(frame, SEQ_CONFLICT); }
        } else {
            temporary_error(frame, frame->sequence < request.sequence ? REQUEST_EXPIRED : BUSY);
        }
        return;
    }
    if (new_session) {
        if (frame->command != CMD_INFO) { temporary_error(frame, NOT_READY); return; }
        if (frame->length != 0U) { temporary_error(frame, BAD_LENGTH); return; }
        if (frame->sequence != 1U) { temporary_error(frame, BAD_VALUE); return; }
    } else if (frame->sequence <= state.highest_sequence) {
        if (frame->sequence < state.highest_sequence) {
            temporary_error(frame, REQUEST_EXPIRED);
        } else if (!same_request(frame, &request)) {
            temporary_error(frame, SEQ_CONFLICT);
#if WATCH_F06_STATUS_SILENCE_TEST
        } else if (request_silenced) {
            return;
#endif
        } else if (cache_present && (uint32_t)(board_millis() - completed_ms) <= 30000U) {
            enqueue(cached, cached_length); increment(&state.replays);
        } else {
            temporary_error(frame, REQUEST_EXPIRED);
        }
        return;
    }
    /* 受理前保证最终应答有空间，队列满时无副作用、不消耗序号。 */
    if (tx_count >= TX_SLOTS - 1U) { temporary_error(frame, BUSY); return; }
    request = *frame;
    state.session = frame->session;
    state.highest_sequence = frame->sequence;
    cache_present = false;
#if WATCH_F06_STATUS_SILENCE_TEST
    request_silenced = status_silence_test && frame->command == CMD_STATUS && frame->length == 0U;
    if (request_silenced) {
        increment(&state.accepted);
        return;
    }
    request_silenced = false;
#endif
    phase = EXECUTE;
    increment(&state.accepted);
}

static void finish(uint8_t error, const uint8_t *data, size_t length, uint32_t now)
{
    ble_protocol_frame_t reply = {0};
    reply.kind = 1U; reply.command = request.command;
    reply.sequence = request.sequence; reply.session = request.session;
    reply.payload[0] = error;
    reply.length = (uint16_t)(1U + (error == OK ? length : 0U));
    if (error == OK && length != 0U) { memcpy(reply.payload + 1U, data, length); }
    if (ble_protocol_encode(&reply, cached, sizeof(cached), &cached_length) == WATCH_OK) {
        completed_ms = now;
        cache_present = true;
        enqueue(cached, cached_length);
    }
    phase = IDLE;
}

static void status_payload(uint8_t data[42], uint32_t now)
{
    /* 整帧固定快照，未支持/无效值字段先置零，未知age为FFFFFFFF。 */
    memset(data, 0, 42U);
    put32(data, now);
    put32(data + 15U, UINT32_MAX); put32(data + 22U, UINT32_MAX);
    put32(data + 31U, UINT32_MAX); put32(data + 38U, UINT32_MAX);
    uint32_t utc = 0U;
    watch_status_t result = time_service_get(&utc);
    data[4] = result == WATCH_OK && utc >= UTC_MIN && utc <= UTC_MAX ? DATA_VALID :
              result == WATCH_NOT_READY ? DATA_MISSING : DATA_INVALID;
    if (data[4] == DATA_VALID) { put32(data + 5U, utc); }

    mpu6050_state_t motion;
    pedometer_status_t steps;
    memset(&motion, 0, sizeof(motion)); memset(&steps, 0, sizeof(steps));
    mpu6050_get_state(&motion);
    result = pedometer_get_state(&steps);
    if (motion.sample_count != 0U) { put32(data + 15U, now - motion.sample.timestamp_ms); }
    if (result != WATCH_OK || (motion.status != WATCH_OK && motion.status != WATCH_NOT_READY)) {
        data[9] = DATA_INVALID;
    } else if (motion.sample_count != 0U && (uint32_t)(now - motion.sample.timestamp_ms) > 1000U) {
        data[9] = DATA_STALE;
    } else if (!motion.sample.valid || motion.sample_count == 0U ||
               (steps.state != PEDOMETER_READY && steps.state != PEDOMETER_VERIFYING)) {
        data[9] = DATA_MISSING;
    } else {
        data[9] = DATA_VALID; put32(data + 11U, steps.step_count);
    }
    battery_adc_state_t battery;
    memset(&battery, 0, sizeof(battery));
    result = battery_adc_get_state(&battery);
    if (battery.sample_count != 0U) { put32(data + 22U, now - battery.timestamp_ms); }
    if (result == WATCH_NOT_IMPLEMENTED) { data[19] = DATA_UNSUPPORTED; }
    else if (battery.sample_count != 0U && (uint32_t)(now - battery.timestamp_ms) >= 1000U) {
        data[19] = DATA_STALE;
    } else if (battery.valid && result == WATCH_OK) {
        data[19] = DATA_VALID; put16(data + 20U, battery.battery_mv);
    } else { data[19] = result == WATCH_NOT_READY ||
                       (battery.sample_count == 0U && result == WATCH_OK) ? DATA_MISSING : DATA_INVALID; }
}

static void execute(uint32_t now)
{
    if (phase == READBACK) {
        if ((uint32_t)(now - written_ms) < 5U) { return; }
        uint32_t actual = 0U;
        watch_status_t result = time_service_get(&actual);
        if (result == WATCH_OK && (actual < target_utc || actual > UTC_MAX ||
                                  (uint64_t)actual > (uint64_t)target_utc + 1U)) {
            result = WATCH_IO_ERROR;
        }
        uint8_t bytes[4]; put32(bytes, actual);
        finish(wire_error(result), bytes, sizeof(bytes), now);
        return;
    }
    if (phase != EXECUTE) { return; }
    if (request.command == CMD_INFO) {
        if (request.length != 0U) { finish(BAD_LENGTH, NULL, 0U, now); return; }
        uint8_t data[52] = {0};
        memcpy(data, uid_bytes, sizeof(uid_bytes));
        put16(data + 12U, 1U);  /* 项目核心板加模块；硬件修订号仍为未知0 */
        put32(data + 16U, 0x1FU);
        data[20] = firmware_length;
        memcpy(data + 21U, firmware_version, firmware_length);
        finish(OK, data, 21U + firmware_length, now);
    } else if (request.command == CMD_STATUS) {
        if (request.length != 0U) { finish(BAD_LENGTH, NULL, 0U, now); return; }
        uint8_t data[42]; status_payload(data, now);
        finish(OK, data, sizeof(data), now);
    } else if (request.command == CMD_TIME) {
        if (request.length != 4U) { finish(BAD_LENGTH, NULL, 0U, now); return; }
        target_utc = get32(request.payload);
        if (target_utc < UTC_MIN || target_utc > UTC_MAX) {
            finish(BAD_VALUE, NULL, 0U, now); return;
        }
        watch_status_t result = time_service_set(target_utc);
        /* 统计调用次数而非宣称RTC成功；失败也缓存，防止不确定写后重复执行。 */
        increment(&state.rtc_writes);
        if (result != WATCH_OK) { finish(wire_error(result), NULL, 0U, now); }
        else { written_ms = board_millis(); phase = READBACK; }
    } else { finish(UNSUPPORTED, NULL, 0U, now); }
}

watch_status_t phone_sync_init(const char *firmware, const uint32_t uid[3])
{
    initialized = false;
#if WATCH_F06_STATUS_SILENCE_TEST
    status_silence_test = false; request_silenced = false; status_silence_deadline = 0U;
#endif
    if (firmware == NULL || uid == NULL) { return WATCH_INVALID_ARGUMENT; }
    size_t length = 0U;
    while (length < 32U && firmware[length] != '\0') {
        if ((unsigned char)firmware[length] < 32U || (unsigned char)firmware[length] > 126U) {
            return WATCH_INVALID_ARGUMENT;
        }
        ++length;
    }
    if (length == 0U || length > 31U) { return WATCH_INVALID_ARGUMENT; }
    memset(&state, 0, sizeof(state)); memset(&request, 0, sizeof(request));
    tx_head = tx_count = 0U; phase = IDLE; cache_present = false; tx_started = false;
    cached_length = 0U; firmware_length = (uint8_t)length;
    memcpy(firmware_version, firmware, length); firmware_version[length] = '\0';
    for (unsigned i = 0U; i < 3U; ++i) { put32(uid_bytes + i * 4U, uid[i]); }
    uart_errors = uart_port_get_error_count();
    ble_protocol_init(receive_frame, NULL);
    initialized = true;
    return WATCH_OK;
}

watch_status_t phone_sync_get_state(phone_sync_state_t *out)
{
    if (out == NULL) { return WATCH_INVALID_ARGUMENT; }
    *out = state; out->executing = phase != IDLE;
    return initialized ? WATCH_OK : WATCH_NOT_READY;
}

watch_status_t phone_sync_process(void)
{
    if (!initialized) { return WATCH_NOT_READY; }
    uint32_t now = board_millis();
#if WATCH_F06_STATUS_SILENCE_TEST
    if (status_silence_test && (int32_t)(now - status_silence_deadline) >= 0) {
        status_silence_test = false;
    }
#endif
    /* 周期失效后不再恢复，避免32位时基绕回时复活很久以前的应答。 */
    if (cache_present && (uint32_t)(now - completed_ms) > 30000U) { cache_present = false; }
    uint8_t bytes[16]; size_t received = 0U;
    watch_status_t result = ble_link_receive(bytes, sizeof(bytes), &received);
    uint32_t errors = uart_port_get_error_count();
    if (result != WATCH_OK || errors != uart_errors) {
        ble_protocol_reset(); increment(&state.uart_resets);
    } else { (void)ble_protocol_feed(bytes, received, now); }
    uart_errors = errors;
    ble_protocol_expire(now);
    execute(now);
    if (tx_count != 0U && (!tx_started || (uint32_t)(now - last_tx_ms) >= 5U)) {
        tx_frame_t *slot = &tx[tx_head];
        size_t length = slot->length - slot->offset;
        if (length > 16U) { length = 16U; }
        watch_status_t sent = ble_link_send(slot->bytes + slot->offset, length);
        tx_started = true; last_tx_ms = now;
        if (sent == WATCH_OK) { slot->offset += length; }
        else { increment(&state.tx_errors); result = sent; }
        /* 半帧失败结束该次发送；完整缓存保留，等待手机重传请求。 */
        if (sent != WATCH_OK || slot->offset == slot->length) {
            tx_head = (tx_head + 1U) % TX_SLOTS; --tx_count;
        }
    }
    return result;
}

#if WATCH_F06_STATUS_SILENCE_TEST
void phone_sync_toggle_status_silence_test(void)
{
    status_silence_test = !status_silence_test;
    status_silence_deadline = status_silence_test ? board_millis() + 120000U : 0U;
}

bool phone_sync_status_silence_test_enabled(void)
{
    return status_silence_test;
}
#endif
