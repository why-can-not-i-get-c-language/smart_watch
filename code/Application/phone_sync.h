#ifndef WATCH_PHONE_SYNC_H
#define WATCH_PHONE_SYNC_H
#include "watch_types.h"
/* 台架单手机D3协议；不声明BLE连接状态或身份认证。
 * UID取STM32真实UID，版本为当前编译字符串，初始化时复制。 */
watch_status_t phone_sync_init(const char *firmware, const uint32_t uid[3]);
typedef struct {
    uint32_t received, accepted, replays, rtc_writes, uart_resets, tx_errors, queue_drops;
    uint32_t session;
    uint16_t highest_sequence;
    bool executing;
} phone_sync_state_t;
watch_status_t phone_sync_get_state(phone_sync_state_t *state);
/* 每5ms调用：至多读16字节、发送16字节；SetTime读回分至下一轮。
 * 所有收发均由本模块独占，不混入裸文本或原始回显。 */
watch_status_t phone_sync_process(void);
#endif
