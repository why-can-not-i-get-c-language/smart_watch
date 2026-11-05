# 本文件与project/config/source_manifest.json同步维护。

# 手表完整源码框架；功能骨架已加入编译，是否运行由应用调用决定。
set(WATCH_STARTUP_SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/Start/startup_stm32f411xe.s"
    "${CMAKE_CURRENT_LIST_DIR}/Start/system_stm32f4xx.c"
    "${CMAKE_CURRENT_LIST_DIR}/Start/stm32f4xx_it.c"
)

set(WATCH_HAL_SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/Library/stm32f4xx_hal.c"
    "${CMAKE_CURRENT_LIST_DIR}/Library/stm32f4xx_hal_rcc.c"
    "${CMAKE_CURRENT_LIST_DIR}/Library/stm32f4xx_hal_rcc_ex.c"
    "${CMAKE_CURRENT_LIST_DIR}/Library/stm32f4xx_hal_cortex.c"
    "${CMAKE_CURRENT_LIST_DIR}/Library/stm32f4xx_hal_gpio.c"
    "${CMAKE_CURRENT_LIST_DIR}/Library/stm32f4xx_hal_flash.c"
    "${CMAKE_CURRENT_LIST_DIR}/Library/stm32f4xx_hal_flash_ex.c"
    "${CMAKE_CURRENT_LIST_DIR}/Library/stm32f4xx_hal_pwr.c"
    "${CMAKE_CURRENT_LIST_DIR}/Library/stm32f4xx_hal_pwr_ex.c"
    "${CMAKE_CURRENT_LIST_DIR}/Library/stm32f4xx_hal_exti.c"
    "${CMAKE_CURRENT_LIST_DIR}/Library/stm32f4xx_hal_dma.c"
    "${CMAKE_CURRENT_LIST_DIR}/Library/stm32f4xx_hal_dma_ex.c"
    "${CMAKE_CURRENT_LIST_DIR}/Library/stm32f4xx_hal_adc.c"
    "${CMAKE_CURRENT_LIST_DIR}/Library/stm32f4xx_hal_adc_ex.c"
    "${CMAKE_CURRENT_LIST_DIR}/Library/stm32f4xx_hal_i2c.c"
    "${CMAKE_CURRENT_LIST_DIR}/Library/stm32f4xx_hal_i2c_ex.c"
    "${CMAKE_CURRENT_LIST_DIR}/Library/stm32f4xx_hal_spi.c"
    "${CMAKE_CURRENT_LIST_DIR}/Library/stm32f4xx_hal_uart.c"
    "${CMAKE_CURRENT_LIST_DIR}/Library/stm32f4xx_hal_rtc.c"
    "${CMAKE_CURRENT_LIST_DIR}/Library/stm32f4xx_hal_rtc_ex.c"
    "${CMAKE_CURRENT_LIST_DIR}/Library/stm32f4xx_hal_tim.c"
    "${CMAKE_CURRENT_LIST_DIR}/Library/stm32f4xx_hal_tim_ex.c"
)

# 主机公共模块检查所用源码；不等于当前目标固件的集成范围。
set(WATCH_CORE_SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/Hardware/board_port.c"
    "${CMAKE_CURRENT_LIST_DIR}/Hardware/i2c_bus.c"
    "${CMAKE_CURRENT_LIST_DIR}/Hardware/spi_bus.c"
    "${CMAKE_CURRENT_LIST_DIR}/Hardware/uart_port.c"
    "${CMAKE_CURRENT_LIST_DIR}/Hardware/ssd1306.c"
    "${CMAKE_CURRENT_LIST_DIR}/Hardware/buttons.c"
    "${CMAKE_CURRENT_LIST_DIR}/Hardware/tm6605.c"
    "${CMAKE_CURRENT_LIST_DIR}/Hardware/mpu6050.c"
    "${CMAKE_CURRENT_LIST_DIR}/Hardware/max30102.c"
    "${CMAKE_CURRENT_LIST_DIR}/Hardware/ble_link.c"
    "${CMAKE_CURRENT_LIST_DIR}/Hardware/w25q64.c"
    "${CMAKE_CURRENT_LIST_DIR}/Hardware/battery_adc.c"
    "${CMAKE_CURRENT_LIST_DIR}/System/time_service.c"
    "${CMAKE_CURRENT_LIST_DIR}/System/settings.c"
    "${CMAKE_CURRENT_LIST_DIR}/System/record_store.c"
    "${CMAKE_CURRENT_LIST_DIR}/System/ble_protocol.c"
    "${CMAKE_CURRENT_LIST_DIR}/Application/pedometer.c"
    "${CMAKE_CURRENT_LIST_DIR}/Application/wrist_wake.c"
    "${CMAKE_CURRENT_LIST_DIR}/Application/activity.c"
    "${CMAKE_CURRENT_LIST_DIR}/Application/reminder.c"
    "${CMAKE_CURRENT_LIST_DIR}/Application/battery_service.c"
    "${CMAKE_CURRENT_LIST_DIR}/Application/health.c"
    "${CMAKE_CURRENT_LIST_DIR}/Application/safety.c"
    "${CMAKE_CURRENT_LIST_DIR}/Application/phone_sync.c"
    "${CMAKE_CURRENT_LIST_DIR}/Application/power_manager.c"
    "${CMAKE_CURRENT_LIST_DIR}/Application/ota.c"
    "${CMAKE_CURRENT_LIST_DIR}/Application/watch_ui.c"
    "${CMAKE_CURRENT_LIST_DIR}/Application/watch_app.c"
    "${CMAKE_CURRENT_LIST_DIR}/System/crc32.c"
    "${CMAKE_CURRENT_LIST_DIR}/System/scheduler.c"
)
set(WATCH_APP_ENTRY "${CMAKE_CURRENT_LIST_DIR}/User/main.c")
set(WATCH_BOOT_SOURCES "${CMAKE_CURRENT_LIST_DIR}/User/bootloader.c")

# Boot源码单独登记，不混入当前App链接；Android由手机端工程编译。
set(WATCH_FIRMWARE_SOURCES ${WATCH_STARTUP_SOURCES} ${WATCH_HAL_SOURCES} ${WATCH_CORE_SOURCES} ${WATCH_APP_ENTRY})
