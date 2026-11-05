/**
 * P01 首启检验固件 - STM32F411CEU6 + PC13 LED
 * 行为：上电三短闪（100ms亮100ms灭）后，每秒一次心跳（200ms亮800ms灭）
 * 版本标识：由代码引用保证不被优化删除
 */

#include "board_port.h"
#include "stm32f4xx_hal.h"
#include <stdint.h>
#include <stddef.h>

/* 版本字符串 - 全局const，被代码引用以防被编译器删除 */
static const char p01_version_string[] = "P01-20261105-01";

/**
 * 不依赖时基的软件延迟
 * 用于故障闪烁时LED控制
 * @param iterations __NOP循环次数，粗略对应数十至数百毫秒
 */
static void volatile_delay(uint32_t iterations)
{
    volatile uint32_t i = iterations;
    while (i--) {
        __NOP();
    }
}

/**
 * 毫秒级延迟（使用HAL时基）
 * 仅在board_init成功且SysTick已就绪时调用
 */
static void delay_ms(uint32_t ms)
{
    uint32_t start = board_millis();
    while ((board_millis() - start) < ms);
}

/**
 * 启动序列：三短闪（100ms亮，100ms灭，重复3次）
 */
static void startup_sequence(void)
{
    for (int i = 0; i < 3; i++) {
        HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);  /* LED亮 */
        delay_ms(100);
        HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);    /* LED灭 */
        delay_ms(100);
    }
    delay_ms(500);  /* 长暂停后进入心跳 */
}

/**
 * 故障指示：不依赖时基的连续闪烁
 * 用于board_init失败时显示错误
 * 注意：此循环无return，不依赖中断/HAL时基
 */
static void fault_blink(void)
{
    for (;;) {
        HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);  /* LED亮 */
        volatile_delay(300000);   /* 软件延迟，数十至数百ms */
        HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);    /* LED灭 */
        volatile_delay(300000);   /* 软件延迟，数十至数百ms */
    }
}

int main(void)
{
    /* 实际读取版本字符串以防被链接器删除 */
    volatile const char *p = p01_version_string;
    while (*p != '\0') {
        p++;
    }

    /* 初始化HAL和板级资源 */
    if (board_init() != WATCH_OK) {
        /* 初始化失败：进入故障闪烁 */
        fault_blink();
        /* fault_blink不返回 */
    }

    /* 启动序列：三次100ms闪烁 */
    startup_sequence();

    /* 主循环：每秒心跳（200ms亮，800ms灭） */
    for (;;) {
        HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);  /* LED亮 */
        delay_ms(200);
        HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);    /* LED灭 */
        delay_ms(800);
    }

    return 0;  /* 不可达 */
}
