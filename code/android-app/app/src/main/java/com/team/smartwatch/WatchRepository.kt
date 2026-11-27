package com.team.smartwatch

import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withTimeoutOrNull

internal class WatchRepository(private val session: WatchSession, private val timeoutMillis: Long = 5_000) {
    private val requests = Mutex()
    private var closed = false
    val statusChanges get() = session.statusChanges
    val safetyEvents get() = session.safetyEvents

    private suspend fun <T> request(operation: suspend WatchSession.() -> T): T = requests.withLock {
        if (closed) throw WatchRequestException(WatchError.DISCONNECTED, "设备会话已关闭")
        val result = withTimeoutOrNull(timeoutMillis) { Result.success(session.operation()) }
            ?: throw WatchRequestException(WatchError.TIMEOUT, "设备响应超时")
        result.getOrThrow()
    }

    suspend fun deviceInfo() = request { deviceInfo() }
    suspend fun getStatus() = request { getStatus() }
    suspend fun synchronize() = request { deviceInfo() to getStatus() }

    // 时间容差由联调确定。使用传入的时钟分别取发送和读回时间，避免写入后按发送瞬间核对。
    suspend fun synchronizeTime(nowUtcSeconds: () -> Long, toleranceSeconds: Long): Long = request {
        val requested = nowUtcSeconds()
        if (requested !in 946684800L..4102444799L || toleranceSeconds < 0) {
            throw WatchRequestException(WatchError.INVALID_DATA, "校时值或允许误差超出范围")
        }
        val stored = setTime(requested)
        val readback = (getStatus().timeUtcSeconds as? Reading.Valid)?.value
        val finished = nowUtcSeconds()
        if (stored != requested || readback == null || finished < requested ||
            readback < requested || (readback > finished && readback - finished > toleranceSeconds)
        ) throw WatchRequestException(WatchError.READBACK_MISMATCH, "校时回读未确认")
        readback
    }

    suspend fun getSettings() = request { getSettings() }

    suspend fun saveSettings(settings: DeviceSettings) = request { writeAndVerifySettings(settings) }

    suspend fun saveAlarm(enabled: Boolean, hour: Int, minute: Int) = request {
        val existing = getSettings()
        writeAndVerifySettings(existing.copy(alarmEnabled = enabled, alarmHour = hour, alarmMinute = minute))
    }

    suspend fun saveLowActivity(enabled: Boolean, minutes: Int) = request {
        val existing = getSettings()
        writeAndVerifySettings(existing.copy(lowActivityEnabled = enabled, lowActivityMinutes = minutes))
    }

    private suspend fun WatchSession.writeAndVerifySettings(settings: DeviceSettings): DeviceSettings {
        if (settings.alarmHour !in 0..23 || settings.alarmMinute !in 0..59 || settings.lowActivityMinutes <= 0) {
            throw WatchRequestException(WatchError.INVALID_DATA, "提醒参数无效")
        }
        if (setSettings(settings) != settings || getSettings() != settings) {
            throw WatchRequestException(WatchError.READBACK_MISMATCH, "提醒设置回读不一致")
        }
        return settings
    }

    suspend fun history(query: HistoryQuery) = request {
        if (query.limit <= 0 || (query.fromUtcSeconds != null && query.untilUtcSeconds != null &&
                query.fromUtcSeconds >= query.untilUtcSeconds)) {
            throw WatchRequestException(WatchError.INVALID_DATA, "历史查询范围无效")
        }
        val page = getHistory(query)
        if (page.records.size > query.limit || page.records.any { it.id.isBlank() || it.kind != query.kind } ||
            (page.nextCursor != null && (page.nextCursor.isBlank() || page.nextCursor == query.cursor))) {
            throw WatchRequestException(WatchError.INVALID_DATA, "手表返回的历史分页无效")
        }
        page
    }

    suspend fun sendWeather(weather: WatchWeather, nowUtcSeconds: Long) = request {
        if (weather.validUntilUtcSeconds <= nowUtcSeconds || !weather.hasValidFields()) {
            throw WatchRequestException(WatchError.INVALID_DATA, "天气已过期或字段无效")
        }
        val confirmed = syncWeather(weather)
        if (confirmed != weather) throw WatchRequestException(WatchError.READBACK_MISMATCH, "天气回执不一致")
        confirmed
    }

    suspend fun acknowledgeReceived(event: SafetyEvent) = request {
        if (event.eventId.isBlank() || event.deviceId != deviceInfo().deviceId) {
            throw WatchRequestException(WatchError.INVALID_DATA, "求助事件不属于当前设备")
        }
        acknowledgeEvent(event.eventId)
    }

    suspend fun beginUpdate(image: OtaImage) = request {
        if (image.id.isBlank() || image.length <= 0 || image.crc32 !in 0..0xffffffffL ||
            image.hardware != deviceInfo().hardware) {
            throw WatchRequestException(WatchError.INVALID_DATA, "固件元数据或硬件型号不匹配")
        }
        otaBegin(image).also {
            if (it.imageId != image.id || it.confirmedOffset !in 0..image.length ||
                it.maxChunkBytes <= 0 || it.stage != OtaStage.RECEIVING) {
                throw WatchRequestException(WatchError.INVALID_DATA, "升级开始回执无效")
            }
        }
    }
    suspend fun queryUpdate() = request { otaQuery() }
    suspend fun writeUpdateChunk(imageId: String, offset: Long, bytes: ByteArray) = request {
        if (imageId.isBlank() || offset < 0 || bytes.isEmpty() || offset > Long.MAX_VALUE - bytes.size) {
            throw WatchRequestException(WatchError.INVALID_DATA, "固件分片无效")
        }
        otaChunk(imageId, offset, bytes).also {
            if (it.imageId != imageId || it.confirmedOffset != offset + bytes.size ||
                it.maxChunkBytes <= 0 || it.stage != OtaStage.RECEIVING) {
                throw WatchRequestException(WatchError.INVALID_DATA, "升级分片回执无效，请查询设备偏移")
            }
        }
    }
    suspend fun finishUpdate(imageId: String) = request {
        if (imageId.isBlank()) throw WatchRequestException(WatchError.INVALID_DATA, "缺少镜像标识")
        otaFinish(imageId).also {
            if (it.imageId != imageId || it.stage != OtaStage.WAITING_FOR_RESTART) {
                throw WatchRequestException(WatchError.INVALID_DATA, "升级结束回执无效")
            }
        }
    }

    suspend fun close() = requests.withLock {
        if (!closed) {
            closed = true
            session.close()
        }
    }
}
