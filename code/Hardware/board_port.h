#ifndef WATCH_BOARD_PORT_H
#define WATCH_BOARD_PORT_H
#include "watch_types.h"
/* HAL端口、时基与STOP恢复。未实现接口返回WATCH_NOT_IMPLEMENTED，输出无效。 */
watch_status_t board_init(void);
uint32_t board_millis(void);
watch_status_t board_enter_stop(void);

/* I2C/SPI/UART初始化接口 */
watch_status_t board_i2c_init(void);
watch_status_t board_spi_init(void);
watch_status_t board_uart_init(uint32_t baudrate);

/* MAX30102低有效开漏INT：PB1下降沿计数，不在中断中访问I2C。
 * count是边沿数，不能当成PPG样本数或STOP唤醒次数。 */
typedef struct {
    bool initialized, pin_high;
    uint32_t count, last_edge_ms;
} board_irq_state_t;
typedef board_irq_state_t board_ppg_irq_state_t;
typedef board_irq_state_t board_mpu_irq_state_t;
watch_status_t board_ppg_irq_init(void);
watch_status_t board_ppg_irq_get_state(board_ppg_irq_state_t *state);
/* 仅由EXTI1_IRQHandler转发。 */
void board_ppg_irq_handle(void);
/* MPU数据就绪高有效推挽输出：PB0/EXTI0上升沿，PA0仍轮询。 */
watch_status_t board_mpu_irq_init(void);
watch_status_t board_mpu_irq_get_state(board_mpu_irq_state_t *state);
void board_mpu_irq_handle(void);
#endif
