#ifndef WATCH_RECORD_STORE_H
#define WATCH_RECORD_STORE_H
#include "watch_types.h"
#include <stdbool.h>

/* F07-P03环形历史存储：1791个4KiB扇区，每扇区32字节头+127条记录。
 * 开机异步加载，显式持久化提交，断电恢复合法记录；未知残留只读保留。 */

/* 类型2：UTC+8当天00:00所对应UTC（支持起点当天夹到UTC_MIN），
 * value为该日已观察步数的绝对快照。同日多条取最后ID，不能相加。
 * 含断电/未提交间隙，表示部分汇总，不补造关机日或缺失步数。 */
#define RECORD_STORE_TYPE_MANUAL_EVENT 1U
#define RECORD_STORE_TYPE_DAILY_STEPS 2U

typedef enum {
    RECORD_STORE_IDLE = 0,
    RECORD_STORE_LOADING,
    RECORD_STORE_READY,
    RECORD_STORE_APPENDING,
    RECORD_STORE_READONLY_ERROR
} record_store_phase_t;

typedef struct {
    record_store_phase_t phase;
    bool ready;
    bool busy;
    bool readonly;           /* 未知数据或完整性/IO错误时保留数据，禁止追加。 */
    uint32_t count;           /* 最近扫描/提交验证的保留记录数 */
    uint32_t capacity;        /* 扣除一个备用扇区后的最大容量 */
    uint32_t oldest_id;       /* 最旧记录ID */
    uint32_t newest_id;       /* 最新记录ID */
    uint32_t scan_progress;   /* 扫描百分比0～100 */
    watch_status_t last_append_status;
    watch_status_t last_read_status;
    watch_status_t load_status;
} record_store_state_t;

/* 异步排队写入，WATCH_OK仅表示已复制排队，非已落盘；
 * 最终状态通过last_append_status查询，等于WATCH_OK才表示提交成功。 */
watch_status_t record_store_append(const record_t *record);

/* 按确切ID读取；0/未来/空区为INVALID_ARGUMENT，过旧/缺口/校验错误为
 * INVALID_DATA，忙为NOT_READY，IO错误保留错误码，失败输出无效。
 * UTC=0表示该记录时间未知，其他UTC限定2000～2099年；不补造时间。
 * 发生加载错误时readonly=true，索引可能不完整，不将缺失解释成已覆盖。
 * 顺序歧义时保留所有可校验物理副本，不承诺正常窗口容量，拒绝歧义ID。 */
watch_status_t record_store_read(uint32_t id, record_t *record);

/* 查询当前状态 */
watch_status_t record_store_get_state(record_store_state_t *state);

/* 初始化和5ms推进：开机只读恢复，不自动擦除/格式化；
 * 每次至多推进一个有界阶段，普通读<=256字节，不忙等Flash完成。 */
watch_status_t record_store_init(void);
watch_status_t record_store_process(uint32_t now_ms);

#endif
