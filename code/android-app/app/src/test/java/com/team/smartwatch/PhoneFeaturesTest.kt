package com.team.smartwatch

import java.io.StringWriter
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
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
}
