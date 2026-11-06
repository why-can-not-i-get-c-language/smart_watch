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

typedef enum { UI_WHITE, UI_CHECKER, UI_CORNERS, UI_OFF, UI_BUTTON_CHECK } ui_phase_t;
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

/* 按键计数与最近事件 */
static uint32_t btn_press_counts[3];
static uint32_t btn_release_counts[3];
static uint32_t btn_short_counts[3];
static uint32_t btn_long_counts[3];
static const char *btn_last_event_name[3] = {"READY", "READY", "READY"};
static int btn_last_key_idx;

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
    if (value > 999999) value = 999999;
    if (value == 0) { character(x, y, '0'); return; }
    do { digits[used++] = (char)('0' + value % 10U); value /= 10U; } while (value != 0U && used < 10);
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
    } else if (phase == UI_BUTTON_CHECK) {
        /* y=0: BUTTON CHECK标题 */
        text(18U, 0U, "BUTTON CHECK");

        /* y=8: UPTIME S + 秒数 */
        text(2U, 8U, "UPTIME S"); number(54U, 8U, now_ms / 1000U);

        /* y=16: 最近按键名称和事件 */
        const char *key_names[] = {"BOARD", "MENU", "SOS"};
        int idx = btn_last_key_idx;
        if (idx >= 0 && idx < 3) {
            text(2U, 16U, key_names[idx]);
            text(38U, 16U, btn_last_event_name[idx][0] ? btn_last_event_name[idx] : "READY");
        }

        /* 四行显示最近按键自身的计数，六位饱和值不会重叠。 */
        text(2U, 24U, "PRESS"); number(62U, 24U, btn_press_counts[idx]);
        text(2U, 32U, "RELEASE"); number(62U, 32U, btn_release_counts[idx]);
        text(2U, 40U, "SHORT"); number(62U, 40U, btn_short_counts[idx]);
        text(2U, 48U, "LONG"); number(62U, 48U, btn_long_counts[idx]);
    }
}


void watch_ui_notify_button(uint32_t event, uint32_t now_ms)
{
    (void)now_ms;

    /* 遍历所有按键的所有事件，分别饱和计数 */
    for (int i = 0; i < 3; i++) {
        /* 检查该按键的4种事件 */
        uint32_t event_bits[] = {1U << (i*4+0), 1U << (i*4+1), 1U << (i*4+2), 1U << (i*4+3)};
        uint32_t *count_ptrs[] = {&btn_press_counts[i], &btn_release_counts[i],
                                   &btn_short_counts[i], &btn_long_counts[i]};

        uint8_t found_event = 0;

        /* 检查该按键是否有任何事件发生 */
        if (event & (event_bits[0] | event_bits[1] | event_bits[2] | event_bits[3])) {
            found_event = 1;
        }

        if (found_event) {
            /* 更新最近键索引 */
            btn_last_key_idx = i;

            /* 更新最近事件名，SHORT优先于RELEASE，LONG优先于PRESS */
            if ((event & event_bits[2])) {
                btn_last_event_name[i] = "SHORT";
            } else if ((event & event_bits[3])) {
                btn_last_event_name[i] = "LONG";
            } else if ((event & event_bits[1])) {
                btn_last_event_name[i] = "RELEASE";
            } else if ((event & event_bits[0])) {
                btn_last_event_name[i] = "PRESS";
            }

            /* 累计计数：遍历所有4种事件 */
            for (int e = 0; e < 4; e++) {
                if (event & event_bits[e]) {
                    if (*count_ptrs[e] < 999999) {
                        (*count_ptrs[e])++;
                    }
                }
            }
        }
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
        phase = ever_initialized ? UI_BUTTON_CHECK : UI_WHITE;
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
        if (phase != UI_BUTTON_CHECK) { holding = true; hold_started_ms = now_ms; }
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
        phase = phase == UI_WHITE ? UI_CHECKER : phase == UI_CHECKER ? UI_CORNERS : UI_BUTTON_CHECK;
        holding = false;
        if (phase == UI_BUTTON_CHECK) { last_refresh_ms = now_ms - 1000U; }
    }
    if (phase == UI_OFF || ssd1306_is_busy()) { return WATCH_OK; }
    if ((phase == UI_BUTTON_CHECK) && (uint32_t)(now_ms - last_refresh_ms) < 100U) { return WATCH_OK; }
    draw(now_ms);
    watch_status_t status = ssd1306_flush(frame, sizeof(frame));
    if (status == WATCH_OK) { frame_pending = true; last_refresh_ms = now_ms; }
    return status;
}
