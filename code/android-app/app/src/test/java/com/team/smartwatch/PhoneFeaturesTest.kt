package com.team.smartwatch

import java.io.StringWriter
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Assert.assertThrows
import org.junit.Test

class PhoneFeaturesTest {
    @Test
    fun demoHistoryPagesExportZeroAndMissingReadingsSeparately() = runBlocking {
        val source = demoHistory(Reading.Valid(DailySteps(4260, SimulatedWatch.DEMO_DATE), "09:30"))
        val repository = WatchRepository(OfflineWatchSession(source))
        val query = HistoryQuery(HistoryKind.DAILY_PARTIAL_STEPS, limit = 5)
        val first = repository.history(query)
        val second = repository.history(query.copy(cursor = checkNotNull(first.nextCursor)))
        val records = first.records + second.records
        val daily = latestDailyStepRecords(records.reversed())
        assertEquals(8, records.map { it.id }.distinct().size)
        assertEquals(7, daily.size)
        assertEquals(4260, (daily.first().value as Reading.Valid).value)
        assertFalse(daily.any { it.id == "99" })
        assertNull(second.nextCursor)
        val csv = StringWriter().also { exportHistoryCsv(daily, it) }.toString()
        assertTrue(csv.contains("DAILY_PARTIAL_STEPS"))
        assertTrue(csv.contains("\"0\",\"steps\",\"valid\",\"valid\""))
        assertTrue(csv.contains("\"\",\"steps\",\"valid\",\"missing\""))
        assertEquals(13, source.count { it.kind == HistoryKind.HEART_RATE })
        assertEquals(13, source.count { it.kind == HistoryKind.OXYGEN_ESTIMATE })
    }

    @Test
    fun reminderSettingsUseFirmwareValuesAndKeepOtherFields() = runBlocking {
        val device = OfflineWatchSession()
        val repository = WatchRepository(device)
        assertEquals(300, repository.saveReminderDelay(300))
        val low = repository.saveLowActivity(false, 5)
        assertEquals(300, low.reminderDelaySeconds)
        assertFalse(low.lowActivityEnabled)
        assertEquals(120, repository.saveLowActivity(true, 120).lowActivityMinutes)
        assertEquals(WatchError.INVALID_DATA,
            runCatching { repository.saveReminderDelay(31) }.exceptionOrNull().let { (it as WatchRequestException).reason })
        assertEquals(WatchError.INVALID_DATA,
            runCatching { repository.saveLowActivity(true, 4) }.exceptionOrNull().let { (it as WatchRequestException).reason })
        assertEquals(300, repository.getSettings().reminderDelaySeconds)
        assertEquals(120, repository.getSettings().lowActivityMinutes)
        device.mismatchSettings = true
        assertEquals(WatchError.READBACK_MISMATCH,
            runCatching { repository.saveReminderDelay(30) }.exceptionOrNull().let { (it as WatchRequestException).reason })
    }

    @Test
    fun forecastDateChangesAtShanghaiMidnight() {
        val midnightBeijing = 1795795200000L
        assertEquals(beijingDay(midnightBeijing / 1000) - 1, beijingDay(midnightBeijing / 1000 - 1))
        assertEquals("2026-11-27", forecastDate(midnightBeijing - 1))
        assertEquals("2026-11-28", forecastDate(midnightBeijing))
    }

    @Test
    fun weatherUsesCityPrecisionAndLocatedTimeZone() {
        assertEquals(30.3, cityWeatherCoordinate(30.2741), 0.00001)
        assertEquals(-118.2, cityWeatherCoordinate(-118.2437), 0.00001)
        val utcMorning = 1795748400000L
        assertEquals("2026-11-26", forecastDate(utcMorning, "America/Los_Angeles"))
        assertEquals("2026-11-27", forecastDate(utcMorning, "Asia/Shanghai"))
    }

    @Test
    fun currentWeatherAcceptsNegativeTemperatureAndZeroHumidity() {
        val observedAt = weatherObservationTime("2026-11-27T00:00", "Asia/Shanghai")
        assertEquals(1795708800000L, observedAt)
        val current = checkNotNull(currentWeather(2, -3.2, 0.0, observedAt))
        assertEquals(-3.2, current.temperature, 0.0)
        assertEquals(0, current.humidityPercent)
        assertTrue(current.isFresh(observedAt + 3_599_999L))
        assertFalse(current.isFresh(observedAt + 3_600_000L))
        assertFalse(current.isFresh(observedAt - 1L))
    }

    @Test
    fun invalidCurrentWeatherCannotBeDisplayedAsAReading() {
        assertNull(currentWeather(2, 20.0, 101.0, 1L))
        assertNull(currentWeather(2, 20.0, Double.NaN, 1L))
        assertNull(currentWeather(2, Double.NaN, 68.0, 1L))
        assertNull(currentWeather(-1, 20.0, 68.0, 1L))
        assertEquals(0L, weatherObservationTime("2026-11-31T07:15", "Asia/Shanghai"))
        assertEquals(0L, weatherObservationTime("2026-11-27T07:15x", "Asia/Shanghai"))
    }

    @Test
    fun weatherLocationShowsCityAndDistrictWhenAvailable() {
        assertEquals(WeatherArea("杭州市", "西湖区"), resolveWeatherArea("杭州市", "西湖区", null, "浙江省"))
        assertEquals(WeatherArea("北京市", "朝阳区"), resolveWeatherArea("朝阳区", null, "市辖区", "北京市"))
        assertEquals(WeatherArea("杭州市", "淳安县"), resolveWeatherArea(null, "淳安县", "杭州市", "浙江省"))
    }

    @Test
    fun weatherLocationOmitsStreetNamesAndUnavailableDistricts() {
        assertEquals(WeatherArea("杭州市", null), resolveWeatherArea(" 杭州市 ", "翠苑街道", "市辖区", "浙江省"))
        assertEquals(WeatherArea("香港特别行政区", null), resolveWeatherArea(null, null, null, "香港特别行政区"))
        assertNull(resolveWeatherArea(null, null, "市辖区", "浙江省"))
    }

    @Test
    fun weatherDraftMatchesDocumentedBytesAndExpires() {
        val observedAt = 1_795_754_700_000L
        val reading = CurrentWeather(2, -3.2, 68, observedAt)
        val weather = checkNotNull(reading.toWatchWeather(observedAt))
        val fixture = byteArrayOf(0x02, 0xe0.toByte(), 0xff.toByte(), 0x44, 0xdc.toByte(), 0x18, 0x09, 0x6b)
        assertEquals(WatchWeather(WatchWeatherIcon.CLOUDY, -32, 68, 1_795_758_300L), weather)
        assertArrayEquals(fixture, weather.encodeDraftWeather())
        assertEquals(weather, decodeDraftWeather(fixture))
        assertNull(reading.toWatchWeather(observedAt + CURRENT_WEATHER_MAX_AGE_MS))
        val beyond2038 = weather.copy(validUntilUtcSeconds = D3_MAX_UTC)
        assertEquals(beyond2038, decodeDraftWeather(beyond2038.encodeDraftWeather()))
    }

    @Test
    fun weatherDraftRejectsBadLengthAndFields() {
        val valid = WatchWeather(WatchWeatherIcon.CLEAR, 200, 100, 1_795_758_300L).encodeDraftWeather()
        assertThrows(IllegalArgumentException::class.java) { decodeDraftWeather(valid.copyOf(7)) }
        assertThrows(IllegalArgumentException::class.java) {
            decodeDraftWeather(valid.copyOf().apply { this[0] = 8 })
        }
        assertThrows(IllegalArgumentException::class.java) {
            decodeDraftWeather(valid.copyOf().apply { this[3] = 101 })
        }
    }

    @Test
    fun weatherRepositoryRejectsMismatchedReceipt() = runBlocking {
        val otherWatch = object : WatchSession by OfflineWatchSession() {
            override suspend fun syncWeather(weather: WatchWeather) = weather.copy(humidityPercent = 1)
        }
        val weather = WatchWeather(WatchWeatherIcon.CLOUDY, -32, 68, 1_795_758_300L)
        val result = runCatching { WatchRepository(otherWatch).sendWeather(weather, 1_795_754_700L) }
        assertEquals(WatchError.READBACK_MISMATCH, (result.exceptionOrNull() as? WatchRequestException)?.reason)
    }

    @Test
    fun reminderChangesPreserveTheOtherSettingAndRequireReadback() = runBlocking {
        val device = OfflineWatchSession()
        val repository = WatchRepository(device)
        val before = repository.getSettings()
        val alarm = repository.saveAlarm(false, 6, 45)
        assertEquals(before.copy(alarmEnabled = false, alarmHour = 6, alarmMinute = 45), alarm)
        val lowActivity = repository.saveLowActivity(false, 90)
        assertEquals(alarm.copy(lowActivityEnabled = false, lowActivityMinutes = 90), lowActivity)
        assertEquals(lowActivity, repository.getSettings())
        device.mismatchSettings = true
        val result = runCatching { repository.saveLowActivity(true, 30) }
        assertEquals(WatchError.READBACK_MISMATCH, (result.exceptionOrNull() as? WatchRequestException)?.reason)
        val invalid = runCatching { repository.saveAlarm(true, 25, 0) }
        assertEquals(WatchError.INVALID_DATA, (invalid.exceptionOrNull() as? WatchRequestException)?.reason)
    }

    @Test
    fun historyRejectsWrongKindsOversizePagesAndRepeatedCursors() = runBlocking {
        val record = WatchRecord("step-1", HistoryKind.STEPS, Reading.Missing, Reading.Valid(0, ""))
        val request = HistoryQuery(HistoryKind.STEPS, limit = 1, cursor = "prior")
        val malformed = listOf(
            HistoryPage(listOf(record.copy(kind = HistoryKind.HEART_RATE)), null),
            HistoryPage(listOf(record, record.copy(id = "step-2")), null),
            HistoryPage(listOf(record), request.cursor),
        )
        for (page in malformed) {
            val watch = object : WatchSession by OfflineWatchSession() {
                override suspend fun getHistory(query: HistoryQuery) = page
            }
            val result = runCatching { WatchRepository(watch).history(request) }
            assertEquals(WatchError.INVALID_DATA, (result.exceptionOrNull() as? WatchRequestException)?.reason)
        }
    }
}
