package com.team.smartwatch

import java.util.zip.CRC32
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.flow
import kotlinx.coroutines.flow.flowOf

// 仅联调构建中的内存设备，使用逻辑对象而非假定的 BLE 报文。
internal class OfflineWatchSession : WatchSession {
    private val utc = 1_800_000_000L
    private val info = DeviceInfo("offline-watch", "simulated", "offline-1", "offline",
        WatchCapability.entries.toSet())
    private var time = utc
    private var settings = DeviceSettings(true, 8, 30, true, 30)
    private var image: OtaImage? = null
    private val crc = CRC32()
    private var progress: OtaProgress? = null
    var delayStatus = false
    var mismatchSettings = false
    var mismatchTime = false
    private val event = SafetyEvent(info.deviceId, "offline-sos", SafetyOrigin.FALL,
        valid(utc), SafetyStage.REQUESTED)
    var acknowledgedEventId: String? = null
        private set
    override val safetyEvents = flowOf(event.copy(stage = SafetyStage.PENDING), event, event,
        event.copy(stage = SafetyStage.CANCELLED))
    override val statusChanges = flow { emit(getStatus()) }

    private fun <T> valid(value: T) = Reading.Valid(value, "离线测试")
    override suspend fun deviceInfo() = info
    override suspend fun getStatus(): DeviceStatus {
        if (delayStatus) delay(60_000)
        return DeviceStatus(valid(if (mismatchTime) time - 60 else time), valid(DailySteps(0, "2027-01-15")),
            valid(3900), valid(70), valid(72), Reading.Invalid("佩戴接触不良"))
    }
    override suspend fun setTime(utcSeconds: Long): Long {
        time = utcSeconds
        return time
    }
    override suspend fun getSettings() = if (mismatchSettings) settings.copy(alarmEnabled = !settings.alarmEnabled) else settings
    override suspend fun setSettings(settings: DeviceSettings): DeviceSettings {
        this.settings = settings
        return settings
    }
    override suspend fun getHistory(query: HistoryQuery): HistoryPage {
        val records = listOf(
            WatchRecord("first", query.kind, valid(utc - 60), valid(0)),
            WatchRecord("second,\"demo", query.kind, valid(utc), Reading.Invalid("缺测")),
        ).filter { record ->
            val time = (record.utcSeconds as Reading.Valid).value
            time >= (query.fromUtcSeconds ?: Long.MIN_VALUE) && time < (query.untilUtcSeconds ?: Long.MAX_VALUE)
        }
        val start = when (query.cursor) {
            null -> 0
            else -> records.indexOfFirst { it.id == query.cursor }.takeIf { it >= 0 }?.plus(1)
                ?: throw WatchRequestException(WatchError.CURSOR_EXPIRED, "历史游标失效")
        }
        val page = records.drop(start).take(query.limit)
        return HistoryPage(page, page.lastOrNull()?.id?.takeIf { start + page.size < records.size })
    }
    override suspend fun syncWeather(weather: WatchWeather) = weather
    override suspend fun acknowledgeEvent(eventId: String) {
        if (eventId != event.eventId) throw WatchRequestException(WatchError.REJECTED, "未知求助事件")
        acknowledgedEventId = eventId
    }
    override suspend fun otaBegin(image: OtaImage): OtaProgress {
        if (progress != null) throw WatchRequestException(WatchError.REJECTED, "已有升级会话")
        this.image = image
        return OtaProgress(image.id, 0, 4, OtaStage.RECEIVING).also { progress = it }
    }
    override suspend fun otaQuery() = progress
    override suspend fun otaChunk(imageId: String, offset: Long, bytes: ByteArray): OtaProgress {
        val current = receiving(imageId)
        val remaining = checkNotNull(image).length - current.confirmedOffset
        if (offset != current.confirmedOffset || bytes.size > current.maxChunkBytes || bytes.size > remaining) {
            throw WatchRequestException(WatchError.REJECTED, "设备拒绝分片长度或偏移")
        }
        crc.update(bytes)
        return current.copy(confirmedOffset = offset + bytes.size).also { progress = it }
    }
    override suspend fun otaFinish(imageId: String): OtaProgress {
        val current = receiving(imageId)
        val target = checkNotNull(image)
        if (current.confirmedOffset != target.length || crc.value != target.crc32) {
            throw WatchRequestException(WatchError.REJECTED, "镜像未传完或 CRC 不匹配")
        }
        return current.copy(stage = OtaStage.WAITING_FOR_RESTART).also { progress = it }
    }
    private fun receiving(imageId: String): OtaProgress =
        progress?.takeIf { it.imageId == imageId && it.stage == OtaStage.RECEIVING }
            ?: throw WatchRequestException(WatchError.REJECTED, "无匹配的升级会话")

    override fun close() = Unit
}
