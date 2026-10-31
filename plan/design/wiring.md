# 总线与供电分配

以下为拟定分配，未完成实物验证。逐功能接线记录见functions/。各功能文件是对应功能验收时的线路记录入口，修改公共引脚时检查所有引用。

| 硬件信号 | 主控或电源端 | 连接信息 | 核对项 |
| --- | --- | --- | --- |
| F411板载RTC/LSE | 板内连接 | 板内连接 | 核对32.768kHz晶振实物；不外接GPIO |
| DFR0650 VCC / GND | 3V3 / GND | 3.3V电源轨 / 公共地 | 默认I2C；D/C和CS按默认I2C模式保持，不猜测桥接 |
| DFR0650 SCL / SDA | PB6 / PB7 | I2C1 SCL / SDA | 7位地址0x3C；以实物扫描复核 |
| 菜单键 / SOS键 | PB12 / PB13 | 每键一端接GPIO，另一端接GND | 内部上拉，按下低电平；四脚按导通关系接线 |
| 板载辅助KEY | PA0 | 板内连接 | 极性以实物及原理图核对；与外置按键配置分开 |
| DRI0056 电源 / 地 | 3V3 / GND | 套装驱动板电源及公共地 | TM6605驱动配套LRA，马达只接驱动输出端 |
| DRI0056 SCL / SDA | PB6 / PB7 | 共享I2C1 | 拟用7位地址0x2D，调试先扫描与核对手册 |
| GY-521 VCC / GND | 3V3 / GND | 模块电源及公共地 | 核对模块稳压、电平及实际供电要求 |
| GY-521 SCL / SDA / INT | PB6 / PB7 / PB0 | I2C1与运动中断 | AD0接地拟用0x68；XDA/XCL不用 |
| TEL0073 VCC / GND | 3V3 / GND | 电源及公共地 | 2.0mm接口；核对实物丝印与厂商引脚图后接线 |
| TEL0073 RX / TX | PA9 / PA10 | STM32 TX→模块RX；模块TX→STM32 RX | USART1；波特率与BLE服务/特征UUID待实测；不用其他模块默认值 |
| 板载SPI Flash CS / SCK / MISO / MOSI | PA4 / PA5 / PA6 / PA7 | SPI1，核心板板内连接 | 接线按WeAct V3.1原理图和实物复核；读取JEDEC后适配实际芯片 |
| 电池采样 | PA1 | 电池正极→100kΩ→采样点→100kΩ→GND；采样点接PA1 | 采样点并100nF到地；电阻1%；ADC1_IN1；满电输入先实测 |
| MAX30102模块 VCC / GND | 3V3 / GND | 模块电源及公共地 | 使用完整模块；确认稳压与上拉电压，不能按裸芯片接法供电 |
| MAX30102 SCL / SDA / INT | PB6 / PB7 / PB1 | I2C1与FIFO中断 | 7位地址0x57；遮光与贴合接触，INT电平待核对 |
| FIT0120电池 | DFR0668电池端；开关输入 | 电池带保护，按实物核对PH2.0正负极 | 充电支路在总开关前；充电时关闭负载 |
| 总开关→DFR0568 | DFR0568输入→3.3V输出 | 输出接核心板3V3及模块电源轨，公共地 | 不要把3.3V接VIN；输入/输出焊盘以实物为准 |
| DFR0668 Type-C | 外部USB 5V | 仅用于充电板输入 | 初装50mA，核对电池后确定档位；隔离核心板USB与外部供电 |
| ST-LINK SWDIO / SWCLK / GND | PA13 / PA14 / GND | 调试器信号与公共地 | 参考电压和供电引脚依具体下载器说明；避免多路电源并联 |

PA13/PA14保留SWD；PA4～PA7保留板载Flash；PA8不分配。电源和总线先确认再接其他外设。

资料：
- [WeAct核心板官方资料](https://github.com/WeActStudio/WeActStudio.MiniSTM32F4x1)
- [DFR0650官方说明](https://wiki.dfrobot.com/dfr0650/)
- [TEL0073官方说明](https://wiki.dfrobot.com.cn/_SKU_TEL0073_BLE-Link_%E8%93%9D%E7%89%994.0%E6%97%A0%E7%BA%BF%E9%80%9A%E8%AE%AF%E6%A8%A1%E5%9D%97)
- [DRI0056官方库](https://github.com/DFRobot/DFRobot_TM6605)
