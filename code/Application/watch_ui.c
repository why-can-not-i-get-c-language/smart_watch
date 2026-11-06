#include "watch_ui.h"
#include "ssd1306.h"
#include <string.h>

/* 每行5位、共7行的小字库，只保存本界面需要的字符。 */
typedef struct { char value; uint8_t rows[7]; } glyph_t;
static const glyph_t glyphs[] = {
    {'A',{14,17,17,31,17,17,17}}, {'B',{30,17,17,30,17,17,30}},
    {'C',{14,17,16,16,16,17,14}}, {'D',{30,17,17,17,17,17,30}},
    {'E',{31,16,16,30,16,16,31}}, {'F',{31,16,16,30,16,16,16}},
    {'G',{14,17,16,23,17,17,15}}, {'H',{17,17,17,31,17,17,17}},
    {'I',{14,4,4,4,4,4,14}}, {'J',{7,2,2,2,18,18,12}},
    {'K',{17,18,20,24,20,18,17}}, {'L',{16,16,16,16,16,16,31}},
    {'M',{17,27,21,21,17,17,17}}, {'N',{17,25,21,19,17,17,17}},
    {'O',{14,17,17,17,17,17,14}}, {'P',{30,17,17,30,16,16,16}},
    {'Q',{14,17,17,17,21,18,13}}, {'R',{30,17,17,30,20,18,17}},
    {'S',{15,16,16,14,1,1,30}}, {'T',{31,4,4,4,4,4,4}},
    {'U',{17,17,17,17,17,17,14}}, {'V',{17,17,17,17,17,10,4}},
    {'W',{17,17,17,21,21,21,10}}, {'X',{17,17,10,4,10,17,17}},
    {'Y',{17,17,10,4,4,4,4}}, {'Z',{31,1,2,4,8,16,31}},
    {'0',{14,17,19,21,25,17,14}}, {'1',{4,12,4,4,4,4,14}},
    {'2',{14,17,1,2,4,8,31}}, {'3',{30,1,1,14,1,1,30}},
    {'4',{2,6,10,18,31,2,2}}, {'5',{31,16,16,30,1,1,30}},
    {'6',{14,16,16,30,17,17,14}}, {'7',{31,1,2,4,8,8,8}},
    {'8',{14,17,17,14,17,17,14}}, {'9',{14,17,17,15,1,1,14}},
    {':',{0,4,4,0,4,4,0}}, {'-',{0,0,0,31,0,0,0}},
    {'?',{14,17,1,2,4,0,4}}
};

typedef enum { UI_WHITE, UI_CHECKER, UI_CORNERS, UI_OFF, UI_STATUS } ui_phase_t;
static uint8_t frame[SSD1306_FRAME_SIZE];
static ui_phase_t phase;
static bool ever_initialized;
static bool attempted_init;
static bool frame_pending;
static bool holding;
static bool screen_enabled;
static uint32_t last_init_ms;
static uint32_t hold_started_ms;
static uint32_t last_refresh_ms;

static void pixel(unsigned x, unsigned y)
{
    if (x < SSD1306_WIDTH && y < SSD1306_HEIGHT) {
        frame[(y / 8U) * SSD1306_WIDTH + x] |= (uint8_t)(1U << (y % 8U));
    }
}

static void character(unsigned x, unsigned y, char value)
{
    if (value == ' ') { return; }
    const uint8_t *rows = NULL;
    for (size_t i = 0U; i < sizeof(glyphs) / sizeof(glyphs[0]); ++i) {
        if (glyphs[i].value == value) { rows = glyphs[i].rows; break; }
    }
    if (rows == NULL) { rows = glyphs[sizeof(glyphs) / sizeof(glyphs[0]) - 1U].rows; }
    for (unsigned row = 0U; row < 7U; ++row) {
        for (unsigned col = 0U; col < 5U; ++col) {
            if ((rows[row] & (1U << (4U - col))) != 0U) { pixel(x + col, y + row); }
        }
    }
}

static void text(unsigned x, unsigned y, const char *value)
{
    while (*value != '\0' && x + 5U <= SSD1306_WIDTH && y + 7U <= SSD1306_HEIGHT) {
        character(x, y, *value++);
        x += 6U;
    }
}

static void number(unsigned x, unsigned y, uint32_t value)
{
    char digits[11];
    size_t used = 0U;
    do { digits[used++] = (char)('0' + value % 10U); value /= 10U; } while (value != 0U);
    while (used != 0U && x + 5U <= SSD1306_WIDTH) {
        character(x, y, digits[--used]);
        x += 6U;
    }
}

static void draw(uint32_t now_ms)
{
    memset(frame, 0, sizeof(frame));
    if (phase == UI_WHITE) {
        memset(frame, 0xFF, sizeof(frame));
    } else if (phase == UI_CHECKER) {
        for (unsigned page = 0U; page < SSD1306_HEIGHT / 8U; ++page) {
            for (unsigned x = 0U; x < SSD1306_WIDTH; ++x) {
                frame[page * SSD1306_WIDTH + x] = ((page + x / 8U) & 1U) != 0U ? 0xFFU : 0U;
            }
        }
    } else if (phase == UI_CORNERS) {
        for (unsigned x = 0U; x < SSD1306_WIDTH; ++x) { pixel(x, 0U); pixel(x, 63U); }
        for (unsigned y = 0U; y < SSD1306_HEIGHT; ++y) { pixel(0U, y); pixel(127U, y); }
        text(3U, 3U, "TL"); text(113U, 3U, "TR");
        text(3U, 54U, "BL"); text(113U, 54U, "BR");
        text(25U, 25U, "DISPLAY CHECK");
    } else {
        text(31U, 0U, "SMART WATCH");
        text(2U, 16U, "UPTIME S"); number(62U, 16U, now_ms / 1000U);
        text(2U, 26U, "TIME --:--");
        text(2U, 36U, "STEPS --");
        text(2U, 46U, "BAT --");
        text(2U, 56U, "DATA NOT READY");
    }
}

watch_status_t watch_ui_update(uint32_t now_ms)
{
    if (!ssd1306_is_ready()) {
        if (attempted_init && (uint32_t)(now_ms - last_init_ms) < 1000U) {
            return ssd1306_get_status();
        }
        attempted_init = true;
        last_init_ms = now_ms;
        watch_status_t status = ssd1306_init();
        if (status != WATCH_OK) { return status; }
        /* 自检只运行一次，掉屏后直接重新提交当前状态页。 */
        phase = ever_initialized ? UI_STATUS : UI_WHITE;
        ever_initialized = true;
        screen_enabled = false;
        holding = false;
        frame_pending = false;
        last_refresh_ms = now_ms - 1000U;
    }

    if (frame_pending) {
        if (ssd1306_is_busy()) { return WATCH_OK; }
        if (!screen_enabled) {
            watch_status_t status = ssd1306_set_enabled(true);
            if (status != WATCH_OK) { return status; }
            screen_enabled = true;
        }
        frame_pending = false;
        if (phase != UI_STATUS) { holding = true; hold_started_ms = now_ms; }
        return WATCH_OK;
    }
    if (holding) {
        if ((uint32_t)(now_ms - hold_started_ms) < 1000U) { return WATCH_OK; }
        if (phase == UI_CORNERS) {
            watch_status_t status = ssd1306_set_enabled(false);
            if (status != WATCH_OK) { return status; }
            screen_enabled = false;
            phase = UI_OFF;
            hold_started_ms = now_ms;
            return WATCH_OK;
        }
        phase = phase == UI_WHITE ? UI_CHECKER : phase == UI_CHECKER ? UI_CORNERS : UI_STATUS;
        holding = false;
        if (phase == UI_STATUS) { last_refresh_ms = now_ms - 1000U; }
    }
    if (phase == UI_OFF || ssd1306_is_busy()) { return WATCH_OK; }
    if (phase == UI_STATUS && (uint32_t)(now_ms - last_refresh_ms) < 1000U) { return WATCH_OK; }
    draw(now_ms);
    watch_status_t status = ssd1306_flush(frame, sizeof(frame));
    if (status == WATCH_OK) { frame_pending = true; last_refresh_ms = now_ms; }
    return status;
}
