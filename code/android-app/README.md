# SmartWatch Android 手机端

这是智能手表的 Android 伴侣应用。当前版本为 `1.5-sos-escalation-sim`（versionCode 6），可在模拟器体验首页、健康趋势、天气、设备连接、校时、提醒和 SOS 流程。

用 Android Studio 打开本目录，安装 Android SDK Platform 37 并使用 JDK 25。完成 Gradle 同步后，选择已启动的 Android 模拟器并运行 `app`。也可在本目录运行 Windows 的 `gradlew.bat :app:assembleDebug` 或 macOS/Linux 的 `./gradlew :app:assembleDebug`。调试 APK 输出到 `app/build/outputs/apk/debug/`。

在首页进入「跌倒 SOS」，填入演示联系人、排列优先级并开启「自动拨号与短信（模拟）」。完成设备连接的模拟同步后触发「模拟手表 SOS」。第一位 30 秒未接时，应用模拟向全部联系人提交一批短信，同时继续拨下一位。开启 SOS 通知后可测试 Android 系统通知和锁屏显示。

手表事件、电话和短信均使用模拟数据。真实 BLE 接收、拨号与短信发送需独立实机验收。
