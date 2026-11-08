/**
 * F03-P02 计步累计与清零验证 - STM32F411CEU6
 * 版本：F03-P02-20261108-02
 * 行为：
 *  - 上电三短闪后自检序列：全白/棋盘/四角标记/关屏/打开
 *  - 默认计步页，板载KEY长按循环计步、MPU、振动、北京时间页
 *  - 仅本次开机RAM累计，计步页MENU长按清零；日汇总留后续任务
 *  - 六轴持续采样，掉线立即无效，节流重新配置后恢复
 *  - 振动命令异步推进，切出验收页时请求停止
 *  - 时间页保留本地编辑/保存/取消，RTC继续走时
 *  - 保留LED心跳、I2C/SPI/UART诊断与掉屏恢复
 */

#include "board_port.h"
#include "board_config.h"
#include "stm32f4xx_hal.h"
#include "i2c_bus.h"
#include "spi_bus.h"
#include "uart_port.h"
#include "buttons.h"
#include "scheduler.h"
#include "ssd1306.h"
#include "watch_ui.h"
#include "time_service.h"
#include "tm6605.h"
#include "mpu6050.h"
#include "pedometer.h"
#include <stdint.h>
#include <string.h>

static const char firmware_version[] = "F03-P02-20261108-02";

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

    /* UART诊断与回环匹配 */
    volatile uint32_t uart_tx_count;
    volatile uint32_t uart_rx_count;
    volatile uint32_t uart_rx_error_count;
    volatile uint8_t uart_loopback_ok;
    volatile uint32_t uart_loopback_passes;
    volatile uint32_t uart_timeouts;
    volatile uint8_t uart_last_rx_byte;
    volatile watch_status_t uart_last_status;

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
    volatile watch_status_t pedometer_status;
    volatile pedometer_status_t pedometer;
} diag_state_t;

static volatile diag_state_t diag = {0};
static uint32_t haptic_last_retry_ms;

/* UART loopback state machine */
static volatile uint8_t uart_expected_frame[4];
static volatile uint32_t uart_frame_send_time = 0;
static volatile uint8_t uart_frame_index = 0;
static volatile uint8_t uart_send_pending = 0;

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
    uint8_t cmd[4] = {0x9F, 0xFF, 0xFF, 0xFF};
    uint8_t result[4] = {0};

    watch_status_t status = spi_bus_transfer(cmd, result, 4);
    if (status == WATCH_OK) {
        diag.spi_jedec[0] = result[1];
        diag.spi_jedec[1] = result[2];
        diag.spi_jedec[2] = result[3];
        diag.spi_transfer_ok = 1;
    } else {
        diag.spi_transfer_ok = 0;
    }
    diag.spi_last_status = status;
}

/**
 * UART回环匹配任务：
 * - 每500ms发送含递增序号的4字节帧（AA seq ~seq 55），发送前清ok标志
 * - 异步每5ms尝试接收：可每次取1字节，或一次取多个，最多100ms内匹配
 * - 错字节立刻记WATCH_INVALID_DATA/清ok/取消等待
 * - rx_status非WATCH_OK时立刻记错误/清ok/取消等待
 * - 100ms超时后清ok/记WATCH_TIMEOUT/timeouts++
 * - 完整4字节匹配保留计数，接回跳线后恢复
 */
static void uart_loopback_task(void *ctx)
{
    (void)ctx;
    static uint32_t last_send_time = 0;
    static uint8_t send_sequence = 0;

    uint32_t now = board_millis();

    /* 每500ms尝试发送一帧 */
    if (!uart_send_pending && (now - last_send_time) >= 500) {
        /* 清空旧接收缓冲 */
        uint8_t dummy_buf[256];
        size_t dummy_size = 0;
        uart_port_receive(dummy_buf, sizeof(dummy_buf), &dummy_size);

        /* 构建帧：AA seq ~seq 55 */
        uart_expected_frame[0] = 0xAA;
        uart_expected_frame[1] = send_sequence;
        uart_expected_frame[2] = (uint8_t)~send_sequence;
        uart_expected_frame[3] = 0x55;

        /* 发送前清ok标志 */
        diag.uart_loopback_ok = 0;

        /* 发送帧 */
        watch_status_t status = uart_port_send((const uint8_t *)uart_expected_frame, 4);
        if (status == WATCH_OK) {
            diag.uart_tx_count++;
            uart_send_pending = 1;
            uart_frame_send_time = now;
            uart_frame_index = 0;
            send_sequence++;
        } else {
            /* 发送失败也清ok，不残留旧成功 */
            diag.uart_loopback_ok = 0;
        }
        diag.uart_last_status = status;
        last_send_time = now;
    }

    /* 待发送中：每5ms接收并匹配 */
    if (uart_send_pending) {
        uint32_t elapsed_since_send = now - uart_frame_send_time;

        /* 100ms超时 */
        if (elapsed_since_send >= 100) {
            uart_send_pending = 0;
            diag.uart_loopback_ok = 0;
            diag.uart_timeouts++;
            diag.uart_last_status = WATCH_TIMEOUT;
            return;
        }

        /* 接收字节：可逐字节或批量接收 */
        uint8_t rx_buf[16];
        size_t received = 0;
        watch_status_t rx_status = uart_port_receive(rx_buf, sizeof(rx_buf), &received);

        if (rx_status != WATCH_OK) {
            /* 接收失败立刻记错误 */
            uart_send_pending = 0;
            diag.uart_loopback_ok = 0;
            diag.uart_last_status = rx_status;
            return;
        }

        if (received > 0) {
            /* 逐字节匹配 */
            for (size_t i = 0; i < received && uart_send_pending; i++) {
                uint8_t rx_byte = rx_buf[i];
                diag.uart_last_rx_byte = rx_byte;
                diag.uart_rx_count++;

                /* 匹配当前期望字节 */
                if (uart_frame_index < 4 && rx_byte == uart_expected_frame[uart_frame_index]) {
                    uart_frame_index++;

                    /* 全部4字节匹配 */
                    if (uart_frame_index == 4) {
                        uart_send_pending = 0;
                        diag.uart_loopback_ok = 1;
                        diag.uart_loopback_passes++;
                    }
                } else if (uart_frame_index < 4) {
                    /* 字节不匹配，记错误并重置等待 */
                    uart_send_pending = 0;
                    diag.uart_loopback_ok = 0;
                    diag.uart_last_status = WATCH_INVALID_DATA;
                }
            }
        }
    }
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

        /* 页面切换和时间编辑由UI负责；本分发点独占振动命令。 */
        bool was_haptic = watch_ui_haptic_active();
        watch_ui_notify_button(events, now);
        if (was_haptic && !watch_ui_haptic_active()) {
            diag.haptic_request_status = tm6605_stop();
        } else if (watch_ui_haptic_active() && (events & BUTTON_BOARD_LONG) == 0U) {
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
    diag.motion_process_status = mpu6050_process(board_millis());
    mpu6050_state_t state;
    mpu6050_get_state(&state);
    diag.motion = state;
    motion_sample_t sample;
    uint32_t steps;
    /* read只领取新样本；其他5ms轮询不会重复计步。无效时保数并重置检测。 */
    watch_status_t read_status = mpu6050_read(&sample);
    if (read_status == WATCH_OK || !state.sample.valid) {
        diag.pedometer_status = pedometer_update(&sample, &steps);
    }
    pedometer_status_t counter;
    (void)pedometer_get_state(&counter);
    diag.pedometer = counter;
}

/* 常规时基及串口诊断，与振动命令状态分别记录。 */
static void update_status(void *ctx)
{
    (void)ctx;
    diag.millis = board_millis();
    diag.scheduler_runs++;
    diag.uart_rx_error_count = uart_port_get_error_count();
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

    /* 初始化按键 */
    watch_status_t buttons_status = buttons_init();
    diag.buttons_init_ok = (buttons_status == WATCH_OK) ? 1 : 0;
    if (buttons_status != WATCH_OK) {
        fault_blink();
    }

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
    (void)mpu6050_init();
    (void)pedometer_reset();

    /* 建立任务数组，所有last_ms初始化到startup_baseline */
    scheduler_task_t tasks[] = {
        {.period_ms = 5, .last_ms = startup_baseline, .callback = haptic_update_task, .context = NULL},
        {.period_ms = 5, .last_ms = startup_baseline, .callback = motion_update_task, .context = NULL},
        /* LED控制：每10ms更新一次 */
        {.period_ms = 10, .last_ms = startup_baseline, .callback = led_control_task, .context = NULL},

        /* I2C诊断：每1000ms探测一次 */
        {.period_ms = 1000, .last_ms = startup_baseline, .callback = i2c_diag_task, .context = NULL},

        /* SPI诊断：每1000ms读JEDEC一次 */
        {.period_ms = 1000, .last_ms = startup_baseline, .callback = spi_diag_task, .context = NULL},

        /* UART回环：每5ms调用一次（内部500ms发送节流）*/
        {.period_ms = 5, .last_ms = startup_baseline, .callback = uart_loopback_task, .context = NULL},

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
    diag.uart_loopback_ok = 0;
    diag.uart_loopback_passes = 0;
    diag.uart_timeouts = 0;
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
