package com.team.smartwatch

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class BleScanEntryTest {
    @Test
    fun laterBroadcastSuppliesNameAndEmptyBroadcastDoesNotEraseIt() {
        val first = observe(100, advertisedName = " ", rssi = -65)
        assertEquals("未命名设备", first.name)

        val named = first.update(observe(200, advertisedName = " SMARTWATCH ", rssi = -57))
        assertEquals("SMARTWATCH", named.name)
        assertEquals(200L, named.timestampNanos)
        assertEquals(-57, named.rssi)

        val last = named.update(observe(300, cachedName = "旧缓存", rssi = 127))
        assertEquals("SMARTWATCH", last.name)
        assertEquals(300L, last.timestampNanos)
        assertNull(last.rssi)
    }

    @Test
    fun cachedNameIsFallbackAndBroadcastServicesAreOptional() {
        val first = observe(100, cachedName = "系统缓存")
        assertEquals("系统缓存", first.name)
        assertEquals(emptySet<String>(), first.advertisedServices)
        assertEquals(false, first.connectable)
        val named = first.update(observe(200, advertisedName = "Watch", services = setOf("service-a")))
        assertEquals("Watch", named.name)
        assertEquals(setOf("service-a"), named.update(observe(300)).advertisedServices)
    }

    @Test
    fun outOfOrderCallbackDoesNotReplaceNewestSignalOrTime() {
        val last = observe(300, advertisedName = "new", rssi = -48)
            .update(observe(200, advertisedName = "old", rssi = -85))
        assertEquals("new", last.name)
        assertEquals(300L, last.timestampNanos)
        assertEquals(-48, last.rssi)
    }

    private fun observe(
        timestamp: Long,
        advertisedName: String? = null,
        cachedName: String? = null,
        rssi: Int = -70,
        services: Set<String> = emptySet(),
    ) = BleScanEntry.fromAdvertisement(advertisedName, cachedName, timestamp, rssi, services, false)
}
