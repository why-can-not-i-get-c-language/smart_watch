#ifndef WATCH_SCHEDULER_H
#define WATCH_SCHEDULER_H
#include "watch_types.h"
typedef void (*scheduler_callback_t)(void *context);
typedef struct { uint32_t period_ms, last_ms; scheduler_callback_t callback; void *context; } scheduler_task_t;
/* 周期必须非零且不超过INT32_MAX；调用间隔小于一次32位时基回绕。 */
watch_status_t scheduler_run(scheduler_task_t *tasks, size_t count, uint32_t now_ms);
#endif
