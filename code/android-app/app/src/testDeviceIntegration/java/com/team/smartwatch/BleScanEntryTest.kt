package com.team.smartwatch

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
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

    @Test
    fun autoConnectRequiresFavoriteAddressAndDoesNotRequireAnAdvertisedNameOrService() {
        val unnamed = observe(100).copy(connectable = true)
        assertTrue(unnamed.eligibleForAutoConnect(isFavorite = true))
        assertFalse(unnamed.copy(advertisedName = "SMARTWATCH").eligibleForAutoConnect(isFavorite = false))
        assertFalse(unnamed.copy(connectable = false).eligibleForAutoConnect(isFavorite = true))
        assertTrue(unnamed.copy(connectable = null).eligibleForAutoConnect(isFavorite = true))
    }

    @Test
    fun directConnectChoosesLastConnectedFavoriteOrTheOnlySavedAddress() {
        val first = "02:00:00:00:00:01"
        val second = "02:00:00:00:00:02"
        val removed = "02:00:00:00:00:03"
        assertNull(preferredFavoriteAddress(emptySet(), first))
        assertEquals(first, preferredFavoriteAddress(setOf(first), removed))
        assertEquals(second, preferredFavoriteAddress(setOf(first, second), second))
        assertNull(preferredFavoriteAddress(setOf(first, second), removed))
        assertNull(preferredFavoriteAddress(setOf(first, second), null))
    }

    private fun observe(
        timestamp: Long,
        advertisedName: String? = null,
        cachedName: String? = null,
        rssi: Int = -70,
        services: Set<String> = emptySet(),
    ) = BleScanEntry.fromAdvertisement(advertisedName, cachedName, timestamp, rssi, services, false)
}
