#include "board_port.h"
#include "watch_app.h"
/* HAL/启动/链接配置按P01、P02接入；当前不是可烧录固件。 */
int main(void)
{
    if(board_init()!=WATCH_OK) return 1;
    if(watch_app_init()!=WATCH_OK) return 2;
    for(;;){ if(watch_app_process()!=WATCH_OK) return 3; }
}
