#ifndef WATCH_SETTINGS_H
#define WATCH_SETTINGS_H
#include "watch_types.h"
#include <stdbool.h>
#define SETTINGS_MAX_PAYLOAD 128U
/* F07-P02双槽参数持久化状态 */
typedef struct {
    bool ready;
    bool busy;
    bool valid;
    bool empty;
    uint32_t generation;
    uint16_t length;
    watch_status_t last_load_status;
    watch_status_t last_save_status;
} settings_state_t;
/* settings_load返回WATCH_OK表示读取合法缓存；NOT_READY可能为未就绪、
 * 介质不可用或真正空白，须以state.empty区分；其他错误保留原错误码。
 * 失败时输出无效。settings_save异步排队，
 * WATCH_OK表示请求已接受并立即复制数据，非已落盘；最终状态通过
 * settings_get_operation_status查询。非空未知数据拒绝保存。 */
watch_status_t settings_load(void *data, size_t length);
watch_status_t settings_save(const void *data, size_t length);
watch_status_t settings_get_operation_status(void);
watch_status_t settings_get_state(settings_state_t *state);
/* 初始化/显式重新读取及5ms推进。内部持有Flash参数锁，忙时拒绝重置。
 * settings_load只读缓存，不发SPI；空白和读取失败通过state.empty区分。
 * 代号按uint32_t回绕；未知非空槽保留并拒绝作为保存目标。 */
watch_status_t settings_init(void);
watch_status_t settings_process(uint32_t now_ms);
#endif
