package com.team.smartwatch

import java.io.StringWriter
import java.util.zip.CRC32
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.flow.toList

internal suspend fun checkOfflineInterfaces(report: (String) -> Unit) {
    checkOfflineD3()
    report("D3 样例通过 · 连接后自动校时及新序号读回")
    if (BuildConfig.D3_FAULT_INJECTION) checkOfflineD3Faults(report)
    val device = OfflineWatchSession()
    val repository = WatchRepository(device, timeoutMillis = 1_000)
    try {
        val (info, status) = repository.synchronize()
        check(info.deviceId == "offline-watch")
        check((status.steps as Reading.Valid).value.count == 0)
        check(status.oxygenPercentEstimate is Reading.Invalid)
        check(repository.statusChanges.first() == status)
        report("设备信息与状态上报通过 · 保留零步数与无效血氧")

        val utc = 1_800_000_100L
        check(repository.synchronizeTime({ utc }, 0) == utc)
        device.mismatchTime = true
        expectError(WatchError.READBACK_MISMATCH) { repository.synchronizeTime({ utc }, 0) }
        device.mismatchTime = false
        val settings = repository.getSettings()
        val alarmSaved = repository.saveAlarm(false, 9, 15)
        check(alarmSaved == settings.copy(alarmEnabled = false, alarmHour = 9, alarmMinute = 15))
        val lowActivitySaved = repository.saveLowActivity(true, 60)
        check(lowActivitySaved == alarmSaved.copy(lowActivityEnabled = true, lowActivityMinutes = 60))
        device.mismatchSettings = true
        expectError(WatchError.READBACK_MISMATCH) { repository.saveAlarm(true, 7, 0) }
        report("提醒设置离线通过 · 分别保存闹钟和低运动 · 拦截回读不一致")

        val history = HistoryQuery(HistoryKind.STEPS, limit = 1)
        val first = repository.history(history)
        val second = repository.history(history.copy(cursor = checkNotNull(first.nextCursor)))
        check(second.nextCursor == null)
        expectError(WatchError.CURSOR_EXPIRED) { repository.history(history.copy(cursor = "expired")) }
        val records = first.records + second.records
        check(records.map { it.id }.distinct().size == 2)
        val csv = StringWriter().also { exportHistoryCsv(records, it) }.toString()
        check(csv.contains("\"0\",\"steps\",\"valid\",\"valid\""))
        check(csv.contains("\"second,\"\"demo\"") && csv.contains("\"\",\"steps\",\"valid\",\"invalid\""))
        for (kind in listOf(HistoryKind.HEART_RATE, HistoryKind.OXYGEN_ESTIMATE, HistoryKind.SOS)) {
            check(repository.history(history.copy(kind = kind)).records.single().kind == kind)
        }
        report("历史分页与 CSV 通过 · 区分零值、无效值和游标失效")

        val observed = 1_795_754_700L
        val weather = checkNotNull(CurrentWeather(2, -3.2, 68, observed * 1_000L)
            .toWatchWeather(observed * 1_000L))
        val example = byteArrayOf(0x02, 0xe0.toByte(), 0xff.toByte(), 0x44, 0xdc.toByte(), 0x18, 0x09, 0x6b)
        check(weather.encodeDraftWeather().contentEquals(example))
        check(repository.sendWeather(weather, observed) == weather)
        expectError(WatchError.INVALID_DATA) { repository.sendWeather(weather, weather.validUntilUtcSeconds) }
        report("F20 天气报文草案离线通过 · 多云、负温度与湿度 · 拦截过期")

        val events = repository.safetyEvents.toList()
        check(events.map { it.stage } ==
            listOf(SafetyStage.PENDING, SafetyStage.REQUESTED, SafetyStage.REQUESTED, SafetyStage.CANCELLED))
        val event = events[1]
        expectError(WatchError.INVALID_DATA) { repository.acknowledgeReceived(event.copy(deviceId = "other")) }
        repository.acknowledgeReceived(event)
        check(device.acknowledgedEventId == event.eventId)
        report("SOS 事件接收与确认通过 · 覆盖阶段变化、重复输入和设备归属")

        val bytes = byteArrayOf(1, 2, 3, 4, 5)
        val crc = CRC32().apply { update(bytes) }.value
        val image = OtaImage("demo", info.hardware, "offline-2", bytes.size.toLong(), crc)
        check(repository.queryUpdate() == null)
        check(repository.beginUpdate(image).confirmedOffset == 0L)
        expectError(WatchError.REJECTED) { repository.finishUpdate(image.id) }
        check(repository.writeUpdateChunk(image.id, 0, bytes.copyOfRange(0, 4)).confirmedOffset == 4L)
        val offset = checkNotNull(repository.queryUpdate()).confirmedOffset
        expectError(WatchError.REJECTED) { repository.writeUpdateChunk(image.id, 0, bytes) }
        check(repository.writeUpdateChunk(image.id, offset, bytes.copyOfRange(offset.toInt(), bytes.size))
            .confirmedOffset == image.length)
        check(repository.finishUpdate(image.id).stage == OtaStage.WAITING_FOR_RESTART)
        report("OTA 请求与分片确认通过 · 校验后进入等待重启")

        device.delayStatus = true
        expectError(WatchError.TIMEOUT) { repository.getStatus() }
    } finally {
        repository.close()
    }
    expectError(WatchError.DISCONNECTED) { repository.getStatus() }
    report("响应超时与会话关闭通过")
}

private suspend fun expectError(reason: WatchError, request: suspend () -> Unit) {
    try {
        request()
    } catch (error: WatchRequestException) {
        check(error.reason == reason) { "预期 $reason，实际 ${error.reason}" }
        return
    }
    error("预期设备请求失败：$reason")
}
