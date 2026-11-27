package com.team.smartwatch

import java.nio.ByteBuffer
import java.nio.ByteOrder
import kotlin.math.roundToInt

internal enum class WatchWeatherIcon(val wire: Int) {
    UNKNOWN(0), CLEAR(1), CLOUDY(2), OVERCAST(3), FOG(4), RAIN(5), SNOW(6), THUNDERSTORM(7);

    companion object {
        fun fromWeatherCode(code: Int): WatchWeatherIcon = when (code) {
            0, 1 -> CLEAR
            2 -> CLOUDY
            3 -> OVERCAST
            45, 48 -> FOG
            51, 53, 55, 56, 57, 61, 63, 65, 66, 67, 80, 81, 82 -> RAIN
            71, 73, 75, 77, 85, 86 -> SNOW
            95, 96, 99 -> THUNDERSTORM
            else -> UNKNOWN
        }
    }
}

internal data class WatchWeather(
    val icon: WatchWeatherIcon,
    val temperatureTenths: Int,
    val humidityPercent: Int,
    val validUntilUtcSeconds: Long,
) {
    fun hasValidFields(): Boolean =
        temperatureTenths in Short.MIN_VALUE..Short.MAX_VALUE &&
            humidityPercent in 0..100 && validUntilUtcSeconds in D3_MIN_UTC..D3_MAX_UTC
}

internal const val CURRENT_WEATHER_MAX_AGE_MS = 60 * 60 * 1_000L
private const val WEATHER_PAYLOAD_BYTES = 8

internal fun CurrentWeather.toWatchWeather(nowMillis: Long): WatchWeather? {
    if (!isFresh(nowMillis) || !temperature.isFinite()) return null
    val expiry = observedAt / 1_000 + CURRENT_WEATHER_MAX_AGE_MS / 1_000
    return WatchWeather(
        WatchWeatherIcon.fromWeatherCode(weatherCode),
        (temperature * 10).roundToInt(),
        humidityPercent,
        expiry,
    ).takeIf { it.hasValidFields() && expiry > nowMillis / 1_000 }
}

// F20 draft payload for offline verification. The BLE command and capability are still pending.
internal fun WatchWeather.encodeDraftWeather(): ByteArray {
    require(hasValidFields())
    return ByteBuffer.allocate(WEATHER_PAYLOAD_BYTES).order(ByteOrder.LITTLE_ENDIAN)
        .put(icon.wire.toByte()).putShort(temperatureTenths.toShort())
        .put(humidityPercent.toByte()).putInt(validUntilUtcSeconds.toInt()).array()
}

internal fun decodeDraftWeather(bytes: ByteArray): WatchWeather {
    require(bytes.size == WEATHER_PAYLOAD_BYTES)
    val buffer = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN)
    val iconId = buffer.get().toInt() and 0xff
    val icon = requireNotNull(WatchWeatherIcon.entries.find { it.wire == iconId })
    return WatchWeather(icon, buffer.short.toInt(), buffer.get().toInt() and 0xff,
        buffer.int.toLong() and 0xffffffffL).also { require(it.hasValidFields()) }
}
