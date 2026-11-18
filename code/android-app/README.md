# SmartWatch Android 手机端

这是智能手表的 Android 伴侣应用。当前调试版为 **1.8.0**（versionCode 12）。普通版已整合真实 BLE 扫描、连接和串口收发入口。原有健康趋势、天气、校时、闹钟、低运动提醒和 SOS 演示仍可在模拟器中使用。[下载 APK 与查看版本说明](https://github.com/why-can-not-i-get-c-language/smart_watch/releases/tag/android-v1.8.0)

将发布页的 APK 发送到调试手机，支持 Android 7.0 及以上。安装时识别的应用名为「智能手表」，包名为 `com.team.smartwatch.prototype`。已装同签名、较低版本的普通版可直接更新。原独立蓝牙联调版（包名后缀 `.integration`）可与普通版共存。

## APP-02 真机收发

1. 手表运行 `F06-P01-20261115-01` 固件并停在「BLE UART CHECK」。断开其他占用手表的 BLE 应用。
2. 手机上开启蓝牙和定位，在「设备连接 → 真实 BLE 收发」授予精确位置权限，Android 12 及以上还需授予「附近设备」权限。扫描后按设备地址选择手表，点「连接并发现 GATT 服务」。
3. 进入「串口收发」，按 HEX 发送 `31 32 33 34`。在 RX 记录中核对相同的 4 字节，并观察手表 RX、TX 各增加 4。完整验收还覆盖主动上报、二进制、分片、重连和手表熄屏，详见[本版对应的计划书](https://github.com/why-can-not-i-get-c-language/smart_watch/blob/f0a2f4ee8efe904d6cc7fd5ecec095047d8d1504/plan/task.md#基础透传对接要求-app-02)。

## 构建与版本管理

用 Android Studio 打开本目录，安装 Android SDK Platform 37 并使用 JDK 25。构建可覆盖安装的开发包需要同一把开发私钥。维护者从私有备份恢复到用户目录的 `.android/smartwatch-development.keystore`，或设置 `SMARTWATCH_DEVELOPMENT_KEYSTORE` 指向该文件。Gradle 会检查证书指纹。Windows 执行 `gradlew.bat :app:assembleDebug`，macOS/Linux 执行 `./gradlew :app:assembleDebug`。APK 输出到 `app/build/outputs/apk/debug/`。

手机端源码在 `software` 分支维护。发布新版时递增 `app/build.gradle.kts` 的 `versionCode`，更新 `versionName`，在对应源码提交打 `android-v<versionName>` 标签。每版 APK、SHA-256 和测试范围保存在同名 GitHub Release 中；已分发的标签和附件保留原样。待手表实测的调试版标记为预发布。

在首页进入「跌倒 SOS」，填入演示联系人、排列优先级并开启「自动拨号与短信（模拟）」。完成设备连接的模拟同步后触发「模拟手表 SOS」。第一位 30 秒未接时，应用模拟向全部联系人提交一批短信，同时继续拨下一位。开启 SOS 通知后可测试 Android 系统通知和锁屏显示。

首页「手表闹钟」可选择每日时间和启停。完成模拟连接后保存，可查看模拟手表读回的设置。「低运动提醒」支持 30、60 或 90 分钟。保存后点「模拟低运动 +15 分钟」累计时长，达到阈值后显示提醒。点「模拟恢复活动」清零。

手表事件、电话和短信均使用模拟数据。真实 BLE 接收、拨号、短信发送、到点振动和运动检测需独立实机验收。

## 模拟器状态回归

先在 Medium Phone API 37.0（1080 × 2400，竖屏）上安装并运行调试版，再在 PowerShell 7 执行下列命令。示例使用开发环境的 ADB 5038 和 emulator-5586。若使用 Android Studio 默认 ADB 服务，改为 5037，并按 `adb devices` 的结果填写模拟器编号。

```powershell
pwsh -File ./tools/check-android-state-regression.ps1 -AdbPath "$env:ANDROID_HOME/platform-tools/adb.exe" -AdbPort 5038 -Device emulator-5586
```

脚本会重开 App、点击页面并核对结果。可通过 `-Section Readings` 或 `-Section Settings` 分组执行。日志写入本地 `worklogs/`。2026-11-10 已用 1.7 验证无数据与 0 步区分、过期读数与天气、闹钟保存失败及断线重连。真实 BLE 晚到报文留待实机验证。

## 权限演示与校时复测

运行 `gradlew.bat :app:assemblePermissionPreview` 可构建「智能手表（权限演示）」，输出位于 `app/build/outputs/apk/permissionPreview/`。它使用独立应用 ID `com.team.smartwatch.prototype.permissions`，可以与普通调试版共存。

「设备连接」可试 Android 的「附近设备」授权弹窗。设备同步采用模拟数据。SOS 页可单独授权前台定位，发起模拟求助时尝试一次定位，最多 6 秒。短信预览和手动微信分享文本可附高德地图链接。无法定位时仍生成求助预览。带位置的短信预览保存在本机求助记录。

2026-11-11 修复模拟校时跨秒时慢一秒的问题。模拟器复测中，手动校时后 5 次取样与重连自动校时后 3 次取样均显示手机和模拟手表时间一致。模拟回读不一致时仍提示失败。红米 K70E 复测、真实手表 RTC/BLE 校时和真实短信发送待验收。
