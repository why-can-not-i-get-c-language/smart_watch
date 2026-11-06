#ifndef WATCH_SSD1306_H
#define WATCH_SSD1306_H
#include "watch_types.h"
#define SSD1306_WIDTH 128U
#define SSD1306_HEIGHT 64U
#define SSD1306_FRAME_SIZE (SSD1306_WIDTH * SSD1306_HEIGHT / 8U)
/* 初始化后关屏。flush成功仅表示帧已复制排队，忙时不覆盖在途数据。
 * process每次最多发送16字节；整帧通过后is_busy变为false。
 * 总线错误会清除ready和busy，需要重新初始化。 */
watch_status_t ssd1306_init(void);
watch_status_t ssd1306_flush(const uint8_t *frame, size_t length);
watch_status_t ssd1306_set_enabled(bool enabled);

/* 分步发送进程：从调度器定期调用 */
watch_status_t ssd1306_process(void);
bool ssd1306_is_busy(void);
bool ssd1306_is_ready(void);
watch_status_t ssd1306_get_status(void);
#endif
