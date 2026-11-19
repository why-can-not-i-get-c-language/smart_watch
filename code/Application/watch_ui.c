#include "watch_ui.h"
#include "buttons.h"
#include "ssd1306.h"
#include "time_service.h"
#include "tm6605.h"
#include "mpu6050.h"
#include "max30102.h"
#include "pedometer.h"
#include "wrist_wake.h"
#include "battery_adc.h"
#include "board_config.h"
#include "ble_link.h"
#include <string.h>
#include <math.h>

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
/* F08-P01 BAT ADC页作为首页，KEY长按循环：BAT ADC、PPG、BLE、WRIST、STEPS、MOTION、HAPTIC、TIME */
typedef enum { PAGE_BAT, PAGE_PPG, PAGE_BLE, PAGE_WRIST, PAGE_STEPS, PAGE_MOTION, PAGE_HAPTIC, PAGE_TIME, PAGE_COUNT } ui_page_t;
static ui_page_t active_page = PAGE_BAT;
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
/* 正常页的目标亮灭状态与自检UI_OFF、驱动已ACK状态分开维护。 */
static bool idle_started;
static bool idle_sleeping;
static uint32_t last_activity_ms;
static uint8_t keys_down;
static uint8_t keys_blocked;

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

#if WATCH_BLE_NAME_SETUP
watch_status_t watch_ui_ble_name_setup(bool attempted, watch_status_t result, uint32_t now_ms)
{
    if (!ssd1306_is_ready()) {
        if (attempted_init && (uint32_t)(now_ms - last_init_ms) < 1000U) {
            return ssd1306_get_status();
        }
        attempted_init = true;
        last_init_ms = now_ms;
        watch_status_t status = ssd1306_init();
        if (status != WATCH_OK) { return status; }
        frame_pending = false;
        screen_enabled = false;
        last_refresh_ms = now_ms - 1000U;
    }
    if (ssd1306_is_busy()) { return WATCH_OK; }
    if (frame_pending) {
        watch_status_t status = ssd1306_set_enabled(true);
        if (status != WATCH_OK) { return status; }
        screen_enabled = true;
        frame_pending = false;
    }
    if ((uint32_t)(now_ms - last_refresh_ms) < 250U) { return WATCH_OK; }
    memset(frame, 0, sizeof(frame));
    text(0U, 0U, "BLE NAME SETUP 02");
    text(0U, 12U, "NAME SMARTWATCH");
    if (!attempted) {
        text(0U, 28U, "OLD AT NEW AUTO");
        text(0U, 40U, "KEY SHORT TO SET");
        text(0U, 54U, "UART 115200 8N1");
    } else if (result == WATCH_NOT_READY) {
        text(0U, 28U, "CONFIGURING");
        text(0U, 40U, "WAIT UP TO 8 SEC");
        text(0U, 54U, "DO NOT PRESS AGAIN");
    } else if (result == WATCH_OK) {
        text(0U, 28U, "MODULE ACK OK");
        text(0U, 40U, "POWER OFF THEN NORM");
        text(0U, 54U, "RESTORE NORMAL FW");
    } else {
        ble_link_state_t state;
        (void)ble_link_get_state(&state);
        text(0U, 28U, result == WATCH_TIMEOUT ?
                         (ble_link_setup_stage() == BLE_NAME_SET ? "SET NAME TIMEOUT" :
                          ble_link_setup_stage() == BLE_NAME_ENTER ? "ENTER AT TIMEOUT" : "QUERY NAME TIMEOUT") :
                         result == WATCH_INVALID_DATA ? "MODULE ERROR" : "UART NOT READY");
        text(0U, 40U, state.rx_bytes == 0U ? "NO RX BYTES" : "RX DATA SEEN");
        text(0U, 54U, "RESET TO TRY AGAIN");
    }
    watch_status_t status = ssd1306_flush(frame, sizeof(frame));
    if (status == WATCH_OK) { frame_pending = true; last_refresh_ms = now_ms; }
    return status;
}
#endif

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
    else if (counter.state == PEDOMETER_VERIFYING) { state = "CHECK WALK"; }
    if (!motion.sample.valid) { state = "WAIT SENSOR"; }
    if (motion.sample.valid && steps_cleared && (uint32_t)(now_ms - steps_cleared_ms) < 2000U) {
        state = "COUNT CLEARED";
    }
    text(2U, 24U, state);
    if (motion.sample.valid && counter.state == PEDOMETER_VERIFYING &&
        !(steps_cleared && (uint32_t)(now_ms - steps_cleared_ms) < 2000U)) {
        number(68U, 24U, counter.pending_steps);
        text(80U, 24U, "OF"); number(98U, 24U, WATCH_PEDOMETER_CONFIRM_STEPS);
    }
    text(2U, 32U, "ACT HZ");
    if (motion.rate_valid) {
        fixed_number(44U, 32U, motion.actual_rate_millihz / 1000U, 2U);
        text(56U, 32U, "."); number(62U, 32U, (motion.actual_rate_millihz % 1000U) / 100U);
    } else { text(44U, 32U, "--"); }
    text(2U, 40U, motion.sample.valid ? "MPU ONLINE" : "CHECK SENSOR");
    text(2U, 48U, "MENU HOLD CLEAR");
    text(2U, 56U, "KEY HOLD PAGE");
}

static void draw_wrist_page(void)
{
    wrist_wake_status_t wrist;
    (void)wrist_wake_get_state(&wrist);
    static const char *poses[] = {
        "NOT SET", "HOLD STILL", "WAIT AWAY", "ARMED", "HOLD VIEW",
        "TURN TOO FAST", "COOLDOWN", "TRIGGERED", "SENSOR ERROR", "SET FAILED",
        "ACCEL RANGE"
    };
    text(32U, 0U, "WRIST CHECK");
    text(2U, 8U, wrist.calib_state == WRIST_CALIB_DONE ? "REF SET" :
         wrist.calib_state == WRIST_CALIB_PENDING ? "REF CAPTURING" : "REF NOT SET");
    text(2U, 16U, (unsigned)wrist.pose < sizeof(poses) / sizeof(poses[0]) ? poses[wrist.pose] : "STATE ERROR");
    text(2U, 24U, wrist.pose == WRIST_POSE_SENSOR_ERROR ||
         wrist.pose == WRIST_POSE_ACCEL_RANGE ? "POSE INVALID" :
         wrist.calib_state != WRIST_CALIB_DONE ? "POSE NOT SET" :
         wrist.in_view ? "POSE NEAR VIEW" : "POSE AWAY");
    text(2U, 32U, "EVENTS"); number(44U, 32U, wrist.event_count);
    text(2U, 40U, "MENU HOLD SET VIEW");
    /* 检查页显示缓存样本的合量计算值，不增加传感器读取。 */
    mpu6050_state_t motion;
    mpu6050_get_state(&motion);
    text(2U, 48U, "ACC NORM MG");
    text(2U, 56U, "GYRO NORM DPS");
    float a2 = motion.sample.ax_g * motion.sample.ax_g + motion.sample.ay_g * motion.sample.ay_g +
               motion.sample.az_g * motion.sample.az_g;
    float g2 = motion.sample.gx_dps * motion.sample.gx_dps + motion.sample.gy_dps * motion.sample.gy_dps +
               motion.sample.gz_dps * motion.sample.gz_dps;
    if (motion.sample.valid && isfinite(a2) && isfinite(g2) && a2 <= 192.0f && g2 <= 3000000.0f) {
        number(86U, 48U, (uint32_t)(sqrtf(a2) * 1000.0f + 0.5f));
        number(86U, 56U, (uint32_t)(sqrtf(g2) + 0.5f));
    } else {
        text(86U, 48U, "--");
        text(86U, 56U, "--");
    }
}

static void draw_ppg_page(void)
{
    max30102_state_t state;
    max30102_get_state(&state);
    text(2U, 0U, "PPG RAW CHECK");
    text(2U, 8U, "ID");
    static const char hex[] = "0123456789ABCDEF";
    if (state.identity_read) {
        character(20U, 8U, hex[state.part_id >> 4U]);
        character(26U, 8U, hex[state.part_id & 15U]);
    } else { text(20U, 8U, "--"); }
    text(44U, 8U, "CFG 100HZ");
    text(2U, 16U, "RED");
    text(2U, 24U, "IR");
    if (state.sample.valid) {
        number(32U, 16U, state.sample.red);
        number(32U, 24U, state.sample.infrared);
    } else {
        text(32U, 16U, "--"); text(32U, 24U, "--");
    }
    text(2U, 32U, "N");
    number(14U, 32U, state.sample_count > 999999U ? 999999U : state.sample_count);
    text(2U, 40U, "OVF");
    number(26U, 40U, state.overflow_count > 9999U ? 9999U : state.overflow_count);
    text(62U, 40U, "E");
    number(74U, 40U, state.error_count > 999999U ? 999999U : state.error_count);
    const char *signal = state.status == WATCH_TIMEOUT ? "TIMEOUT RETRY" :
        state.status == WATCH_IO_ERROR ? "IO ERROR RETRY" :
        state.status == WATCH_INVALID_DATA ? "DATA ERROR RETRY" :
        !state.ready ? "STARTING RETRY" :
        !state.sample.valid ? "WAIT RAW DATA" :
        state.saturated ? "SATURATED" : state.low_signal ? "LOW SIGNAL" : "RAW ONLY";
    text(2U, 48U, signal);
    text(2U, 56U, "T MS");
    if (state.sample.valid) { number(32U, 56U, state.sample.timestamp_ms); }
    else { text(32U, 56U, "--"); }
}

static void draw_ble_page(void)
{
    ble_link_state_t state;
    (void)ble_link_get_state(&state);
    text(2U, 0U, "BLE UART CHECK");
    text(2U, 8U, "UART"); number(32U, 8U, WATCH_UART1_BAUDRATE); text(80U, 8U, "8N1");
    text(2U, 16U, "LINK UNKNOWN");
    text(2U, 24U, "RX B"); number(38U, 24U, state.rx_bytes);
    text(2U, 32U, "TX B"); number(38U, 32U, state.tx_bytes);
    bool error = state.rx_status != WATCH_OK && state.rx_status != WATCH_NOT_READY;
    error = error || (state.tx_status != WATCH_OK && state.tx_status != WATCH_NOT_READY);
    text(2U, 40U, !state.initialized ? "UART NOT READY" : error ? "IO ERROR" :
         state.rx_bytes == 0U ? "WAIT RX" : "RX SEEN");
    /* 两层错误计数取较大值，避免同一UART错误双重累计。 */
    uint32_t errors = state.errors > state.uart_errors ? state.errors : state.uart_errors;
    text(86U, 40U, "E");
    number(92U, 40U, errors > 99999U ? 99999U : errors);
    if (errors > 99999U) { character(122U, 40U, '+'); }
    text(2U, 48U, "LAST");
    static const char hex[] = "0123456789ABCDEF";
    if (state.last_rx_length == 0U) { text(38U, 48U, "--"); }
    for (unsigned i = 0U; i < state.last_rx_length && i < 4U; ++i) {
        character(38U + 18U * i, 48U, hex[state.last_rx[i] >> 4U]);
        character(44U + 18U * i, 48U, hex[state.last_rx[i] & 15U]);
    }
    text(2U, 56U, "MENU SEND TEST");
}

static void draw_battery_page(void)
{
    battery_adc_state_t bat;
    (void)battery_adc_get_state(&bat);
    text(26U, 0U, "BAT ADC CHECK");

    /* 第2行：RAW */
    text(2U, 8U, "RAW");
    if (bat.valid) { number(26U, 8U, bat.raw_pin); }
    else { text(26U, 8U, "--"); }

    /* 第3行：REF */
    text(2U, 16U, "REF");
    if (bat.valid) { number(26U, 16U, bat.raw_vref); }
    else { text(26U, 16U, "--"); }

    /* 第4行：VDD MV */
    text(2U, 24U, "VDD MV");
    if (bat.valid) { number(44U, 24U, bat.vdda_mv); }
    else { text(44U, 24U, "--"); }

    /* 第5行：PIN MV */
    text(2U, 32U, "PIN MV");
    if (bat.valid) { number(44U, 32U, bat.pin_mv); }
    else { text(44U, 32U, "--"); }

    /* 第6行：BAT MV */
    text(2U, 40U, "BAT MV");
    if (bat.valid) { number(44U, 40U, bat.battery_mv); }
    else { text(44U, 40U, "--"); }

    /* 第7行：N E */
    text(2U, 48U, "N"); number(14U, 48U, bat.sample_count > 999999U ? 999999U : bat.sample_count);
    text(62U, 48U, "E"); number(74U, 48U, bat.error_count > 999999U ? 999999U : bat.error_count);

    /* 第8行：状态 */
    const char *status_text = "VOLT ONLY";
    if (!bat.valid) {
        if (bat.status == WATCH_NOT_READY) { status_text = "STARTING"; }
        else if (bat.status == WATCH_TIMEOUT) { status_text = "TIMEOUT RETRY"; }
        else if (bat.status == WATCH_IO_ERROR) { status_text = "IO ERROR RETRY"; }
        else if (bat.status == WATCH_INVALID_DATA) { status_text = "DATA ERROR RETRY"; }
        else if (bat.status == WATCH_NOT_IMPLEMENTED) { status_text = "NOT IMPLEMENTED"; }
        else { status_text = "STALE RETRY"; }
    }
    text(2U, 56U, status_text);
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
    } else if (phase == UI_NORMAL && active_page == PAGE_BAT) {
        draw_battery_page();
    } else if (phase == UI_NORMAL && active_page == PAGE_PPG) {
        draw_ppg_page();
    } else if (phase == UI_NORMAL && active_page == PAGE_BLE) {
        draw_ble_page();
    } else if (phase == UI_NORMAL && active_page == PAGE_WRIST) {
        draw_wrist_page();
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
    if (phase == UI_NORMAL && active_page == PAGE_WRIST) {
        if ((event & BUTTON_MENU_LONG) != 0U && (event & BUTTON_SOS_LONG) == 0U) {
            action_status = wrist_wake_calibrate(now_ms);
            last_refresh_ms = now_ms - 1000U;
        }
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



void watch_ui_wake(uint32_t now_ms)
{
    if (phase != UI_NORMAL && phase != UI_EDIT) { return; }
    idle_started = true;
    last_activity_ms = now_ms;
    if (idle_sleeping) {
        idle_sleeping = false;
        /* 在途旧帧可继续发送，但不能用它开屏；随后重新排队当前页。 */
        frame_pending = false;
        last_refresh_ms = now_ms - 1000U;
    }
}

bool watch_ui_is_sleeping(void)
{
    return idle_sleeping;
}

uint32_t watch_ui_filter_buttons(uint32_t events, uint32_t now_ms)
{
    uint8_t present = 0U;
    for (unsigned key = 0U; key < 3U; ++key) {
        uint32_t group = (events >> (4U * key)) & 15U;
        if (group != 0U) { present |= (uint8_t)(1U << key); }
        if ((group & 1U) != 0U) { keys_down |= (uint8_t)(1U << key); }
    }
    if (present != 0U && (phase == UI_NORMAL || phase == UI_EDIT)) {
        /* 逻辑已唤醒但新帧尚未开屏时，按键仍只用于唤醒。 */
        if (idle_sleeping || !screen_enabled || !ssd1306_is_ready()) {
            keys_blocked |= (uint8_t)(keys_down | present);
        }
        watch_ui_wake(now_ms);
    }
    uint32_t filtered = events;
    for (unsigned key = 0U; key < 3U; ++key) {
        uint8_t bit = (uint8_t)(1U << key);
        uint32_t mask = 15U << (4U * key);
        if ((keys_blocked & bit) != 0U) { filtered &= ~mask; }
        /* 先过滤再解锁，RELEASE和SHORT同帧时不会漏出功能事件。 */
        if ((events & (2U << (4U * key))) != 0U) {
            keys_down &= (uint8_t)~bit;
            keys_blocked &= (uint8_t)~bit;
        }
    }
    return filtered;
}

watch_status_t watch_ui_get_action_status(void)
{
    return action_status;
}

watch_status_t watch_ui_update(uint32_t now_ms)
{
    /* 即使屏幕掉线也继续计算空闲时间；重连不产生唤醒事件。 */
    if (phase == UI_NORMAL || phase == UI_EDIT) {
        if (!idle_started) { idle_started = true; last_activity_ms = now_ms; }
#if WATCH_SCREEN_TIMEOUT_MS > 0U
        if ((uint32_t)(now_ms - last_activity_ms) >= WATCH_SCREEN_TIMEOUT_MS) {
            idle_sleeping = true;
        }
#endif
    }
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

    if (idle_sleeping && (phase == UI_NORMAL || phase == UI_EDIT)) {
        frame_pending = false;
        if (screen_enabled) {
            watch_status_t status = ssd1306_set_enabled(false);
            if (status != WATCH_OK) { return status; }
            screen_enabled = false;
        }
        return WATCH_OK;
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
        if (phase == UI_NORMAL) {
            last_refresh_ms = now_ms - 1000U;
            action_status = WATCH_NOT_READY;
            idle_started = true;
            last_activity_ms = now_ms;
        }
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

bool watch_ui_ble_active(void)
{
    return phase == UI_NORMAL && active_page == PAGE_BLE;
}
