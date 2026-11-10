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
import kotlinx.coroutines.suspendCancellableCoroutine
import kotlinx.coroutines.withTimeoutOrNull

internal fun sosLocationPermissions() = arrayOf(
    Manifest.permission.ACCESS_FINE_LOCATION,
    Manifest.permission.ACCESS_COARSE_LOCATION,
)

internal fun hasSosLocationPermission(context: Context): Boolean =
    sosLocationPermissions().any { context.checkSelfPermission(it) == PackageManager.PERMISSION_GRANTED }

// At most one short request per SOS. Avoid using a stale cached position.
@SuppressLint("MissingPermission")
@Suppress("DEPRECATION")
internal suspend fun readSosLocation(context: Context): String? {
    if (!hasSosLocationPermission(context)) return null
    val manager = context.getSystemService(LocationManager::class.java) ?: return null
    val precise = context.checkSelfPermission(Manifest.permission.ACCESS_FINE_LOCATION) ==
        PackageManager.PERMISSION_GRANTED
    val candidates = buildList {
        if (precise) add(LocationManager.GPS_PROVIDER)
        add("fused")
        add(LocationManager.NETWORK_PROVIDER)
    }
    val location = try {
        val provider = candidates.firstOrNull { it in manager.allProviders && manager.isProviderEnabled(it) }
            ?: return null
        withTimeoutOrNull(6_000L) {
            suspendCancellableCoroutine<Location?> { continuation ->
                val cancellation = CancellationSignal()
                continuation.invokeOnCancellation { cancellation.cancel() }
                LocationManagerCompat.getCurrentLocation(manager, provider, cancellation, Executor { it.run() }) {
                    continuation.resume(it)
                }
            }
        }
    } catch (_: SecurityException) {
        null
    } catch (_: IllegalArgumentException) {
        null
    } ?: return null
    val ageMillis = SystemClock.elapsedRealtime() - location.elapsedRealtimeNanos / 1_000_000L
    if (ageMillis !in 0..15_000 || location.latitude !in -90.0..90.0 ||
        location.longitude !in -180.0..180.0 || !hasSosLocationPermission(context)) return null
    val point = String.format(Locale.US, "%.5f,%.5f", location.longitude, location.latitude)
    val accuracy = if (location.hasAccuracy()) "（误差约 ${location.accuracy.roundToInt()} 米）" else ""
    return "求助时手机定位$accuracy：https://uri.amap.com/marker?position=$point&coordinate=wgs84"
}
