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

#if WATCH_BLE_NAME_SETUP
static ble_name_stage_t name_stage;

ble_name_stage_t ble_link_setup_stage(void)
{
    return name_stage;
}

static watch_status_t setup_line_result(const char *line)
{
    if (strncmp(line, "ERROR", 5U) == 0) { return WATCH_INVALID_DATA; }
    if (name_stage == BLE_NAME_ENTER) {
        return strcmp(line, "Enter AT Mode") == 0 ? WATCH_OK : WATCH_NOT_READY;
    }
    if (name_stage == BLE_NAME_SET) {
        return strcmp(line, "OK") == 0 ? WATCH_OK : WATCH_NOT_READY;
    }
    /* 查询应返回名称；忽略空行、命令回显和孤立OK，避免把旧ACK当模式确认。 */
    return line[0] != '\0' && strncmp(line, "AT+", 3U) != 0 &&
           strcmp(line, "OK") != 0 ? WATCH_OK : WATCH_NOT_READY;
}

static watch_status_t setup_exchange(const char *command, size_t length)
{
    uint8_t bytes[16];
    size_t count = 0U;
    char line[32];
    size_t used = 0U;
    bool overflow = false;
    current.rx_bytes = 0U;
    current.last_rx_length = 0U;
    memset(current.last_rx, 0, sizeof(current.last_rx));
    /* 清除确认之前的旧输入；持续输入时拒绝配置，不无限排空。 */
    for (unsigned i = 0U; ; ++i) {
        watch_status_t status = uart_port_receive(bytes, sizeof(bytes), &count);
        if (status != WATCH_OK) { return status; }
        if (count == 0U) { break; }
        if (i >= WATCH_UART1_RX_BUFFER_SIZE / sizeof(bytes)) { return WATCH_NOT_READY; }
    }
    watch_status_t status = uart_port_send((const uint8_t *)command, length);
    if (status != WATCH_OK) { return status; }
    uint32_t started = board_millis();
    uint32_t last_byte = started;
    while ((uint32_t)(board_millis() - started) < 2000U) {
        status = uart_port_receive(bytes, sizeof(bytes), &count);
        if (status != WATCH_OK) { return status; }
        if (count != 0U) {
            current.rx_bytes = add_saturated(current.rx_bytes, (uint32_t)count);
            current.last_rx_length = (uint8_t)count;
            memcpy(current.last_rx, bytes, count);
            last_byte = board_millis();
        }
        /* 部分固件的模式提示不以换行结束：空闲100毫秒后按完整提示判断。 */
        if (count == 0U && used != 0U && (uint32_t)(board_millis() - last_byte) >= 100U) {
            line[used] = '\0';
            status = overflow ? WATCH_NOT_READY : setup_line_result(line);
            if (status != WATCH_NOT_READY) { return status; }
            used = 0U;
            overflow = false;
        }
        for (size_t i = 0U; i < count; ++i) {
            if (bytes[i] == '\r' || bytes[i] == '\n') {
                line[used] = '\0';
                status = overflow ? WATCH_NOT_READY : setup_line_result(line);
                if (status != WATCH_NOT_READY) { return status; }
                used = 0U;
                overflow = false;
            } else if (bytes[i] < 32U || bytes[i] > 126U) {
                overflow = true;
            } else if (used < sizeof(line) - 1U) {
                line[used++] = (char)bytes[i];
            } else {
                overflow = true;
            }
        }
    }
    return WATCH_TIMEOUT;
}

watch_status_t ble_link_setup_name(void)
{
    static const char query[] = "AT+NAME=?\r\n";
    static const char enter[] = "+++";
    static const char command[] = "AT+NAME=" WATCH_BLE_DEVICE_NAME "\r\n";
    name_stage = BLE_NAME_QUERY;
    if (sizeof(WATCH_BLE_DEVICE_NAME) - 1U > 13U) { return WATCH_INVALID_ARGUMENT; }
    watch_status_t status = setup_exchange(query, sizeof(query) - 1U);
    if (status == WATCH_TIMEOUT) {
        /* 老版拨动开关、新版+++；查询无响应只尝试一次新版进入流程。 */
        uint32_t quiet = board_millis();
        while ((uint32_t)(board_millis() - quiet) < 1000U) { }
        name_stage = BLE_NAME_ENTER;
        status = setup_exchange(enter, sizeof(enter) - 1U);
    }
    if (status != WATCH_OK) { return status; }
    name_stage = BLE_NAME_SET;
    return setup_exchange(command, sizeof(command) - 1U);
}
#endif
