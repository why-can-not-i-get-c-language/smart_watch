package com.team.smartwatch

import android.Manifest
import android.app.Application
import android.content.ActivityNotFoundException
import android.content.Context
import android.content.Intent
import android.location.Address
import android.location.Geocoder
import android.net.Uri
import android.os.Build
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import java.io.IOException
import java.net.URL
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import java.util.TimeZone
import javax.net.ssl.HttpsURLConnection
import kotlin.coroutines.resume
import kotlin.math.roundToInt
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.suspendCancellableCoroutine
import kotlinx.coroutines.withContext
import kotlinx.coroutines.withTimeoutOrNull
import org.json.JSONException
import org.json.JSONObject

private const val FORECAST_URL = "https://api.open-meteo.com/v1/forecast"
private const val SOURCE_URL = "https://open-meteo.com/"
private const val CACHE_VERSION = 2
private const val CITY_KEY = "located_city"
private const val FORECAST_KEY = "latest_forecast"

internal data class PhoneForecast(
    val date: String,
    val zone: String,
    val low: Double,
    val high: Double,
    val weatherCode: Int,
    val receivedAt: Long,
)

internal fun forecastDate(millis: Long, zone: String = "Asia/Shanghai"): String =
    SimpleDateFormat("yyyy-MM-dd", Locale.ROOT).apply {
        timeZone = TimeZone.getTimeZone(zone)
    }.format(Date(millis))

internal fun cityWeatherCoordinate(value: Double): Double = (value * 10).roundToInt() / 10.0

private fun readForecast(response: JSONObject, receivedAt: Long): PhoneForecast {
    val zone = response.getString("timezone")
    require(zone in TimeZone.getAvailableIDs())
    val daily = response.getJSONObject("daily")
    val date = daily.getJSONArray("time").getString(0)
    val low = daily.getJSONArray("temperature_2m_min").getDouble(0)
    val high = daily.getJSONArray("temperature_2m_max").getDouble(0)
    val code = daily.getJSONArray("weather_code").getInt(0)
    require(date == forecastDate(receivedAt, zone) && low.isFinite() && high.isFinite() && low <= high)
    return PhoneForecast(date, zone, low, high, code, receivedAt)
}

private fun fetchForecast(latitude: Double, longitude: Double): String {
    val url = URL(
        "$FORECAST_URL?latitude=$latitude&longitude=$longitude" +
            "&daily=weather_code,temperature_2m_min,temperature_2m_max&timezone=auto&forecast_days=1",
    )
    val connection = url.openConnection() as HttpsURLConnection
    connection.connectTimeout = 8_000
    connection.readTimeout = 8_000
    connection.instanceFollowRedirects = false
    try {
        if (connection.responseCode != 200) throw IOException("天气服务暂不可用")
        return connection.inputStream.bufferedReader(Charsets.UTF_8).use { reader ->
            val buffer = CharArray(1_024)
            buildString {
                while (true) {
                    val count = reader.read(buffer)
                    if (count == -1) break
                    if (length + count > 16_384) throw IOException("天气响应过大")
                    append(buffer, 0, count)
                }
            }
        }
    } finally {
        connection.disconnect()
    }
}

@Suppress("DEPRECATION")
private suspend fun cityName(context: Context, latitude: Double, longitude: Double): String? {
    if (!Geocoder.isPresent()) return null
    val geocoder = Geocoder(context, Locale.SIMPLIFIED_CHINESE)
    val matches = if (Build.VERSION.SDK_INT >= 33) {
        withTimeoutOrNull(7_000L) {
            suspendCancellableCoroutine<List<Address>> { result ->
                geocoder.getFromLocation(latitude, longitude, 3, object : Geocoder.GeocodeListener {
                    override fun onGeocode(addresses: MutableList<Address>) {
                        if (result.isActive) result.resume(addresses)
                    }

                    override fun onError(errorMessage: String?) {
                        if (result.isActive) result.resume(emptyList())
                    }
                })
            }
        }
    } else {
        withContext(Dispatchers.IO) { geocoder.getFromLocation(latitude, longitude, 3) }
    }
    return matches.orEmpty().firstNotNullOfOrNull { address ->
        sequenceOf(address.locality, address.subAdminArea, address.adminArea)
            .firstOrNull { !it.isNullOrBlank() }
    }
}

class PhoneWeatherViewModel(application: Application) : AndroidViewModel(application) {
    private val preferences = application.getSharedPreferences("phone_forecast", Context.MODE_PRIVATE).apply {
        if (getInt("cache_version", 0) != CACHE_VERSION) {
            edit().clear().putInt("cache_version", CACHE_VERSION).apply()
        }
    }
    internal var city by mutableStateOf(preferences.getString(CITY_KEY, null))
        private set
    internal var forecast by mutableStateOf(loadCache())
        private set
    internal var loading by mutableStateOf(false)
        private set
    internal var message by mutableStateOf<String?>(null)
        private set

    internal fun permissionDenied() {
        message = "请允许手机定位，以识别所在城市"
    }

    internal fun refresh() {
        if (loading) return
        val context = getApplication<Application>()
        loading = true
        message = "正在定位城市…"
        viewModelScope.launch {
            try {
                val point = readRecentPhoneLocation(context)
                if (point == null) {
                    message = "暂时无法获取当前位置，请检查系统定位"
                    return@launch
                }
                val latitude = cityWeatherCoordinate(point.latitude)
                val longitude = cityWeatherCoordinate(point.longitude)
                val located = cityName(context, latitude, longitude)
                if (located == null) {
                    message = "暂时无法识别所在城市，请稍后重试"
                    return@launch
                }
                if (located != city) {
                    city = located
                    forecast = null
                    preferences.edit().putString(CITY_KEY, located).remove(FORECAST_KEY).apply()
                }
                message = "正在查询预报…"
                val json = withContext(Dispatchers.IO) { fetchForecast(latitude, longitude) }
                val current = readForecast(JSONObject(json), System.currentTimeMillis())
                forecast = current
                val cache = JSONObject()
                    .put("date", current.date).put("zone", current.zone)
                    .put("low", current.low).put("high", current.high)
                    .put("code", current.weatherCode).put("received", current.receivedAt)
                preferences.edit().putString(FORECAST_KEY, cache.toString()).apply()
                message = null
            } catch (_: IOException) {
                message = "定位或刷新失败，请检查网络后重试"
            } catch (_: JSONException) {
                message = "天气数据读取失败，请稍后重试"
            } catch (_: IllegalArgumentException) {
                message = "天气日期或温度无效，请刷新"
            } catch (_: SecurityException) {
                permissionDenied()
            } finally {
                loading = false
            }
        }
    }

    private fun loadCache(): PhoneForecast? {
        val data = preferences.getString(FORECAST_KEY, null) ?: return null
        return runCatching {
            val cache = JSONObject(data)
            PhoneForecast(cache.getString("date"), cache.getString("zone"), cache.getDouble("low"),
                cache.getDouble("high"), cache.getInt("code"), cache.getLong("received"))
        }.getOrNull()
    }
}

@Composable
internal fun PhoneWeatherScreen(model: PhoneWeatherViewModel, now: Long, onBack: () -> Unit, modifier: Modifier) {
    val forecast = model.forecast
    val today = forecast?.takeIf { it.date == forecastDate(now, it.zone) }
    val context = LocalContext.current
    val permission = rememberLauncherForActivityResult(ActivityResultContracts.RequestPermission()) { granted ->
        if (granted) model.refresh() else model.permissionDenied()
    }
    LaunchedEffect(Unit) {
        if (hasSosLocationPermission(context)) model.refresh()
    }
    Column(
        modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(20.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        OutlinedButton(onClick = onBack) { Text("返回首页") }
        Text("手机天气", style = MaterialTheme.typography.headlineSmall)
        Text(model.city?.let { "最近定位城市 · $it" } ?: "通过手机定位识别所在城市")
        Button(enabled = !model.loading, onClick = {
            if (hasSosLocationPermission(context)) model.refresh()
            else permission.launch(Manifest.permission.ACCESS_COARSE_LOCATION)
        }) { Text("定位并刷新") }
        model.message?.let { Text(it, color = MaterialTheme.colorScheme.onSurfaceVariant) }
        if (today != null) {
            Text(weatherLabel(today.weatherCode), style = MaterialTheme.typography.titleLarge)
            Text("最低 ${today.low} °C · 最高 ${today.high} °C")
            Text("${today.date} · 最近获取 ${beijingTime(today.receivedAt)}（北京时间）",
                style = MaterialTheme.typography.bodySmall)
        } else if (forecast != null) {
            Text("旧预报已过期，请刷新")
        }
        Text("同步至手表待联调", color = MaterialTheme.colorScheme.onSurfaceVariant)
        TextButton(onClick = {
            try {
                context.startActivity(Intent(Intent.ACTION_VIEW, Uri.parse(SOURCE_URL)))
            } catch (_: ActivityNotFoundException) {
                // 数据来源仍在按钮上可见。
            }
        }) { Text("天气数据来自 Open-Meteo") }
    }
}

private fun weatherLabel(code: Int): String = when (code) {
    0 -> "晴"
    1, 2 -> "局部多云"
    3 -> "阴"
    45, 48 -> "雾"
    51, 53, 55, 56, 57 -> "毛毛雨"
    61, 63, 65, 66, 67, 80, 81, 82 -> "雨"
    71, 73, 75, 77, 85, 86 -> "雪"
    95, 96, 99 -> "雷雨"
    else -> "天气状况未知"
}
