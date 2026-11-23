#include "ble_protocol.h"
#include "crc32.h"
#include <string.h>
/* 13字节头 + 128字节载荷 + 4字节CRC，传输块边界不作为帧边界。 */
static uint8_t candidate[BLE_PROTOCOL_MAX_FRAME];
/* 重同步后候选起点可能移动，保留到达时刻，不继承坏帧的年龄。 */
static uint32_t arrived_ms[BLE_PROTOCOL_MAX_FRAME];
static size_t used;
static uint32_t first_ms, last_ms;
static ble_protocol_handler_t receiver;
static void *receiver_context;

static uint16_t get16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8U));
}

static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8U) |
           ((uint32_t)p[2] << 16U) | ((uint32_t)p[3] << 24U);
}

static void put16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8U);
}

static void put32(uint8_t *p, uint32_t value)
{
    for (unsigned i = 0U; i < 4U; ++i) { p[i] = (uint8_t)(value >> (i * 8U)); }
}

void ble_protocol_reset(void)
{
    used = 0U;
    first_ms = last_ms = 0U;
}

void ble_protocol_init(ble_protocol_handler_t handler, void *context)
{
    ble_protocol_reset();
    receiver = handler;
    receiver_context = context;
}

void ble_protocol_expire(uint32_t now_ms)
{
    if (used != 0U && ((uint32_t)(now_ms - last_ms) > 1000U ||
                       (uint32_t)(now_ms - first_ms) > 5000U)) {
        ble_protocol_reset();
    }
}

static void discard(size_t count)
{
    used -= count;
    memmove(candidate, candidate + count, used);
    memmove(arrived_ms, arrived_ms + count, used * sizeof(arrived_ms[0]));
    if (used != 0U) { first_ms = arrived_ms[0]; last_ms = arrived_ms[used - 1U]; }
}

static void parse(void)
{
    while (used != 0U) {
        if (candidate[0] != 0xA5U || (used >= 2U && candidate[1] != 0x5AU)) {
            discard(1U);
            continue;
        }
        if (used < BLE_PROTOCOL_HEADER_SIZE) { return; }
        uint16_t length = get16(candidate + 11U);
        if (candidate[2] != BLE_PROTOCOL_VERSION || candidate[3] > 1U ||
            length > BLE_PROTOCOL_MAX_PAYLOAD) {
            discard(1U);
            continue;
        }
        size_t total = BLE_PROTOCOL_HEADER_SIZE + (size_t)length + 4U;
        if (used < total) { return; }
        uint32_t crc = 0U;
        (void)crc32_compute(candidate, total - 4U, &crc);
        if (crc != get32(candidate + total - 4U)) {
            discard(1U);
            continue;
        }
        ble_protocol_frame_t frame;
        memset(&frame, 0, sizeof(frame));
        frame.kind = candidate[3];
        frame.command = candidate[4];
        frame.sequence = get16(candidate + 5U);
        frame.session = get32(candidate + 7U);
        frame.length = length;
        memcpy(frame.payload, candidate + BLE_PROTOCOL_HEADER_SIZE, length);
        discard(total);
        receiver(&frame, receiver_context);
    }
}

watch_status_t ble_protocol_feed(const uint8_t *data, size_t length, uint32_t now_ms)
{
    if (data == NULL && length != 0U) { return WATCH_INVALID_ARGUMENT; }
    if (receiver == NULL) { return WATCH_NOT_READY; }
    ble_protocol_expire(now_ms);
    for (size_t i = 0U; i < length; ++i) {
        if (used == 0U) { first_ms = now_ms; }
        last_ms = now_ms;
        arrived_ms[used] = now_ms;
        candidate[used++] = data[i];
        parse();
    }
    return WATCH_OK;
}

watch_status_t ble_protocol_encode(const ble_protocol_frame_t *frame, uint8_t *out,
                                   size_t capacity, size_t *written)
{
    if (written == NULL) { return WATCH_INVALID_ARGUMENT; }
    *written = 0U;
    if (frame == NULL || out == NULL || frame->length > BLE_PROTOCOL_MAX_PAYLOAD ||
        frame->kind > 1U) { return WATCH_INVALID_ARGUMENT; }
    size_t total = BLE_PROTOCOL_HEADER_SIZE + (size_t)frame->length + 4U;
    if (capacity < total) { return WATCH_INVALID_ARGUMENT; }
    out[0] = 0xA5U;
    out[1] = 0x5AU;
    out[2] = BLE_PROTOCOL_VERSION;
    out[3] = frame->kind;
    out[4] = frame->command;
    put16(out + 5U, frame->sequence);
    put32(out + 7U, frame->session);
    put16(out + 11U, frame->length);
    memcpy(out + BLE_PROTOCOL_HEADER_SIZE, frame->payload, frame->length);
    uint32_t crc = 0U;
    (void)crc32_compute(out, total - 4U, &crc);
    put32(out + total - 4U, crc);
    *written = total;
    return WATCH_OK;
}
