#include "ble_link.h"
#include "board_config.h"
#include "uart_port.h"
#include "board_port.h"
#include <string.h>
#include <limits.h>

static ble_link_state_t current;

static uint32_t add_saturated(uint32_t value, uint32_t increment)
{
    return value > UINT32_MAX - increment ? UINT32_MAX : value + increment;
}

watch_status_t ble_link_init(void)
{
    memset(&current, 0, sizeof(current));
    /* USART1由board_uart_init唯一初始化；本层不重置接收缓冲。 */
    watch_status_t status = uart_port_get_status();
    current.initialized = status == WATCH_OK;
    current.rx_status = current.initialized ? WATCH_NOT_READY : status;
    current.tx_status = WATCH_NOT_READY;
    return status;
}

watch_status_t ble_link_send(const uint8_t *data, size_t length)
{
    if (data == NULL || length == 0U || length > WATCH_BLE_CHUNK_SIZE) { return WATCH_INVALID_ARGUMENT; }
    if (!current.initialized) { return WATCH_NOT_READY; }
    current.tx_status = uart_port_send(data, length);
    if (current.tx_status == WATCH_OK) {
        current.tx_bytes = add_saturated(current.tx_bytes, (uint32_t)length);
        current.tx_writes = add_saturated(current.tx_writes, 1U);
    } else {
        current.errors = add_saturated(current.errors, 1U);
    }
    return current.tx_status;
}

watch_status_t ble_link_receive(uint8_t *data, size_t capacity, size_t *received)
{
    if (received == NULL) { return WATCH_INVALID_ARGUMENT; }
    *received = 0U;
    if (data == NULL || capacity == 0U) { return WATCH_INVALID_ARGUMENT; }
    if (!current.initialized) { return WATCH_NOT_READY; }
    size_t limit = capacity < WATCH_BLE_CHUNK_SIZE ? capacity : WATCH_BLE_CHUNK_SIZE;
    current.rx_status = uart_port_receive(data, limit, received);
    if (current.rx_status != WATCH_OK || *received > limit) {
        if (current.rx_status == WATCH_OK) { current.rx_status = WATCH_INVALID_DATA; }
        *received = 0U;
        current.last_rx_length = 0U;
        memset(current.last_rx, 0, sizeof(current.last_rx));
        current.errors = add_saturated(current.errors, 1U);
    } else if (*received != 0U) {
        current.rx_bytes = add_saturated(current.rx_bytes, (uint32_t)*received);
        current.rx_reads = add_saturated(current.rx_reads, 1U);
        current.last_rx_ms = board_millis();
        current.last_rx_length = (uint8_t)*received;
        memset(current.last_rx, 0, sizeof(current.last_rx));
        memcpy(current.last_rx, data, *received);
    }
    return current.rx_status;
}

watch_status_t ble_link_get_state(ble_link_state_t *state)
{
    if (state == NULL) { return WATCH_INVALID_ARGUMENT; }
    *state = current;
    state->uart_errors = uart_port_get_error_count();
    return WATCH_OK;
}

watch_status_t ble_link_connected(bool *connected)
{
    if(connected==NULL) return WATCH_INVALID_ARGUMENT;
    *connected=false;
    return WATCH_NOT_IMPLEMENTED;
}
