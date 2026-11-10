#Requires -Version 7.0
[CmdletBinding()]
param(
    [ValidateSet('All', 'Readings', 'Settings')][string]$Section = 'All',
    [string]$AdbPath = 'D:/Android/Sdk/platform-tools/adb.exe',
    [int]$AdbPort = 5038,
    [ValidatePattern('^emulator-\d+$')][string]$Device = 'emulator-5586',
    [string]$LogPath
)
$ErrorActionPreference = 'Stop'
$package = 'com.team.smartwatch.prototype'
if (!$LogPath) {
    $beijingDate = [DateTime]::UtcNow.AddHours(8).ToString('yyyy-MM-dd')
    $LogPath = Join-Path $PSScriptRoot "../worklogs/$beijingDate-state-regression.log"
}
$LogPath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($LogPath)
[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($LogPath)) | Out-Null

function Invoke-Adb {
    & $AdbPath -P $AdbPort -s $Device @args
    if ($LASTEXITCODE -ne 0) { throw "ADB 命令失败：$args" }
}

function Read-Screen {
    for ($attempt = 0; $attempt -lt 4; $attempt++) {
        $result = Invoke-Adb shell uiautomator dump /data/local/tmp/smartwatch-state-regression.xml
        if ($result -match 'dumped to:') {
            $script:screen = [xml]((Invoke-Adb exec-out cat /data/local/tmp/smartwatch-state-regression.xml) -join '')
            return
        }
    }
    throw '无法读取模拟器界面'
}

function Get-TextNode([string]$Text) {
    $script:screen.SelectSingleNode("//node[@text='$Text']")
}

function Assert-Text([string]$Text) {
    if (!(Get-TextNode $Text)) { throw "缺少界面文字：$Text" }
}

function Assert-Absent([string]$Text) {
    if (Get-TextNode $Text) { throw "界面不应显示：$Text" }
}

function Get-TapPoint($Node) {
    if (!$Node -or $Node.bounds -notmatch '^\[(\d+),(\d+)\]\[(\d+),(\d+)\]$') {
        throw '找不到可点击区域'
    }
    @([int](([int]$Matches[1] + [int]$Matches[3]) / 2),
      [int](([int]$Matches[2] + [int]$Matches[4]) / 2))
}

function Tap-Node($Node) {
    $point = Get-TapPoint $Node
    Invoke-Adb shell input tap $point[0] $point[1] | Out-Null
    Read-Screen
}

function Tap-Text([string]$Text) {
    Assert-Text $Text
    Tap-Node (Get-TextNode $Text)
}

function Scroll([bool]$TowardsTop) {
    if ($TowardsTop) { Invoke-Adb shell input swipe 540 450 540 2080 250 | Out-Null }
    else { Invoke-Adb shell input swipe 540 2080 540 450 250 | Out-Null }
    Read-Screen
}

function Find-Text([string]$Text) {
    for ($i = 0; $i -lt 6; $i++) {
        $node = Get-TextNode $Text
        if ($node -and $node.bounds -match '^\[(\d+),(\d+)\]\[(\d+),(\d+)\]$' -and
            [int]$Matches[4] - [int]$Matches[2] -gt 25) { return }
        Scroll $false
    }
    throw "滚动后仍未找到：$Text"
}

function Go-Text([string]$Text) {
    Find-Text $Text
    Tap-Text $Text
}

function Assert-Disabled([string]$Text) {
    $node = Get-TextNode $Text
    while ($node -and $node.clickable -ne 'true') { $node = $node.ParentNode }
    if (!$node -or $node.enabled -ne 'false') { throw "操作应被禁用：$Text" }
}

function Note([string]$Message) {
    $line = "$([DateTime]::UtcNow.AddHours(8).ToString('yyyy-MM-dd HH:mm:ss')) +08:00 PASS $Message"
    Write-Output $line
    Add-Content -LiteralPath $LogPath -Encoding utf8 -Value $line
}

function Restart-Application {
    Invoke-Adb shell am force-stop $package | Out-Null
    Invoke-Adb shell am start -n "$package/com.team.smartwatch.MainActivity" | Out-Null
    Start-Sleep -Seconds 2
    Read-Screen
}

function Connect-Watch {
    Tap-Text '开始模拟连接'
    Tap-Text '模拟链路建立'
    Tap-Text '模拟同步完成'
    Tap-Text '返回首页'
}

function Save-Alarm {
    Tap-Text '保存闹钟'
    Start-Sleep -Milliseconds 1900
    Read-Screen
}

Invoke-Adb shell input keyevent 224 | Out-Null
Invoke-Adb shell wm dismiss-keyguard | Out-Null
if ($Section -in 'All', 'Readings') {
    Restart-Application
    Find-Text '— · 待同步'
    Go-Text '切换模拟场景'
    Scroll $true
    Find-Text '0 步'
    Assert-Text '0 步'
    Note '首开无数据与有效零步数分别显示。'
    Go-Text '切换模拟场景'
    Scroll $true
    Find-Text '— · 佩戴接触不良'
    Assert-Absent '72 bpm'
    Go-Text '切换模拟场景'
    Scroll $true
    Find-Text '— · 数据已过期'
    Assert-Text '上次接收于 09:30'
    Assert-Absent '72 bpm'
    Note '无效心率给出原因，过期心率保留接收时间并隐藏旧读数。'
    Find-Text '切换天气场景'
    Tap-Text '切换天气场景'
    Assert-Text '刷新失败 · 显示当日缓存'
    Tap-Text '切换天气场景'
    Assert-Text '过期天气无法同步'
    Assert-Disabled '模拟同步天气'
    Assert-Absent '杭州 · 多云 · 16–23°C'
    Note '天气刷新失败显示当日缓存。过期数据隐藏且禁止同步。'
}

if ($Section -in 'All', 'Settings') {
    Restart-Application
    Go-Text '手表闹钟'
    Assert-Disabled '保存闹钟'
    Tap-Text '设备连接'
    Connect-Watch
    Save-Alarm
    Assert-Text '模拟手表闹钟：每日 08:30 · 开启'
    Tap-Node ($script:screen.SelectSingleNode("//node[@checkable='true']"))
    foreach ($failure in @('模拟设备拒绝设置，请重试','模拟回读不一致，请重试','模拟应答超时，请重试')) {
        Go-Text '切换演示结果'
        Scroll $true
        Save-Alarm
        Assert-Text "已同步 · $failure"
        Assert-Text '模拟手表闹钟：每日 08:30 · 开启'
    }
    Note '设备拒绝、回读不一致和超时均保留上次已确认的开启状态。'
    Go-Text '切换演示结果'
    Scroll $true
    # 预取点击坐标，切断连接时不等待逐个界面树的刷新。
    $savePoint = Get-TapPoint (Get-TextNode '保存闹钟')
    $devicePoint = Get-TapPoint (Get-TextNode '设备连接')
    Tap-Text '设备连接'
    $disconnectPoint = Get-TapPoint (Get-TextNode '断开连接')
    Tap-Text '返回首页'
    Invoke-Adb shell "input tap $($savePoint[0]) $($savePoint[1]); sleep 0.12; input tap $($devicePoint[0]) $($devicePoint[1]); sleep 0.14; input tap $($disconnectPoint[0]) $($disconnectPoint[1])" | Out-Null
    Read-Screen
    Assert-Text '开始模拟连接'
    Tap-Text '返回首页'
    Assert-Text '未连接 · 连接已变化，本次设置未确认'
    Assert-Text '模拟手表闹钟：尚未确认'
    Assert-Disabled '保存闹钟'
    Tap-Text '设备连接'
    Connect-Watch
    Assert-Text '模拟手表闹钟：尚未确认'
    Save-Alarm
    Assert-Text '模拟手表闹钟：每日 08:30 · 关闭'
    Note '旧会话待确认请求因断线失效。经过原回读时间仍未确认，重连后新请求可确认关闭。'
}
