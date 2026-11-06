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

## F00-P01 核心板台架首启（原P01）

当前状态：2026-11-05用户确认P01烧录及实物验收通过。版本`P01-20261105-01`，仅使用核心板PC13低有效LED；内部HSI为16MHz，不验证25MHz HSE、LSE、RTC或外部Flash。该固件从`0x08000000`独立首启，不是后续Boot/App分区中的App镜像。

1. 先核对核心板版本、STM32F411CEU6丝印、外部Flash是否已焊及其丝印；电池、充电板和稳压板核对型号、正负极与接口，先不接入。断电检查核心板3V3/GND无持续短路。
2. 本轮只用核心板USB供电，断开外部稳压、电池和所有外设；ST-LINK只连接SWDIO→PA13、SWCLK→PA14、GND→GND。下载器若有独立VTref检测端，按其说明接目标3V3；不将下载器供电输出再并到已由USB供电的板上。BOOT0保持低，按实际下载器核对接口。
3. 上电测核心板3V3对GND。本轮台架目标为3.3V，验收范围3.2～3.4V；这不是芯片绝对额定范围。异常发热、极性不符或电压超出本轮范围时先断电并反馈。
4. 打开`code/smart_watch.uvprojx`，选择固定编译目标`smart_watch`；P01是当前任务编号，不是另一套工程。核对实际ST-LINK调试器与内部Flash下载算法后，由用户执行烧录。待验收固件为`code/Build/Objects/smart_watch_p01.hex`，同目录保留对应AXF和BIN。不得将当前首启HEX误认为已实现OTA的固件。
5. 正常现象：上电或复位后LED先短闪3次（每次亮100ms、灭100ms），暂停500ms后持续每秒亮200ms、灭800ms。初始化/异常故障为独立软件延时的连续闪烁，频率不作精确承诺；无灯、常亮、持续快速闪烁或停止心跳均不通过。
6. 复位3次、完全断电后重新上电3次，每次均应重复正常首启序列并稳定心跳。USB和SWD均可能影响断电判断，断电试验须断开所有可能供电路径。反馈固件版本、芯片/Flash丝印、供电电压，以及两组各3次的结果；未反馈前不记为通过。

验收记录（2026-11-05）：用户报告3V3为3.28V，确认本版本烧录完成、首启现象正确；按复位键及完全断电重上电均各至少3次，现象一致，明确验收通过。对应源码基线`a87b91a5f7ce462d7f4e17a04c7bf7bd81246f3d`，BIN SHA256为`a2f86276242d92b881a40d2cb808c0f2cd12787f0c89d304f40ee6c20440ab3a`。未收到主控/Flash丝印照片或线路变更；不据此推定外部Flash、电池供电/充电、外部晶振或其他外设已经验证。

## F00-P02 构建与SWD调试（原P02）

F系列为工程基础与功能分组，P为各组下的开发步骤，完整编号及依赖见本地计划表。日常开发沿用同一个工程、同一个`smart_watch`编译目标，现有功能接口骨架已统一加入编译，当前仍只运行首启代码。

已验证的构建环境为Keil uVision 5.43.1.0、Arm Compiler 6.24、Keil.STM32F4xx_DFP 3.1.1。在`code`目录，以本机`UV4.exe`的完整路径执行：

```text
UV4.exe -r smart_watch.uvprojx -t smart_watch -o Build/keil_build.log
```

构建完成后，`code/Build/Objects/smart_watch.axf`为ELF格式调试镜像，同目录生成`smart_watch.hex`；BIN由同工具链执行`fromelf.exe --bin --output Build/Objects/smart_watch.bin Build/Objects/smart_watch.axf`生成。日志和中间文件检查后清理，尚待用户验收的镜像保留。P01验收时的`smart_watch_p01.*`为历史临时文件，验收结束后清理；版本及哈希以本页记录为准。

2026-11-05软件检查：完整工程构建0错误0警告；56项C/汇编编译输入与`sources.cmake`的`WATCH_FIRMWARE_SOURCES`一致，启动向量及ELF格式已检查。已有接口骨架、头文件与资料共158项纳入工程视图，功能表所需HAL驱动已补齐；本次未改变功能代码，生成BIN与原P01已验收镜像逐字节一致，固件版本仍为`P01-20261105-01`。

接线沿用P01。工程默认下载器已配置为ST-LINK，本机已有调试设置仍应在Keil中核对：

1. 重新加载工程，在`Options for Target → Debug`选择`ST-Link Debugger`，打开`Settings`；端口使用SWD，核对能识别目标。
2. `Flash Download`页使用`STM32F4xx_512`内部Flash算法，地址`0x08000000`、大小`0x80000`；不得选择Option Bytes算法。按实物连接核对下载器电源与公共地。
3. 用户用当前工程重新构建、下载后，进入调试会话，应能停在`main`；完成`board_init`后查看`SystemCoreClock`为16000000，继续运行后LED应与P01一致。既有重启结果仅对应上方已记录的同版本镜像，SWD调试操作仍待本次用户确认。

2026-11-06调试器核验：Codex直接操作当前Keil/ST-Link调试会话，确认已加载`Build/Objects/smart_watch.axf`并停在`main`；在初始化后的首次`HAL_GPIO_WritePin`调用处命中函数断点，调试器读取`SystemCoreClock=16000000`。本次新增的唯一断点已移除，程序已继续运行；这些是调试器观察结果，LED实物现象仍待用户确认。

F00-P02实物调试验收（2026-11-06）：在上述ST-Link调试核验后移除断点继续运行，用户明确确认LED恢复每秒一次心跳，符合亮约200ms、灭约800ms的预期。当前BIN与F00-P01验收版一致，复位和断电重启记录沿用F00-P01；本项调试与运行现象验收通过。

资料：
- [WeAct核心板官方资料](https://github.com/WeActStudio/WeActStudio.MiniSTM32F4x1)
- [DFR0650官方说明](https://wiki.dfrobot.com/dfr0650/)
- [TEL0073官方说明](https://wiki.dfrobot.com.cn/_SKU_TEL0073_BLE-Link_%E8%93%9D%E7%89%994.0%E6%97%A0%E7%BA%BF%E9%80%9A%E8%AE%AF%E6%A8%A1%E5%9D%97)
- [DRI0056官方库](https://github.com/DFRobot/DFRobot_TM6605)
