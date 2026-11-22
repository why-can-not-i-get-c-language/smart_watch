/**
 * UI整合与菜单导航 - STM32F411CEU6
 * 版本：UI-MENU-20261122-01
 * 行为：
 *  - 上电三短闪后自检序列：全白/棋盘/四角标记/关屏/打开
 *  - 默认HOME（时间+会话步数+电池mV），MENU短按或KEY长按进功能菜单（STEPS/HISTORY/TOOLS）
 *  - 功能菜单：KEY短按下一项，SOS短按上一项，MENU短按进入，KEY长按返回HOME
 *  - TOOLS菜单8项滚动显示，KEY/SOS短按导航，MENU短按进入，KEY长按返回功能菜单
 *  - 各详情页KEY长按返回拥有菜单；HOME页MENU长按进入时间编辑，保存/取消返回HOME
 *  - 计步页MENU长按清零，历史页MENU短按追加/SOS短按读取，参数页保留M+/MH/SOS操作
 *  - Flash页MENU长按测试、SOS长按清理，MPU页KEY短按累K，姿态页MENU长按标定
 *  - 振动页切出时停止，BLE页MENU短按发测试；熄屏重连保持导航位置
 *  - 保留LED心跳、I2C/SPI诊断与掉屏恢复，UART只由BLE任务接收
 *  - 电池ADC每5ms至多一次转换，16对后发布，不表示电池百分比或已校准
 */

#include "board_port.h"
#include "board_config.h"
#include "stm32f4xx_hal.h"
#include "i2c_bus.h"
#include "spi_bus.h"
#include "w25q64.h"
#include "uart_port.h"
#include "ble_link.h"
#include "buttons.h"
#include "scheduler.h"
#include "ssd1306.h"
#include "watch_ui.h"
#include "time_service.h"
#include "tm6605.h"
#include "mpu6050.h"
#include "max30102.h"
#include "pedometer.h"
#include "wrist_wake.h"
#include "battery_adc.h"
#include "settings.h"
#include "record_store.h"
#include <stdint.h>
#include <string.h>

#if WATCH_BLE_NAME_SETUP
static const char firmware_version[] = "F06-NAME-20261116-02";
#else
static const char firmware_version[] = "UI-MENU-20261122-01";
#endif

/**
 * 诊断状态结构 - volatile，Keil可观察
 */
typedef struct {
    /* 初始化状态 */
    volatile uint8_t board_init_ok;
    volatile uint8_t i2c_init_ok;
    volatile uint8_t spi_init_ok;
    volatile uint8_t uart_init_ok;
    volatile uint8_t buttons_init_ok;

    /* 时基 */
    volatile uint32_t millis;
    volatile uint32_t heartbeat_count;
    volatile uint32_t scheduler_runs;
    volatile watch_status_t scheduler_status;

    /* I2C @0x3C OLED探测 */
    volatile uint8_t i2c_oled_detected;
    volatile watch_status_t i2c_last_status;

    /* SPI传输状态 */
    volatile uint8_t spi_jedec[3];
    volatile uint8_t spi_transfer_ok;
    volatile watch_status_t spi_last_status;
    volatile w25q64_state_t flash;

    /* 本地UART传输快照；不表示真实BLE连接或手机收到了数据。 */
    volatile ble_link_state_t ble;

    /* 显示状态 */
    volatile watch_status_t display_status;

    /* 按键诊断 */
    volatile uint32_t button_events_last;
    volatile uint32_t button_event_count;
    volatile uint32_t btn_press_p[3];
    volatile uint32_t btn_release_p[3];
    volatile uint32_t btn_short_p[3];
    volatile uint32_t btn_long_p[3];
    volatile uint32_t events_or;
    volatile watch_status_t buttons_status;
    volatile watch_status_t rtc_init_status;
    volatile watch_status_t rtc_get_status;
    volatile watch_status_t rtc_control_status;
    volatile uint32_t rtc_utc;
    volatile time_clock_source_t rtc_clock;
    volatile watch_status_t haptic_request_status, haptic_process_status;
    volatile tm6605_state_t haptic;
    volatile watch_status_t motion_process_status;
    volatile mpu6050_state_t motion;
    volatile watch_status_t mpu_irq_status;
    volatile board_mpu_irq_state_t mpu_irq;
    volatile watch_status_t ppg_process_status;
    volatile max30102_state_t ppg;
    volatile watch_status_t ppg_irq_status;
    volatile board_ppg_irq_state_t ppg_irq;
    volatile watch_status_t pedometer_status;
    volatile pedometer_status_t pedometer;
    volatile watch_status_t wrist_status;
    volatile wrist_wake_status_t wrist;
    volatile watch_status_t battery_adc_status;
    volatile battery_adc_state_t battery_adc;
} diag_state_t;

static volatile diag_state_t diag = {0};
static uint32_t haptic_last_retry_ms;

/* 至多保留一个手动发送请求，UART写入统一在BLE任务中执行。 */
static bool ble_test_pending;

/* LED timing state */
static volatile uint32_t startup_baseline = 0;

/**
 * LED控制：根据启动后经过时间，每1000ms周期内0-200ms亮，200-1000ms灭
 * 心跳计数直接按完整周期数递增
 */
static void led_control_task(void *ctx)
{
    (void)ctx;
    uint32_t elapsed = board_millis() - startup_baseline;
    uint32_t phase = elapsed % 1000;

    /* LED亮灭控制 */
    if (phase < 200) {
        HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);
    } else {
        HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);
    }

    /* 心跳计数按完整周期数更新 */
    diag.heartbeat_count = elapsed / 1000;
}

/**
 * I2C诊断：探测OLED @WATCH_OLED_ADDR7
 */
static void i2c_diag_task(void *ctx)
{
    (void)ctx;
    watch_status_t status = i2c_bus_probe(WATCH_OLED_ADDR7);
    diag.i2c_oled_detected = (status == WATCH_OK) ? 1 : 0;
    diag.i2c_last_status = status;
}

/**
 * SPI诊断：读JEDEC，传输成功标记但不验证芯片身份
 */
static void spi_diag_task(void *ctx)
{
    (void)ctx;
    watch_status_t status = w25q64_probe(board_millis());
    w25q64_state_t flash;
    (void)w25q64_get_state(&flash);
    diag.flash = flash;
    diag.spi_jedec[0] = (uint8_t)(flash.raw_jedec_id >> 16);
    diag.spi_jedec[1] = (uint8_t)(flash.raw_jedec_id >> 8);
    diag.spi_jedec[2] = (uint8_t)flash.raw_jedec_id;
    diag.spi_transfer_ok = (status == WATCH_OK) ? 1U : 0U;
    diag.spi_last_status = status;
}

/* Flash内部编程/擦除完成由5ms调度推进，其余任务继续运行。 */
static void flash_process_task(void *ctx)
{
    (void)ctx;
    (void)w25q64_process(board_millis());
    w25q64_state_t flash;
    (void)w25q64_get_state(&flash);
    diag.flash=flash;
}

/* 参数持久化每5ms推进，与Flash测试互斥。 */
static void settings_process_task(void *ctx)
{
    (void)ctx;
    (void)settings_process(board_millis());
}

/* 历史存储每5ms推进，与Flash测试和参数互斥。 */
static void record_store_process_task(void *ctx)
{
    (void)ctx;
    (void)record_store_process(board_millis());
}

/* 每5ms至多读16字节并写一次；无数据时不自动发包，不重试失败的半包。 */
static void ble_update_task(void *ctx)
{
    (void)ctx;
    uint8_t data[WATCH_BLE_CHUNK_SIZE];
    size_t received = 0U;
    watch_status_t status = ble_link_receive(data, sizeof(data), &received);
    if (status == WATCH_OK && received != 0U) {
        (void)ble_link_send(data, received);
    } else if (status == WATCH_OK && ble_test_pending) {
        static const uint8_t message[] = "WATCH BLE\r\n";
        ble_test_pending = false;
        (void)ble_link_send(message, sizeof(message) - 1U);
    }
    ble_link_state_t state;
    (void)ble_link_get_state(&state);
    diag.ble = state;
}

/**
 * 显示分步发送任务：每5ms最多发送16字节，错误保留至重新初始化。
 */
static void display_process_task(void *ctx)
{
    (void)ctx;
    watch_status_t status = ssd1306_process();
    diag.display_status = status;
}

/**
 * 按键轮询任务：每5ms轮询一次，检测消抖和事件
 */
static void button_poll_task(void *ctx)
{
    (void)ctx;
    uint32_t now = board_millis();
    uint32_t events = 0;
    watch_status_t status = buttons_poll(now, &events);
    diag.buttons_status = status;

    if (status == WATCH_OK && events != 0) {
        diag.button_events_last = events;
        if (diag.button_event_count < 999999U) { diag.button_event_count++; }
        diag.events_or |= events;

        /* 统计各类事件，按999999饱和 */
        if (events & 0xFU) {  /* BOARD */
            if (events & BUTTON_BOARD_PRESS) {
                diag.btn_press_p[0] = (diag.btn_press_p[0] < 999999) ? diag.btn_press_p[0] + 1 : 999999;
            }
            if (events & BUTTON_BOARD_RELEASE) {
                diag.btn_release_p[0] = (diag.btn_release_p[0] < 999999) ? diag.btn_release_p[0] + 1 : 999999;
            }
            if (events & BUTTON_BOARD_SHORT) {
                diag.btn_short_p[0] = (diag.btn_short_p[0] < 999999) ? diag.btn_short_p[0] + 1 : 999999;
            }
            if (events & BUTTON_BOARD_LONG) {
                diag.btn_long_p[0] = (diag.btn_long_p[0] < 999999) ? diag.btn_long_p[0] + 1 : 999999;
            }
        }
        if (events & 0xF0U) {  /* MENU */
            if (events & BUTTON_MENU_PRESS) {
                diag.btn_press_p[1] = (diag.btn_press_p[1] < 999999) ? diag.btn_press_p[1] + 1 : 999999;
            }
            if (events & BUTTON_MENU_RELEASE) {
                diag.btn_release_p[1] = (diag.btn_release_p[1] < 999999) ? diag.btn_release_p[1] + 1 : 999999;
            }
            if (events & BUTTON_MENU_SHORT) {
                diag.btn_short_p[1] = (diag.btn_short_p[1] < 999999) ? diag.btn_short_p[1] + 1 : 999999;
            }
            if (events & BUTTON_MENU_LONG) {
                diag.btn_long_p[1] = (diag.btn_long_p[1] < 999999) ? diag.btn_long_p[1] + 1 : 999999;
            }
        }
        if (events & 0xF00U) {  /* SOS */
            if (events & BUTTON_SOS_PRESS) {
                diag.btn_press_p[2] = (diag.btn_press_p[2] < 999999) ? diag.btn_press_p[2] + 1 : 999999;
            }
            if (events & BUTTON_SOS_RELEASE) {
                diag.btn_release_p[2] = (diag.btn_release_p[2] < 999999) ? diag.btn_release_p[2] + 1 : 999999;
            }
            if (events & BUTTON_SOS_SHORT) {
                diag.btn_short_p[2] = (diag.btn_short_p[2] < 999999) ? diag.btn_short_p[2] + 1 : 999999;
            }
            if (events & BUTTON_SOS_LONG) {
                diag.btn_long_p[2] = (diag.btn_long_p[2] < 999999) ? diag.btn_long_p[2] + 1 : 999999;
            }
        }

        /* 原始诊断已统计；唤醒手势在UI及振动命令分发前统一消费。 */
        events = watch_ui_filter_buttons(events, now);
        if (events == 0U) { return; }
        /* 导航和详情功能由UI负责；本分发点独占振动命令和BLE发送。 */
        bool was_haptic = watch_ui_haptic_active();
        bool was_ble = watch_ui_ble_active();
        watch_ui_notify_button(events, now);
        /* BLE发送：仅当进入前已在BLE页、MENU短按且不组合长按、事件后仍在BLE页时才发送。 */
        if (was_ble && watch_ui_ble_active() && (events & BUTTON_MENU_SHORT) != 0U &&
            (events & (BUTTON_BOARD_LONG | BUTTON_MENU_LONG | BUTTON_SOS_LONG)) == 0U) {
            ble_test_pending = true;
        }
        /* 振动命令：仅当进入前已在HAPTIC页、事件后仍在HAPTIC页且不是KEY长按返回时才执行命令。 */
        if (was_haptic && !watch_ui_haptic_active()) {
            diag.haptic_request_status = tm6605_stop();
        } else if (was_haptic && watch_ui_haptic_active() && (events & BUTTON_BOARD_LONG) == 0U) {
            if ((events & BUTTON_BOARD_SHORT) != 0U) { diag.haptic_request_status = tm6605_stop(); }
            else if ((events & BUTTON_SOS_LONG) != 0U) { diag.haptic_request_status = tm6605_sleep(false); }
            else if ((events & BUTTON_SOS_SHORT) != 0U) { diag.haptic_request_status = tm6605_sleep(true); }
            else if ((events & BUTTON_MENU_LONG) != 0U) { diag.haptic_request_status = tm6605_alert(10000U); }
            else if ((events & BUTTON_MENU_SHORT) != 0U) { diag.haptic_request_status = tm6605_play(14U); }
        } else if (!watch_ui_haptic_active()) {
            diag.rtc_control_status = watch_ui_get_action_status();
        }
    }
}

/**
 * 每500ms读取RTC诊断，不修改RTC及其有效性。
 */
static void rtc_update_task(void *ctx)
{
    (void)ctx;
    uint32_t utc = 0U;
    diag.rtc_get_status = time_service_get(&utc);
    diag.rtc_utc = utc;
    diag.rtc_clock = time_service_clock_source();
}

/* UI每10ms推进，整帧仍通过独立的分步发送任务处理。 */
static void ui_update_task(void *ctx)
{
    (void)ctx;
    uint32_t now = board_millis();
    watch_status_t status = watch_ui_update(now);
    diag.display_status = status;
}


/**
 * 振动命令推进与离线初始化重试
 */
/* 每次最多一个驱动I2C操作；断线仅按每秒节流重试初始化，不重播旧效果。 */
static void haptic_update_task(void *ctx)
{
    (void)ctx;
    uint32_t now = board_millis();
    diag.haptic_process_status = tm6605_process(now);
    tm6605_state_t current;
    tm6605_get_state(&current);
    if (!current.ready && !current.busy &&
        (uint32_t)(now - haptic_last_retry_ms) >= WATCH_HAPTIC_RETRY_MS) {
        diag.haptic_request_status = tm6605_init();
        haptic_last_retry_ms = now;
        tm6605_get_state(&current);
    }
    diag.haptic = current;
}

/* 身份、配置和采样独立于页面推进，每轮至多一次I2C事务。 */
static void motion_update_task(void *ctx)
{
    (void)ctx;
    diag.wrist_status = wrist_wake_process(board_millis());
    diag.motion_process_status = mpu6050_process(board_millis());
    mpu6050_state_t state;
    mpu6050_get_state(&state);
    diag.motion = state;
    board_mpu_irq_state_t irq;
    diag.mpu_irq_status = board_mpu_irq_get_state(&irq);
    diag.mpu_irq = irq;
    motion_sample_t sample;
    uint32_t steps;
    /* read只领取新样本；其他5ms轮询不会重复计步。无效时保数并重置检测。 */
    watch_status_t read_status = mpu6050_read(&sample);
    if (read_status == WATCH_OK || !state.sample.valid) {
        diag.pedometer_status = pedometer_update(&sample, &steps);
        bool wake = false;
        diag.wrist_status = wrist_wake_update(&sample, &wake);
        if (wake) { watch_ui_wake(board_millis()); }
    }
    pedometer_status_t counter;
    (void)pedometer_get_state(&counter);
    diag.pedometer = counter;
    wrist_wake_status_t wrist;
    (void)wrist_wake_get_state(&wrist);
    diag.wrist = wrist;
}

/* 原始PPG独立于页面采集；每次最多领取4个样本，不运行心率或血氧算法。 */
static void ppg_update_task(void *ctx)
{
    (void)ctx;
    diag.ppg_process_status = max30102_process(board_millis());
    ppg_sample_t sample;
    for (unsigned i = 0U; i < 4U; ++i) {
        if (max30102_read(&sample) != WATCH_OK) { break; }
    }
    max30102_state_t state;
    max30102_get_state(&state);
    diag.ppg = state;
    board_ppg_irq_state_t irq;
    diag.ppg_irq_status = board_ppg_irq_get_state(&irq);
    diag.ppg_irq = irq;
}

/* 电池ADC每5ms推进，每轮至多一次ADC转换。 */
static void battery_adc_task(void *ctx)
{
    (void)ctx;
    diag.battery_adc_status = battery_adc_process(board_millis());
    battery_adc_state_t bat_state;
    (void)battery_adc_get_state(&bat_state);
    diag.battery_adc = bat_state;
}

/* 常规时基及串口诊断，与振动命令状态分别记录。 */
static void update_status(void *ctx)
{
    (void)ctx;
    diag.millis = board_millis();
    diag.scheduler_runs++;
}

/**
 * 软延迟（故障闪烁用）
 */
static void volatile_delay(uint32_t iterations)
{
    volatile uint32_t i = iterations;
    while (i--) {
        __NOP();
    }
}

/**
 * 毫秒延迟
 */
static void delay_ms(uint32_t ms)
{
    uint32_t start = board_millis();
    while ((board_millis() - start) < ms);
}

/**
 * 启动三短闪
 */
static void startup_sequence(void)
{
    for (int i = 0; i < 3; i++) {
        HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);
        delay_ms(100);
        HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);
        delay_ms(100);
    }
    delay_ms(500);
}

/**
 * 故障闪烁（初始化失败）
 */
static void fault_blink(void)
{
    for (;;) {
        HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);
        volatile_delay(300000);
        HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);
        volatile_delay(300000);
    }
}

int main(void)
{
    /* 防编译器删除版本字符串 */
    volatile const char *p = firmware_version;
    while (*p != '\0') p++;

    /* 基础初始化 */
    if (board_init() != WATCH_OK) {
        fault_blink();
    }
    diag.board_init_ok = 1;

    /* 初始化总线 */
    diag.i2c_init_ok = (board_i2c_init() == WATCH_OK) ? 1 : 0;
    diag.spi_init_ok = (board_spi_init() == WATCH_OK) ? 1 : 0;
    diag.uart_init_ok = (board_uart_init(WATCH_UART1_BAUDRATE) == WATCH_OK) ? 1 : 0;

    if (!diag.uart_init_ok) {
        fault_blink();
    }
    (void)ble_link_init();

    /* 初始化按键 */
    watch_status_t buttons_status = buttons_init();
    diag.buttons_init_ok = (buttons_status == WATCH_OK) ? 1 : 0;
    if (buttons_status != WATCH_OK) {
        fault_blink();
    }

#if WATCH_BLE_NAME_SETUP
    /* 专用配置路径不启动普通透传任务；每次开机仅确认一次，失败不自动重发。 */
    bool name_attempted = false;
    watch_status_t name_result = WATCH_NOT_READY;
    for (;;) {
        uint32_t now = board_millis();
        uint32_t events = 0U;
        (void)ssd1306_process();
        (void)watch_ui_ble_name_setup(name_attempted, name_result, now);
        if (buttons_poll(now, &events) == WATCH_OK && !name_attempted &&
            (events & BUTTON_BOARD_SHORT) != 0U) {
            name_attempted = true;
            /* 先完整显示进度，再进行有总时限的配置交互。 */
            uint32_t showing = board_millis();
            while ((uint32_t)(board_millis() - showing) < 500U) {
                (void)ssd1306_process();
                (void)watch_ui_ble_name_setup(true, WATCH_NOT_READY, board_millis());
                delay_ms(1U);
            }
            name_result = ble_link_setup_name();
            /* UI的NOT_READY用于进行中；实际配置未就绪须显示失败，不能一直等待。 */
            if (name_result == WATCH_NOT_READY) { name_result = WATCH_IO_ERROR; }
        }
        HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13,
                         now % 1000U < 200U ? GPIO_PIN_RESET : GPIO_PIN_SET);
        delay_ms(1U);
    }
#endif

    /* RTC失败仍继续运行LED及显示，由RTC页面和diag明确反馈。 */
    diag.rtc_init_status = time_service_init();
    diag.rtc_control_status = WATCH_NOT_READY;
    rtc_update_task(NULL);

    /* 启动三短闪 */
    startup_sequence();

    /* 记录启动基准时刻 */
    startup_baseline = board_millis();
    diag.haptic_request_status = tm6605_init();
    haptic_last_retry_ms = board_millis();
    diag.mpu_irq_status = board_mpu_irq_init();
    (void)mpu6050_init();
    diag.ppg_irq_status = board_ppg_irq_init();
    (void)max30102_init();
    (void)pedometer_reset();
    (void)wrist_wake_reset();
    (void)battery_adc_init();
    (void)settings_init();
    (void)record_store_init();

    /* 建立任务数组，所有last_ms初始化到startup_baseline */
    scheduler_task_t tasks[] = {
        {.period_ms = 5, .last_ms = startup_baseline, .callback = flash_process_task, .context = NULL},
        {.period_ms = 5, .last_ms = startup_baseline, .callback = settings_process_task, .context = NULL},
        {.period_ms = 5, .last_ms = startup_baseline, .callback = record_store_process_task, .context = NULL},
        {.period_ms = 5, .last_ms = startup_baseline, .callback = battery_adc_task, .context = NULL},
        {.period_ms = 5, .last_ms = startup_baseline, .callback = ppg_update_task, .context = NULL},
        {.period_ms = 5, .last_ms = startup_baseline, .callback = haptic_update_task, .context = NULL},
        {.period_ms = 5, .last_ms = startup_baseline, .callback = motion_update_task, .context = NULL},
        /* LED控制：每10ms更新一次 */
        {.period_ms = 10, .last_ms = startup_baseline, .callback = led_control_task, .context = NULL},

        /* I2C诊断：每1000ms探测一次 */
        {.period_ms = 1000, .last_ms = startup_baseline, .callback = i2c_diag_task, .context = NULL},

        /* SPI诊断：每1000ms读JEDEC一次 */
        {.period_ms = 1000, .last_ms = startup_baseline, .callback = spi_diag_task, .context = NULL},

        /* UART唯一接收者：原样回显及单次手动测试发送。 */
        {.period_ms = 5, .last_ms = startup_baseline, .callback = ble_update_task, .context = NULL},

        /* 按键轮询：每5ms调用一次 */
        {.period_ms = 5, .last_ms = startup_baseline, .callback = button_poll_task, .context = NULL},

        /* RTC诊断：每500ms读取真实日历 */
        {.period_ms = 500, .last_ms = startup_baseline, .callback = rtc_update_task, .context = NULL},

        /* 显示分步发送：每5ms驱动 */
        {.period_ms = 5, .last_ms = startup_baseline, .callback = display_process_task, .context = NULL},

        /* UI状态机每10ms推进 */
        {.period_ms = 10, .last_ms = startup_baseline, .callback = ui_update_task, .context = NULL},

        /* 状态更新：每10ms更新一次 */
        {.period_ms = 10, .last_ms = startup_baseline, .callback = update_status, .context = NULL},
    };
    const size_t task_count = sizeof(tasks) / sizeof(tasks[0]);

    /* 初始化诊断 */
    diag.spi_transfer_ok = 0;
    diag.millis = board_millis();
    diag.button_event_count = 0;
    diag.events_or = 0;

    /* 主循环 */
    for (;;) {
        uint32_t now = board_millis();
        watch_status_t sched_status = scheduler_run(tasks, task_count, now);
        diag.scheduler_status = sched_status;
    }

    return 0;
}
