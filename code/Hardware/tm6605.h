#ifndef WATCH_TM6605_H
#define WATCH_TM6605_H
#include "watch_types.h"
typedef enum { TM6605_NONE=0, TM6605_INIT, TM6605_PLAY, TM6605_STOP, TM6605_SLEEP, TM6605_WAKE } tm6605_command_t;
typedef struct {
    bool ready;
    bool busy; /* 本地命令尚未完成，不是马达真实忙位。 */
    /* 只表示最后休眠命令ACK，不能证明共享总线下持续睡眠。 */
    bool sleep_command_sent;
    tm6605_command_t command;
    uint8_t effect;
    watch_status_t status;
    uint32_t completed_count; /* 命令全部传输ACK的次数，不是实测振动次数。 */
} tm6605_state_t;
/* OK表示已接受排队，完成须看快照busy=false及status=OK。
 * init探测ACK并确认停止；不证明芯片身份，也不自动播放。
 * stop/sleep可抢占排队中的PLAY；效果编号123只供sleep使用。 */
watch_status_t tm6605_init(void);
watch_status_t tm6605_play(uint8_t effect);
watch_status_t tm6605_stop(void);
watch_status_t tm6605_sleep(bool enabled);
/* 每次最多一个I2C传输；错误直接返回并保留在快照中。 */
watch_status_t tm6605_process(uint32_t now_ms);
/* 只读本地元数据：返回OK表示复制成功，不代表硬件动作成功。 */
watch_status_t tm6605_get_state(tm6605_state_t *snapshot);
#endif
