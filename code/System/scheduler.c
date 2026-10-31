#include "scheduler.h"
#include <limits.h>
watch_status_t scheduler_run(scheduler_task_t *tasks, size_t count, uint32_t now_ms)
{
    if(tasks==NULL && count!=0U) return WATCH_INVALID_ARGUMENT;
    for(size_t i=0U;i<count;++i){
        if(tasks[i].callback==NULL || tasks[i].period_ms==0U || tasks[i].period_ms>INT32_MAX) return WATCH_INVALID_ARGUMENT;
    }
    for(size_t i=0U;i<count;++i){
        scheduler_task_t *task=&tasks[i];
        if((uint32_t)(now_ms-task->last_ms)>=task->period_ms){
            /* 超期只执行一次，不在恢复后集中补跑。 */
            task->last_ms=now_ms; task->callback(task->context);
        }
    }
    return WATCH_OK;
}
