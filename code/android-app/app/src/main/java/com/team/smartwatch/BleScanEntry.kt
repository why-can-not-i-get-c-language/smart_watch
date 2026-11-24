package com.team.smartwatch

internal data class BleScanEntry(
    val advertisedName: String?,
    val cachedName: String?,
    val timestampNanos: Long,
    val rssi: Int?,
    val advertisedServices: Set<String>,
    val connectable: Boolean?,
) {
    val name: String get() = advertisedName ?: cachedName ?: "未命名设备"

    fun eligibleForAutoConnect(isFavorite: Boolean): Boolean = isFavorite && connectable != false

    fun update(observation: BleScanEntry): BleScanEntry {
        val latest = if (observation.timestampNanos >= timestampNanos) observation else this
        val older = if (latest === this) observation else this
        return latest.copy(
            advertisedName = latest.advertisedName ?: older.advertisedName,
            cachedName = latest.cachedName ?: older.cachedName,
            advertisedServices = advertisedServices + observation.advertisedServices,
        )
    }

    companion object {
        fun fromAdvertisement(
            advertisedName: String?,
            cachedName: String?,
            timestampNanos: Long,
            rssi: Int,
            advertisedServices: Set<String>,
            connectable: Boolean?,
        ) = BleScanEntry(
            advertisedName?.trim()?.takeIf(String::isNotEmpty),
            cachedName?.trim()?.takeIf(String::isNotEmpty),
            timestampNanos,
            rssi.takeIf { it in -127..126 },
            advertisedServices,
            connectable,
        )
    }
}
