package com.team.smartwatch

import java.io.StringWriter
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class PhoneFeaturesTest {
    @Test
    fun demoHistoryPagesExportZeroAndMissingReadingsSeparately() = runBlocking {
        val source = demoHistory(Reading.Valid(DailySteps(4260, SimulatedWatch.DEMO_DATE), "09:30"))
        val repository = WatchRepository(OfflineWatchSession(source))
        val query = HistoryQuery(HistoryKind.STEPS, limit = 5)
        val first = repository.history(query)
        val second = repository.history(query.copy(cursor = checkNotNull(first.nextCursor)))
        val records = first.records + second.records
        assertEquals(7, records.map { it.id }.distinct().size)
        assertNull(second.nextCursor)
        val csv = StringWriter().also { exportHistoryCsv(records, it) }.toString()
        assertTrue(csv.contains("\"0\",\"steps\",\"valid\",\"valid\""))
        assertTrue(csv.contains("\"\",\"steps\",\"valid\",\"missing\""))
        assertEquals(13, source.count { it.kind == HistoryKind.HEART_RATE })
        assertEquals(13, source.count { it.kind == HistoryKind.OXYGEN_ESTIMATE })
    }

    @Test
    fun forecastDateChangesAtShanghaiMidnight() {
        val midnightBeijing = 1795795200000L
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
}
