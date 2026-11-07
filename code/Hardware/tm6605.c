#include "tm6605.h"
#include "i2c_bus.h"
#include "board_config.h"
#include "board_port.h"

#ifdef WATCH_TARGET_STM32
#define TM6605_REG_CONTROL 0x0CU
#define TM6605_REG_EFFECT 0x04U
#define TM6605_SLEEP_EFFECT 123U
/* 编号与参考时长来自TM6605 V1.1第5页，不是忙状态测量。 */
static bool is_valid_effect(uint8_t effect) {
    static const uint8_t valid[] = {1,4,7,10,13,14,15,17,21,24,27,31,34,37,41,44,47,58,
                                     70,71,72,73,74,75,76,77,78,79,80,81,82,83,84,85,86,87,88,89,
                                     90,91,92,93,118,119};
    for (size_t i = 0; i < sizeof(valid); ++i) {
        if (valid[i] == effect) return true;
    }
    return false;
}

/* 获取效果时长(ms)，用于本地保护计时 */
static uint32_t get_effect_duration(uint8_t effect) {
    static const struct {uint8_t eff; uint16_t ms;} times[] = {
        {1,65}, {4,45}, {7,130}, {10,200}, {13,215}, {14,190}, {15,730}, {17,90}, {21,65}, {24,20},
        {27,120}, {31,120}, {34,100}, {37,150}, {41,150}, {44,150}, {47,240}, {58,620},
        {70,390}, {71,620}, {72,400}, {73,650}, {74,410}, {75,490}, {76,340}, {77,390}, {78,310}, {79,360},
        {80,340}, {81,350}, {82,320}, {83,650}, {84,310}, {85,640}, {86,320}, {87,460}, {88,290}, {89,615},
        {90,320}, {91,590}, {92,330}, {93,470}, {118,10000}, {119,480}
    };
    for (size_t i = 0; i < sizeof(times)/sizeof(times[0]); ++i) {
        if (times[i].eff == effect) return times[i].ms;
    }
    return 1000U;
}


typedef enum { PH_IDLE, PH_PROBE, PH_STOP1, PH_STOP2, PH_SELECT, PH_GO, PH_SLEEP } tx_phase_t;
static tm6605_state_t state = {.status = WATCH_NOT_READY};
static tx_phase_t tx_phase;
static uint32_t phase_ms, play_ms, guard_ms;
static uint32_t alert_start_ms, alert_duration_ms;
static bool guard_active, safety_active;

static void queue_command(tm6605_command_t command)
{
    state.command = command;
    state.busy = true;
    state.status = WATCH_NOT_READY;
    tx_phase = command == TM6605_INIT ? PH_PROBE : PH_STOP1;
    phase_ms = board_millis() - WATCH_HAPTIC_WAKE_GAP_MS;
    if (command != TM6605_PLAY && command != TM6605_ALERT) {
        /* 取消软件序列；总线ACK不代表已经中断芯片内部效果。 */
        state.alert_active = false;
        safety_active = false;
    }
}

watch_status_t tm6605_init(void)
{
    if (state.busy) { return WATCH_NOT_READY; }
    state.ready = false;
    state.effect = 0U;
    queue_command(TM6605_INIT);
    return WATCH_OK;
}

watch_status_t tm6605_play(uint8_t effect)
{
    if (!is_valid_effect(effect)) { state.status = WATCH_INVALID_ARGUMENT; return WATCH_INVALID_ARGUMENT; }
    if (!state.ready || state.busy || state.alert_active ||
        (guard_active && (uint32_t)(board_millis() - play_ms) < guard_ms)) {
        state.status = WATCH_NOT_READY;
        return WATCH_NOT_READY;
    }
    state.effect = effect;
    queue_command(TM6605_PLAY);
    return WATCH_OK;
}

watch_status_t tm6605_alert(uint32_t duration_ms)
{
    if (duration_ms == 0U || duration_ms > WATCH_HAPTIC_MAX_PLAY_MS) {
        state.status = WATCH_INVALID_ARGUMENT;
        return WATCH_INVALID_ARGUMENT;
    }
    if (!state.ready || state.busy || state.alert_active ||
        (guard_active && (uint32_t)(board_millis() - play_ms) < guard_ms)) {
        state.status = WATCH_NOT_READY;
        return WATCH_NOT_READY;
    }
    alert_start_ms = board_millis();
    alert_duration_ms = duration_ms;
    state.effect = 14U;
    state.alert_active = true;
    queue_command(TM6605_ALERT);
    return WATCH_OK;
}

watch_status_t tm6605_stop(void) { queue_command(TM6605_STOP); return WATCH_OK; }
watch_status_t tm6605_sleep(bool enabled)
{
    queue_command(enabled ? TM6605_SLEEP : TM6605_WAKE);
    return WATCH_OK;
}

static void complete(uint32_t now_ms)
{
    state.busy = false;
    state.ready = true;
    state.status = WATCH_OK;
    if (state.completed_count < 999999U) { state.completed_count++; }
    state.sleep_command_sent = state.command == TM6605_SLEEP;
    tx_phase = PH_IDLE;
    if (state.command == TM6605_PLAY || state.command == TM6605_ALERT) {
        play_ms = now_ms;
        guard_ms = get_effect_duration(state.effect) + 200U;
        guard_active = true;
        safety_active = state.command == TM6605_PLAY;
    }
}

watch_status_t tm6605_process(uint32_t now_ms)
{
    /* 到时也可取消仍在排队的SELECT/GO，防止慢总线在期限后启动一段。 */
    if (state.alert_active && (uint32_t)(now_ms - alert_start_ms) >= alert_duration_ms) {
        queue_command(TM6605_STOP);
    }
    if (tx_phase == PH_IDLE) {
        if (guard_active && (uint32_t)(now_ms - play_ms) >= guard_ms) { guard_active = false; }
        if (state.alert_active && !guard_active) {
            queue_command(TM6605_ALERT);
        } else if (safety_active && (uint32_t)(now_ms - play_ms) >= WATCH_HAPTIC_MAX_PLAY_MS) {
            queue_command(TM6605_STOP);
        }
        return WATCH_OK;
    }
    if ((uint32_t)(now_ms - phase_ms) < WATCH_HAPTIC_WAKE_GAP_MS) { return WATCH_OK; }
    watch_status_t result;
    if (tx_phase == PH_PROBE) {
        result = i2c_bus_probe(WATCH_HAPTIC_ADDR7);
    } else {
        uint8_t reg = TM6605_REG_CONTROL;
        uint8_t data = 0U;
        if (tx_phase == PH_SELECT) { reg = TM6605_REG_EFFECT; data = state.effect; }
        else if (tx_phase == PH_GO) { data = 1U; }
        else if (tx_phase == PH_SLEEP) { reg = TM6605_REG_EFFECT; data = TM6605_SLEEP_EFFECT; }
        result = i2c_bus_write_command(WATCH_HAPTIC_ADDR7, reg, data);
    }
    /* 一次推进最多一个传输；任一错误终止整个命令，禁止后续误报完成。 */
    if (result != WATCH_OK) {
        state.ready = false; state.busy = false; state.status = result;
        state.sleep_command_sent = false;
        state.alert_active = false;
        /* 保留已播放段的保护间隔；离线恢复ACK不证明该段已中断。 */
        safety_active = false; tx_phase = PH_IDLE;
        return result;
    }
    phase_ms = now_ms;
    switch (tx_phase) {
    case PH_PROBE: tx_phase = PH_STOP1; break;
    case PH_STOP1: tx_phase = PH_STOP2; break;
    case PH_STOP2:
        if (state.command == TM6605_PLAY || state.command == TM6605_ALERT) { tx_phase = PH_SELECT; }
        else if (state.command == TM6605_SLEEP) { tx_phase = PH_SLEEP; }
        else { complete(now_ms); }
        break;
    case PH_SELECT: tx_phase = PH_GO; break;
    case PH_GO: case PH_SLEEP: complete(now_ms); break;
    default: break;
    }
    return WATCH_OK;
}
#else
static const tm6605_state_t state = {.status = WATCH_NOT_IMPLEMENTED};
watch_status_t tm6605_init(void) { return WATCH_NOT_IMPLEMENTED; }
watch_status_t tm6605_play(uint8_t effect) { (void)effect; return WATCH_NOT_IMPLEMENTED; }
watch_status_t tm6605_alert(uint32_t duration_ms) { (void)duration_ms; return WATCH_NOT_IMPLEMENTED; }
watch_status_t tm6605_stop(void) { return WATCH_NOT_IMPLEMENTED; }
watch_status_t tm6605_sleep(bool enabled) { (void)enabled; return WATCH_NOT_IMPLEMENTED; }
watch_status_t tm6605_process(uint32_t now_ms) { (void)now_ms; return WATCH_NOT_IMPLEMENTED; }
#endif

watch_status_t tm6605_get_state(tm6605_state_t *snapshot)
{
    if (snapshot == NULL) { return WATCH_INVALID_ARGUMENT; }
    *snapshot = state;
    return WATCH_OK;
}
