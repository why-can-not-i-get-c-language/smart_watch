#include "buttons.h"
#include "board_config.h"
#include <string.h>

#ifdef WATCH_TARGET_STM32
#include "stm32f4xx_hal.h"

/* 按键状态机 */
typedef enum {
    BTN_STATE_WAIT_RELEASE = 0,
    BTN_STATE_ARMING,
    BTN_STATE_RELEASED,
    BTN_STATE_DEBOUNCE_DOWN,
    BTN_STATE_PRESSED,
    BTN_STATE_DEBOUNCE_UP,
} button_state_t;

typedef struct {
    button_state_t state;
    uint32_t state_enter_ms;
    uint32_t pressed_since_ms;
    uint8_t long_fired;
} button_context_t;

static button_context_t btn_board = {0};
static button_context_t btn_menu = {0};
static button_context_t btn_sos = {0};

static uint8_t buttons_init_ok = 0;

watch_status_t buttons_init(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    GPIO_InitTypeDef gpio_init = {0};

    /* PA0 - 板载键 */
    gpio_init.Pin = WATCH_BUTTON_BOARD_PIN;
    gpio_init.Mode = GPIO_MODE_INPUT;
    gpio_init.Pull = GPIO_PULLUP;
    gpio_init.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(WATCH_BUTTON_BOARD_PORT, &gpio_init);

    /* PB12 - 菜单键 */
    gpio_init.Pin = WATCH_BUTTON_MENU_PIN;
    HAL_GPIO_Init(WATCH_BUTTON_MENU_PORT, &gpio_init);

    /* PB13 - SOS键 */
    gpio_init.Pin = WATCH_BUTTON_SOS_PIN;
    HAL_GPIO_Init(WATCH_BUTTON_SOS_PORT, &gpio_init);

    memset(&btn_board, 0, sizeof(btn_board));
    memset(&btn_menu, 0, sizeof(btn_menu));
    memset(&btn_sos, 0, sizeof(btn_sos));

    buttons_init_ok = 1;
    return WATCH_OK;
}

static uint8_t button_read(GPIO_TypeDef *port, uint16_t pin, uint32_t active_level)
{
    /* 读取按键状态：active_level 表示按下时的电平值 */
    return (HAL_GPIO_ReadPin(port, pin) == active_level) ? 1 : 0;
}

static uint32_t button_process(button_context_t *btn, uint8_t pressed, uint32_t now_ms,
                                uint32_t *events, uint32_t event_press, uint32_t event_release,
                                uint32_t event_short, uint32_t event_long)
{
    uint32_t output = 0;

    switch (btn->state) {
    case BTN_STATE_WAIT_RELEASE:
        /* 开机按住不产生事件，必须先确认稳定松开。 */
        if (!pressed) {
            btn->state = BTN_STATE_ARMING;
            btn->state_enter_ms = now_ms;
        }
        break;

    case BTN_STATE_ARMING:
        if (pressed) {
            btn->state = BTN_STATE_WAIT_RELEASE;
        } else if ((uint32_t)(now_ms - btn->state_enter_ms) >= WATCH_BUTTON_DEBOUNCE_MS) {
            btn->state = BTN_STATE_RELEASED;
        }
        break;

    case BTN_STATE_RELEASED:
        if (pressed) {
            btn->state = BTN_STATE_DEBOUNCE_DOWN;
            btn->state_enter_ms = now_ms;
            btn->long_fired = 0;
        }
        break;

    case BTN_STATE_DEBOUNCE_DOWN:
        if (!pressed) {
            /* 抖动，回到释放状态 */
            btn->state = BTN_STATE_RELEASED;
        } else if ((uint32_t)(now_ms - btn->state_enter_ms) >= WATCH_BUTTON_DEBOUNCE_MS) {
            /* 消抖完成，进入按下状态 */
            btn->state = BTN_STATE_PRESSED;
            btn->state_enter_ms = now_ms;
            btn->pressed_since_ms = now_ms;
            btn->long_fired = 0;
            *events |= event_press;
            output = 1;
        }
        break;

    case BTN_STATE_PRESSED:
        if (!pressed) {
            /* 松开，进入消抖上升沿 */
            btn->state = BTN_STATE_DEBOUNCE_UP;
            btn->state_enter_ms = now_ms;
        } else if (!btn->long_fired && (uint32_t)(now_ms - btn->pressed_since_ms) >= WATCH_BUTTON_LONG_PRESS_MS) {
            /* 长按触发 */
            btn->long_fired = 1;
            *events |= event_long;
            output = 1;
        }
        break;

    case BTN_STATE_DEBOUNCE_UP:
        if (pressed) {
            /* 抖动，回到按下状态，但不重置长按起点 */
            btn->state = BTN_STATE_PRESSED;
            btn->state_enter_ms = now_ms;
        } else if ((uint32_t)(now_ms - btn->state_enter_ms) >= WATCH_BUTTON_DEBOUNCE_MS) {
            /* 上升消抖完成，进入释放状态 */
            btn->state = BTN_STATE_RELEASED;
            *events |= event_release;

            /* 首次松开时刻决定持续时间，不把释放消抖时间算入长按。 */
            if (!btn->long_fired) {
                if ((uint32_t)(btn->state_enter_ms - btn->pressed_since_ms) >= WATCH_BUTTON_LONG_PRESS_MS) {
                    *events |= event_long;
                    btn->long_fired = 1U;
                } else {
                    *events |= event_short;
                }
            }
            output = 1;
        }
        break;

    default:
        btn->state = BTN_STATE_WAIT_RELEASE;
        break;
    }

    return output;
}

watch_status_t buttons_poll(uint32_t now_ms, uint32_t *events)
{
    if (events == NULL) {
        return WATCH_INVALID_ARGUMENT;
    }

    *events = 0U;
    if (!buttons_init_ok) {
        return WATCH_NOT_READY;
    }

    /* 板载键 */
    uint8_t board_pressed = button_read(WATCH_BUTTON_BOARD_PORT, WATCH_BUTTON_BOARD_PIN,
                                        WATCH_BUTTON_BOARD_ACTIVE_LEVEL);
    button_process(&btn_board, board_pressed, now_ms, events,
                   BUTTON_BOARD_PRESS, BUTTON_BOARD_RELEASE, BUTTON_BOARD_SHORT, BUTTON_BOARD_LONG);

    /* 菜单键 */
    uint8_t menu_pressed = button_read(WATCH_BUTTON_MENU_PORT, WATCH_BUTTON_MENU_PIN,
                                       WATCH_BUTTON_MENU_ACTIVE_LEVEL);
    button_process(&btn_menu, menu_pressed, now_ms, events,
                   BUTTON_MENU_PRESS, BUTTON_MENU_RELEASE, BUTTON_MENU_SHORT, BUTTON_MENU_LONG);

    /* SOS键 */
    uint8_t sos_pressed = button_read(WATCH_BUTTON_SOS_PORT, WATCH_BUTTON_SOS_PIN,
                                      WATCH_BUTTON_SOS_ACTIVE_LEVEL);
    button_process(&btn_sos, sos_pressed, now_ms, events,
                   BUTTON_SOS_PRESS, BUTTON_SOS_RELEASE, BUTTON_SOS_SHORT, BUTTON_SOS_LONG);

    return WATCH_OK;
}

#else  /* !WATCH_TARGET_STM32 */

watch_status_t buttons_init(void)
{
    return WATCH_NOT_IMPLEMENTED;
}

watch_status_t buttons_poll(uint32_t now_ms, uint32_t *events)
{
    if (events == NULL) {
        return WATCH_INVALID_ARGUMENT;
    }
    *events = 0U;
    (void)now_ms;
    return WATCH_NOT_IMPLEMENTED;
}

#endif  /* WATCH_TARGET_STM32 */
