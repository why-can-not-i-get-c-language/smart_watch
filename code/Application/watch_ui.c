#include "watch_ui.h"
#include "buttons.h"
#include "ssd1306.h"
#include "time_service.h"
#include "tm6605.h"
#include "mpu6050.h"
#include "pedometer.h"
#include "board_config.h"
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
    {'+',{0,4,4,31,4,4,0}}, {'.',{0,0,0,0,0,4,4}}, {'?',{14,17,1,2,4,0,4}}
};

typedef enum { UI_WHITE, UI_CHECKER, UI_CORNERS, UI_OFF, UI_NORMAL, UI_EDIT } ui_phase_t;
typedef enum { EDIT_YEAR=0, EDIT_MONTH, EDIT_DAY, EDIT_HOUR, EDIT_MINUTE, EDIT_SECOND, EDIT_COUNT } edit_field_t;

static uint8_t frame[SSD1306_FRAME_SIZE];
static ui_phase_t phase;
typedef enum { PAGE_STEPS, PAGE_MOTION, PAGE_HAPTIC, PAGE_TIME, PAGE_COUNT } ui_page_t;
static ui_page_t active_page = PAGE_STEPS;
static bool steps_cleared;
static uint32_t steps_cleared_ms;
static bool ever_initialized;
static bool attempted_init;
static bool frame_pending;
static bool holding;
static bool screen_enabled;
static uint32_t last_init_ms;
static uint32_t hold_started_ms;
static uint32_t last_refresh_ms;

/* 编辑状态机 */
static rtc_calendar_t edit_draft;
static edit_field_t edit_field;
static watch_status_t action_status = WATCH_NOT_READY;
static const char *notice = "";

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
    if (value == 0) { character(x, y, '0'); return; }
    do { digits[used++] = (char)('0' + value % 10U); value /= 10U; } while (value != 0U && used < 10);
    while (used != 0U && x + 5U <= SSD1306_WIDTH) {
        character(x, y, digits[--used]);
        x += 6U;
    }
}

static void fixed_number(unsigned x, unsigned y, uint32_t value, unsigned width)
{
    char digits[5];
    digits[width] = '\0';
    for (unsigned i = width; i != 0U; --i) {
        digits[i - 1U] = (char)('0' + value % 10U);
        value /= 10U;
    }
    text(x, y, digits);
}

static uint8_t month_days(uint16_t year, uint8_t month)
{
    static const uint8_t days[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (month < 1U || month > 12U) { return 0U; }
    bool is_leap = (year % 4U == 0U && year % 100U != 0U) || year % 400U == 0U;
    return month == 2U && is_leap ? 29U : days[month - 1U];
}

static void draw_haptic_page(uint32_t now_ms)
{
    tm6605_state_t current;
    tm6605_get_state(&current);
    static const char *commands[] = {"NONE","INIT","PLAY","STOP","SLEEP","WAKE","ALERT"};
    text(28U, 0U, "HAPTIC CHECK");
    text(2U, 8U, "CMD"); text(38U, 8U, commands[current.command]);
    const char *status = "NOT READY";
    if (current.busy) { status = "PENDING"; }
    else if (current.status == WATCH_OK) { status = "LAST ACK OK"; }
    else if (current.status == WATCH_IO_ERROR) { status = "IO ERROR"; }
    else if (current.status == WATCH_TIMEOUT) { status = "TIMEOUT"; }
    else if (current.status == WATCH_INVALID_ARGUMENT) { status = "BAD EFFECT"; }
    else if (current.status == WATCH_NOT_IMPLEMENTED) { status = "UNSUPPORTED"; }
    text(2U, 16U, status);
    text(2U, 24U, "EFFECT"); number(50U, 24U, current.effect);
    text(2U, 32U, "UPTIME S"); number(62U, 32U, now_ms / 1000U);
    text(2U, 40U, "MENU TAP14 HOLD RPT");
    text(2U, 48U, "KEY TAP STOP HOLD UI");
    text(2U, 56U, "SOS TAP SLP HOLD WK");
}

static void signed_milli(unsigned x, unsigned y, float value)
{
    int32_t milli = (int32_t)(value * 1000.0f + (value >= 0.0f ? 0.5f : -0.5f));
    character(x, y, milli < 0 ? '-' : '+');
    number(x + 6U, y, (uint32_t)(milli < 0 ? -milli : milli));
}

static void draw_motion_page(void)
{
    mpu6050_state_t motion;
    mpu6050_get_state(&motion);
    text(2U, 0U, "MPU6050 8G CFG");
    number(86U, 0U, WATCH_MPU_SAMPLE_HZ); text(104U, 0U, "HZ");
    text(2U, 8U, "ID");
    if (motion.identity_read) {
        static const char hex[] = "0123456789ABCDEF";
        character(20U, 8U, hex[motion.who_am_i >> 4]);
        character(26U, 8U, hex[motion.who_am_i & 15U]);
    } else { text(20U, 8U, "--"); }
    const char *status = "INIT";
    if (motion.status == WATCH_IO_ERROR) { status = "IO RETRY"; }
    else if (motion.status == WATCH_TIMEOUT) { status = "TIMEOUT"; }
    else if (motion.status == WATCH_INVALID_DATA) {
        status = motion.who_am_i != 0x68U ? "BAD ID" : "BAD CONFIG";
    } else if (motion.ready) { status = motion.sample.valid ? "ONLINE" : "WAIT DATA"; }
    text(44U, 8U, status);
    if (motion.sample.valid) {
        text(2U, 16U, "AX MG"); signed_milli(44U, 16U, motion.sample.ax_g);
        text(2U, 24U, "AY MG"); signed_milli(44U, 24U, motion.sample.ay_g);
        text(2U, 32U, "AZ MG"); signed_milli(44U, 32U, motion.sample.az_g);
        text(2U, 40U, "HOST MS"); number(50U, 40U, motion.sample.timestamp_ms);
    } else {
        text(2U, 16U, motion.status == WATCH_INVALID_DATA && motion.who_am_i != 0x68U ?
             "CHECK CHIP" : motion.status == WATCH_IO_ERROR || motion.status == WATCH_TIMEOUT ||
             motion.status == WATCH_INVALID_DATA ? "CHECK WIRE" : "WAIT SAMPLE");
        text(2U, 24U, "ACCEL INVALID");
        text(2U, 40U, "HOST MS --");
    }
    text(2U, 48U, "ACT HZ");
    if (motion.rate_valid) {
        fixed_number(44U, 48U, motion.actual_rate_millihz / 1000U, 2U);
        text(56U, 48U, "."); number(62U, 48U, (motion.actual_rate_millihz % 1000U) / 100U);
    } else { text(44U, 48U, "--"); }
    text(2U, 56U, "N"); number(14U, 56U, motion.sample_count);
    text(86U, 56U, "KEY UI");
}

static void draw_steps_page(uint32_t now_ms)
{
    pedometer_status_t counter;
    mpu6050_state_t motion;
    (void)pedometer_get_state(&counter);
    mpu6050_get_state(&motion);
    text(32U, 0U, "STEP COUNT");
    text(2U, 8U, "SESSION RAM");
    text(2U, 16U, "STEPS"); number(44U, 16U, counter.step_count);
    const char *state = "WAIT SENSOR";
    if (counter.state == PEDOMETER_WARMING) { state = "WARMUP"; }
    else if (counter.state == PEDOMETER_READY) { state = "READY"; }
    if (!motion.sample.valid) { state = "WAIT SENSOR"; }
    if (motion.sample.valid && steps_cleared && (uint32_t)(now_ms - steps_cleared_ms) < 2000U) {
        state = "COUNT CLEARED";
    }
    text(2U, 24U, state);
    text(2U, 32U, "ACT HZ");
    if (motion.rate_valid) {
        fixed_number(44U, 32U, motion.actual_rate_millihz / 1000U, 2U);
        text(56U, 32U, "."); number(62U, 32U, (motion.actual_rate_millihz % 1000U) / 100U);
    } else { text(44U, 32U, "--"); }
    text(2U, 40U, motion.sample.valid ? "MPU ONLINE" : "CHECK SENSOR");
    text(2U, 48U, "MENU HOLD CLEAR");
    text(2U, 56U, "KEY HOLD PAGE");
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
    } else if (phase == UI_NORMAL && active_page == PAGE_STEPS) {
        draw_steps_page(now_ms);
    } else if (phase == UI_NORMAL && active_page == PAGE_MOTION) {
        draw_motion_page();
    } else if (phase == UI_NORMAL && active_page == PAGE_HAPTIC) {
        draw_haptic_page(now_ms);
    } else if (phase == UI_NORMAL) {
        uint32_t utc = 0U;
        rtc_calendar_t local;
        watch_status_t status = time_service_get(&utc);
        bool clock_ready = time_service_clock_source() == TIME_CLOCK_LSE;
        bool valid = clock_ready && status == WATCH_OK && time_utc_to_local(utc, &local) == WATCH_OK;
        text(31U, 0U, "SMART WATCH");
        text(2U, 8U, "BJT UTC+8");
        if (valid) {
            fixed_number(2U, 16U, local.year, 4U); text(26U, 16U, "-");
            fixed_number(32U, 16U, local.month, 2U); text(44U, 16U, "-");
            fixed_number(50U, 16U, local.day, 2U);
            fixed_number(2U, 24U, local.hour, 2U); text(14U, 24U, ":");
            fixed_number(20U, 24U, local.minute, 2U); text(32U, 24U, ":");
            fixed_number(38U, 24U, local.second, 2U);
        } else {
            text(2U, 16U, clock_ready && status == WATCH_NOT_READY ? "TIME NOT SET" : "RTC ERROR");
        }
        text(2U, 32U, "UPTIME S"); number(62U, 32U, now_ms / 1000U);
        text(2U, 40U, "MENU HOLD SET");
        if (valid && action_status == WATCH_OK) { text(2U, 48U, "SAVED"); }
        else if (clock_ready && notice[0] != '\0' && action_status == WATCH_NOT_READY) { text(2U, 48U, notice); }
        text(2U, 56U, clock_ready ? "CLK LSE" : "CLK ERROR");
    } else if (phase == UI_EDIT) {
        static const char *fields[] = {"YEAR","MONTH","DAY","HOUR","MIN","SEC"};
        text(2U, 0U, "SET BJT UTC+8");
        fixed_number(2U, 8U, edit_draft.year, 4U); text(26U, 8U, "-");
        fixed_number(32U, 8U, edit_draft.month, 2U); text(44U, 8U, "-");
        fixed_number(50U, 8U, edit_draft.day, 2U);
        fixed_number(2U, 16U, edit_draft.hour, 2U); text(14U, 16U, ":");
        fixed_number(20U, 16U, edit_draft.minute, 2U); text(32U, 16U, ":");
        fixed_number(38U, 16U, edit_draft.second, 2U);
        text(2U, 24U, "FIELD"); text(38U, 24U, fields[edit_field]);
        text(2U, 32U, "KEY INC SOS DEC");
        text(2U, 40U, "MENU NEXT HOLD SAVE");
        text(2U, 48U, "SOS HOLD CANCEL");
        text(2U, 56U, notice);
    }
}

static void adjust_field(int direction)
{
    uint16_t value = 0U, low = 0U, high = 0U;
    switch (edit_field) {
    case EDIT_YEAR: value = edit_draft.year; low = 2000U; high = 2099U; break;
    case EDIT_MONTH: value = edit_draft.month; low = 1U; high = 12U; break;
    case EDIT_DAY: value = edit_draft.day; low = 1U; high = month_days(edit_draft.year, edit_draft.month); break;
    case EDIT_HOUR: value = edit_draft.hour; high = 23U; break;
    case EDIT_MINUTE: value = edit_draft.minute; high = 59U; break;
    case EDIT_SECOND: value = edit_draft.second; high = 59U; break;
    default: return;
    }
    value = direction > 0 ? (value >= high ? low : (uint16_t)(value + 1U)) :
                            (value <= low ? high : (uint16_t)(value - 1U));
    switch (edit_field) {
    case EDIT_YEAR: edit_draft.year = value; break;
    case EDIT_MONTH: edit_draft.month = (uint8_t)value; break;
    case EDIT_DAY: edit_draft.day = (uint8_t)value; break;
    case EDIT_HOUR: edit_draft.hour = (uint8_t)value; break;
    case EDIT_MINUTE: edit_draft.minute = (uint8_t)value; break;
    case EDIT_SECOND: edit_draft.second = (uint8_t)value; break;
    default: return;
    }
    /* 修改年份或月份后夹到合法月末，避免生成2月30日等草稿。 */
    uint8_t maximum = month_days(edit_draft.year, edit_draft.month);
    if (edit_draft.day > maximum) { edit_draft.day = maximum; }
    action_status = WATCH_NOT_READY;
    notice = "EDITING";
}

void watch_ui_notify_button(uint32_t event, uint32_t now_ms)
{
    if (phase == UI_NORMAL && (event & BUTTON_BOARD_LONG) != 0U) {
        active_page = (ui_page_t)(((unsigned)active_page + 1U) % PAGE_COUNT);
        last_refresh_ms = now_ms - 1000U;
        return;
    }
    if (phase == UI_NORMAL && active_page == PAGE_STEPS) {
        if ((event & BUTTON_MENU_LONG) != 0U && (event & BUTTON_SOS_LONG) == 0U) {
            action_status = pedometer_reset();
            steps_cleared = action_status == WATCH_OK;
            steps_cleared_ms = now_ms;
            last_refresh_ms = now_ms - 1000U;
        }
        return;
    }
    if (phase == UI_NORMAL && active_page != PAGE_TIME) { return; }

    /* 自检期间不进入编辑；组合事件按取消、保存、切字段、加减的顺序处理。 */
    if (phase != UI_NORMAL && phase != UI_EDIT) { return; }
    if ((event & BUTTON_SOS_LONG) != 0U) {
        if (phase == UI_EDIT) {
            phase = UI_NORMAL;
            action_status = WATCH_NOT_READY;
            notice = "CANCELLED";
        }
    } else if ((event & BUTTON_MENU_LONG) != 0U) {
        if (phase == UI_NORMAL) {
            uint32_t utc = 0U;
            if (time_service_get(&utc) != WATCH_OK || time_utc_to_local(utc, &edit_draft) != WATCH_OK) {
                edit_draft = (rtc_calendar_t){2026U,1U,1U,0U,0U,0U};
            }
            /* UTC支持上界最后8小时可显示2100年，编辑年份仍限定2000至2099。 */
            if (edit_draft.year > 2099U) { edit_draft.year = 2099U; }
            edit_field = EDIT_YEAR;
            action_status = WATCH_NOT_READY;
            notice = "EDITING";
            phase = UI_EDIT;
        } else {
            uint32_t utc = 0U;
            action_status = time_local_to_utc(&edit_draft, &utc);
            if (action_status == WATCH_OK) { action_status = time_service_set(utc); }
            if (action_status == WATCH_OK) {
                phase = UI_NORMAL;
                notice = "SAVED";
            } else {
                notice = action_status == WATCH_INVALID_ARGUMENT ? "OUT OF RANGE" : "SAVE FAILED";
            }
        }
    } else if (phase == UI_EDIT && (event & BUTTON_MENU_SHORT) != 0U) {
        edit_field = (edit_field_t)(((unsigned)edit_field + 1U) % EDIT_COUNT);
    } else if (phase == UI_EDIT) {
        bool increase = (event & BUTTON_BOARD_SHORT) != 0U;
        bool decrease = (event & BUTTON_SOS_SHORT) != 0U;
        if (increase != decrease) { adjust_field(increase ? 1 : -1); }
    }
    last_refresh_ms = now_ms - 1000U;
}



watch_status_t watch_ui_get_action_status(void)
{
    return action_status;
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
        phase = ever_initialized ? (phase == UI_EDIT ? UI_EDIT : UI_NORMAL) : UI_WHITE;
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
        if (phase != UI_NORMAL && phase != UI_EDIT) { holding = true; hold_started_ms = now_ms; }
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
        phase = phase == UI_WHITE ? UI_CHECKER : phase == UI_CHECKER ? UI_CORNERS : UI_NORMAL;
        holding = false;
        if (phase == UI_NORMAL) { last_refresh_ms = now_ms - 1000U; action_status = WATCH_NOT_READY; }
    }
    if (phase == UI_OFF || ssd1306_is_busy()) { return WATCH_OK; }
    if ((phase == UI_NORMAL || phase == UI_EDIT) && (uint32_t)(now_ms - last_refresh_ms) < 100U) { return WATCH_OK; }
    draw(now_ms);
    watch_status_t status = ssd1306_flush(frame, sizeof(frame));
    if (status == WATCH_OK) { frame_pending = true; last_refresh_ms = now_ms; }
    return status;
}

bool watch_ui_haptic_active(void)
{
    return phase == UI_NORMAL && active_page == PAGE_HAPTIC;
}
