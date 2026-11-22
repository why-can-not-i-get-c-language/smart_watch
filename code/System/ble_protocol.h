#ifndef WATCH_BLE_PROTOCOL_H
#define WATCH_BLE_PROTOCOL_H
#include "watch_types.h"
#define BLE_PROTOCOL_VERSION 1U
#define BLE_PROTOCOL_MAX_PAYLOAD 128U
#define BLE_PROTOCOL_HEADER_SIZE 13U
#define BLE_PROTOCOL_MAX_FRAME (BLE_PROTOCOL_HEADER_SIZE + BLE_PROTOCOL_MAX_PAYLOAD + 4U)

typedef struct {
    uint8_t kind, command;
    uint16_t sequence, length;
    uint32_t session;
    uint8_t payload[BLE_PROTOCOL_MAX_PAYLOAD];
} ble_protocol_frame_t;

/* 回调只在完整且CRC正确时同步调用；frame只在回调期间有效。
 * 由主循环串行使用，不在回调中再次feed或重置解析器。 */
typedef void (*ble_protocol_handler_t)(const ble_protocol_frame_t *frame, void *context);
void ble_protocol_init(ble_protocol_handler_t handler, void *context);
/* UART溢出/错误后丢弃候选，不撤销回调；会话和去重由业务层管理。 */
void ble_protocol_reset(void);
/* 空闲周期及加入新字节前均检查；时间参数使用同一单调毫秒时基。 */
void ble_protocol_expire(uint32_t now_ms);
watch_status_t ble_protocol_feed(const uint8_t *data, size_t length, uint32_t now_ms);
/* 编码允许零session/seq，用于关联非法请求的错误应答；业务层另做校验。 */
watch_status_t ble_protocol_encode(const ble_protocol_frame_t *frame, uint8_t *out,
                                   size_t capacity, size_t *written);
#endif
