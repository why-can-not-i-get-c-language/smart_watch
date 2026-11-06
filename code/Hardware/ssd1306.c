#include "ssd1306.h"
#include "board_config.h"
#include "i2c_bus.h"
#include <string.h>

#ifdef WATCH_TARGET_STM32

#define OLED_COMMAND 0x00U
#define OLED_DATA 0x40U
#define OLED_CHUNK_SIZE 16U

static uint8_t pending_frame[SSD1306_FRAME_SIZE];
static size_t tx_offset;
static bool ready;
static bool busy;
static watch_status_t last_status = WATCH_NOT_READY;

/* 总线错误后丢弃在途帧，等待界面按节流策略重新初始化。 */
static watch_status_t failed(watch_status_t status)
{
    ready = false;
    busy = false;
    tx_offset = 0U;
    last_status = status;
    return status;
}

static watch_status_t command(const uint8_t *data, size_t length)
{
    watch_status_t status = i2c_bus_write(WATCH_OLED_ADDR7, OLED_COMMAND, data, length);
    return status == WATCH_OK ? WATCH_OK : failed(status);
}

watch_status_t ssd1306_init(void)
{
    /* 水平寻址在第128列后自动进入下一页，共8页。初始化后保持关屏。 */
    static const uint8_t sequence[] = {
        0xAEU, 0xD5U, 0x80U, 0xA8U, 0x3FU, 0xD3U, 0x00U, 0x40U,
        0x8DU, 0x14U, 0x20U, 0x00U, 0xA1U, 0xC8U, 0xDAU, 0x12U,
        0x81U, 0x7FU, 0xA4U, 0xA6U, 0xD9U, 0xF1U, 0xDBU, 0x20U, 0x2EU
    };
    ready = false;
    busy = false;
    tx_offset = 0U;
    watch_status_t status = command(sequence, sizeof(sequence));
    if (status != WATCH_OK) { return status; }
    ready = true;
    last_status = WATCH_OK;
    return WATCH_OK;
}

watch_status_t ssd1306_flush(const uint8_t *frame, size_t length)
{
    if (frame == NULL || length != SSD1306_FRAME_SIZE) { return WATCH_INVALID_ARGUMENT; }
    if (!ready || busy) { return WATCH_NOT_READY; }
    memcpy(pending_frame, frame, sizeof(pending_frame));
    tx_offset = 0U;
    busy = true;
    /* 成功表示已复制并排队，不表示整帧已经发送。 */
    return WATCH_OK;
}

watch_status_t ssd1306_process(void)
{
    if (!ready) { return last_status; }
    if (!busy) { return WATCH_OK; }
    if (tx_offset == 0U) {
        static const uint8_t window[] = {0x21U, 0x00U, 0x7FU, 0x22U, 0x00U, 0x07U};
        watch_status_t status = command(window, sizeof(window));
        if (status != WATCH_OK) { return status; }
    }
    size_t length = sizeof(pending_frame) - tx_offset;
    if (length > OLED_CHUNK_SIZE) { length = OLED_CHUNK_SIZE; }
    watch_status_t status = i2c_bus_write(WATCH_OLED_ADDR7, OLED_DATA,
                                         &pending_frame[tx_offset], length);
    if (status != WATCH_OK) { return failed(status); }
    tx_offset += length;
    busy = tx_offset < sizeof(pending_frame);
    last_status = WATCH_OK;
    return WATCH_OK;
}

watch_status_t ssd1306_set_enabled(bool enabled)
{
    if (!ready) { return WATCH_NOT_READY; }
    const uint8_t value = enabled ? 0xAFU : 0xAEU;
    watch_status_t status = command(&value, 1U);
    if (status == WATCH_OK) { last_status = WATCH_OK; }
    return status;
}

bool ssd1306_is_busy(void) { return busy; }
bool ssd1306_is_ready(void) { return ready; }
watch_status_t ssd1306_get_status(void) { return last_status; }

#else

watch_status_t ssd1306_init(void) { return WATCH_NOT_IMPLEMENTED; }
watch_status_t ssd1306_flush(const uint8_t *frame, size_t length)
{
    if (frame == NULL || length != SSD1306_FRAME_SIZE) { return WATCH_INVALID_ARGUMENT; }
    return WATCH_NOT_IMPLEMENTED;
}
watch_status_t ssd1306_process(void) { return WATCH_NOT_IMPLEMENTED; }
watch_status_t ssd1306_set_enabled(bool enabled)
{
    (void)enabled;
    return WATCH_NOT_IMPLEMENTED;
}
bool ssd1306_is_busy(void) { return false; }
bool ssd1306_is_ready(void) { return false; }
watch_status_t ssd1306_get_status(void) { return WATCH_NOT_IMPLEMENTED; }

#endif
