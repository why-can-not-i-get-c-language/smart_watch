#ifndef WATCH_BLE_LINK_H
#define WATCH_BLE_LINK_H
#include "watch_types.h"
#include "board_config.h"
/* UART透传诊断，不推断真实GATT连接。成功发送仅表示UART发送完成。 */
typedef struct {
    bool initialized;
    watch_status_t rx_status, tx_status;
    uint32_t rx_bytes, tx_bytes, rx_reads, tx_writes, errors, uart_errors;
    uint32_t last_rx_ms;
    uint8_t last_rx[WATCH_BLE_CHUNK_SIZE];
    uint8_t last_rx_length;
} ble_link_state_t;
/* board_uart_init成功后调用；只启用本地适配，不探测或证明模块存在。 */
watch_status_t ble_link_init(void);
/* 每次1..WATCH_BLE_CHUNK_SIZE字节；不自动重试可能已部分发送的内容。 */
watch_status_t ble_link_send(const uint8_t *data, size_t length);
/* 单次读取最多WATCH_BLE_CHUNK_SIZE；错误时received=0，旧预览清除。 */
watch_status_t ble_link_receive(uint8_t *data, size_t capacity, size_t *received);
watch_status_t ble_link_get_state(ble_link_state_t *state);
/* 未接链路状态信号：总是先清false并返回NOT_IMPLEMENTED，不代表已断开。 */
watch_status_t ble_link_connected(bool *connected);
#if WATCH_BLE_NAME_SETUP
/* 专用配置固件：用户确认AT档后单次设置名称，最多等待2秒。
 * OK仅表示模块应答，不证明广播名已生效；正常任务不得并行领取UART。 */
watch_status_t ble_link_setup_name(void);
#endif
#endif
