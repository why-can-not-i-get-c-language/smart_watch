package com.team.smartwatch

import android.Manifest
import android.annotation.SuppressLint
import android.content.Context
import android.content.pm.PackageManager
import android.location.Location
import android.location.LocationManager
import android.os.SystemClock
import androidx.core.location.LocationManagerCompat
import androidx.core.os.CancellationSignal
import java.util.Locale
import java.util.concurrent.Executor
import kotlin.coroutines.resume
import kotlin.math.roundToInt
import kotlinx.coroutines.flow.channelFlow
import kotlinx.coroutines.flow.firstOrNull
import kotlinx.coroutines.launch
import kotlinx.coroutines.suspendCancellableCoroutine
import kotlinx.coroutines.withTimeoutOrNull

internal fun sosLocationPermissions() = arrayOf(
    Manifest.permission.ACCESS_FINE_LOCATION,
    Manifest.permission.ACCESS_COARSE_LOCATION,
)

internal fun hasSosLocationPermission(context: Context): Boolean =
    sosLocationPermissions().any { context.checkSelfPermission(it) == PackageManager.PERMISSION_GRANTED }

internal fun isPhoneLocationEnabled(context: Context): Boolean =
    context.getSystemService(LocationManager::class.java)?.let(LocationManagerCompat::isLocationEnabled) == true

private fun hasPreciseLocationPermission(context: Context): Boolean =
    context.checkSelfPermission(Manifest.permission.ACCESS_FINE_LOCATION) == PackageManager.PERMISSION_GRANTED

@SuppressLint("MissingPermission")
@Suppress("DEPRECATION")
private suspend fun requestPosition(manager: LocationManager, provider: String): Location? =
    suspendCancellableCoroutine { continuation ->
        val cancellation = CancellationSignal()
        continuation.invokeOnCancellation { cancellation.cancel() }
        LocationManagerCompat.getCurrentLocation(manager, provider, cancellation, Executor { it.run() }) {
            if (continuation.isActive) continuation.resume(it)
        }
    }

private fun Location.isUsablePosition(maxAgeMillis: Long): Boolean {
    val ageMillis = SystemClock.elapsedRealtime() - elapsedRealtimeNanos / 1_000_000L
    return ageMillis in 0..maxAgeMillis && latitude in -90.0..90.0 && longitude in -180.0..180.0
}

// City weather can use other enabled providers while waiting for GPS indoors.
@SuppressLint("MissingPermission")
internal suspend fun readWeatherPhoneLocation(context: Context): Location? {
    if (!hasSosLocationPermission(context)) return null
    val manager = context.getSystemService(LocationManager::class.java) ?: return null
    if (!LocationManagerCompat.isLocationEnabled(manager)) return null
    val providers = buildList {
        add("fused")
        add(LocationManager.NETWORK_PROVIDER)
        if (hasPreciseLocationPermission(context)) add(LocationManager.GPS_PROVIDER)
    }.filter { it in manager.allProviders && manager.isProviderEnabled(it) }

    val live = withTimeoutOrNull(10_000L) {
        channelFlow {
            providers.forEach { provider ->
                launch {
                    val point = try {
                        requestPosition(manager, provider)
                    } catch (_: SecurityException) {
                        null
                    } catch (_: IllegalArgumentException) {
                        null
                    }
                    if (point?.isUsablePosition(15_000L) == true) send(point)
                }
            }
        }.firstOrNull()
    }
    if (!hasSosLocationPermission(context) || !LocationManagerCompat.isLocationEnabled(manager)) return null
    return live ?: providers.mapNotNull { provider ->
        try {
            manager.getLastKnownLocation(provider)?.takeIf { it.isUsablePosition(120_000L) }
        } catch (_: SecurityException) {
            null
        } catch (_: IllegalArgumentException) {
            null
        }
    }.maxByOrNull { it.elapsedRealtimeNanos }
}

// At most one short request per SOS. Avoid using a stale cached position.
internal suspend fun readRecentPhoneLocation(context: Context): Location? {
    if (!hasSosLocationPermission(context)) return null
    val manager = context.getSystemService(LocationManager::class.java) ?: return null
    val candidates = buildList {
        if (hasPreciseLocationPermission(context)) add(LocationManager.GPS_PROVIDER)
        add("fused")
        add(LocationManager.NETWORK_PROVIDER)
    }
    val location = try {
        val provider = candidates.firstOrNull { it in manager.allProviders && manager.isProviderEnabled(it) }
            ?: return null
        withTimeoutOrNull(6_000L) { requestPosition(manager, provider) }
    } catch (_: SecurityException) {
        null
    } catch (_: IllegalArgumentException) {
        null
    } ?: return null
    if (!location.isUsablePosition(15_000L) || !hasSosLocationPermission(context)) return null
    return location
}

internal suspend fun readSosLocation(context: Context): String? {
    val location = readRecentPhoneLocation(context) ?: return null
    val point = String.format(Locale.US, "%.5f,%.5f", location.longitude, location.latitude)
    val accuracy = if (location.hasAccuracy()) "（误差约 ${location.accuracy.roundToInt()} 米）" else ""
    return "求助时手机定位$accuracy：https://uri.amap.com/marker?position=$point&coordinate=wgs84"
}
