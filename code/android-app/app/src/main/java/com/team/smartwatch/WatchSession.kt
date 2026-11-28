package com.team.smartwatch

import kotlinx.coroutines.flow.Flow

// 一次已发现并订阅业务通道的会话。重连创建新实例，关闭后终止待处理请求和事件流。
// 实物实现负责帧编解码、匹配应答、超时与设备错误映射，单位和类型不等于线上字节布局。
internal interface WatchSession {
    val statusChanges: Flow<DeviceStatus>
    val safetyEvents: Flow<SafetyEvent>
    suspend fun deviceInfo(): DeviceInfo
    suspend fun getStatus(): DeviceStatus
    suspend fun setTime(utcSeconds: Long): Long
    suspend fun getSettings(): DeviceSettings
    suspend fun setSettings(settings: DeviceSettings): DeviceSettings
    suspend fun getHistory(query: HistoryQuery): HistoryPage
    suspend fun syncWeather(weather: WatchWeather): WatchWeather
    suspend fun acknowledgeEvent(eventId: String)
    suspend fun otaBegin(image: OtaImage): OtaProgress
    suspend fun otaQuery(): OtaProgress?
    suspend fun otaChunk(imageId: String, offset: Long, bytes: ByteArray): OtaProgress
    suspend fun otaFinish(imageId: String): OtaProgress
    fun close()
}

internal enum class WatchCapability { STATUS, TIME, SETTINGS, HISTORY, WEATHER, SAFETY, OTA }

internal data class DeviceInfo(
    val deviceId: String,
    val hardware: String,
    val firmwareVersion: String,
    val protocolVersion: String,
    val capabilities: Set<WatchCapability>,
)

internal data class DeviceStatus(
    val timeUtcSeconds: Reading<Long>,
    val steps: Reading<DailySteps>,
    val batteryMillivolts: Reading<Int>,
    val batteryPercentEstimate: Reading<Int>,
    val heartRateBpm: Reading<Int>,
    val oxygenPercentEstimate: Reading<Int>,
)

internal data class DeviceSettings(
    val alarmEnabled: Boolean,
    val alarmHour: Int,
    val alarmMinute: Int,
    val lowActivityEnabled: Boolean,
    val lowActivityMinutes: Int,
    val reminderDelaySeconds: Int = 30,
)

internal enum class HistoryKind { STEPS, DAILY_PARTIAL_STEPS, MANUAL_EVENT, HEART_RATE, OXYGEN_ESTIMATE, SOS }
internal data class HistoryQuery(
    val kind: HistoryKind,
    val limit: Int,
    val cursor: String? = null,
    val fromUtcSeconds: Long? = null,
    val untilUtcSeconds: Long? = null,
)
internal data class WatchRecord(
    val id: String,
    val kind: HistoryKind,
    val utcSeconds: Reading<Long>,
    val value: Reading<Int>,
)
internal data class HistoryPage(val records: List<WatchRecord>, val nextCursor: String?)

internal enum class SafetyOrigin { MANUAL, FALL }
internal enum class SafetyStage { PENDING, REQUESTED, CANCELLED }
internal data class SafetyEvent(
    val deviceId: String,
    val eventId: String,
    val origin: SafetyOrigin,
    val occurredUtcSeconds: Reading<Long>,
    val stage: SafetyStage,
)

internal data class OtaImage(
    val id: String,
    val hardware: String,
    val version: String,
    val length: Long,
    val crc32: Long,
)
internal enum class OtaStage { RECEIVING, WAITING_FOR_RESTART }
internal data class OtaProgress(
    val imageId: String,
    val confirmedOffset: Long,
    val maxChunkBytes: Int,
    val stage: OtaStage,
)

internal enum class WatchError { DISCONNECTED, TIMEOUT, REJECTED, UNSUPPORTED, INVALID_DATA, READBACK_MISMATCH, CURSOR_EXPIRED }

internal class WatchRequestException(val reason: WatchError, message: String) : Exception(message)
