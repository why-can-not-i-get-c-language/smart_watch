package com.team.smartwatch

import android.app.Application
import android.content.ActivityNotFoundException
import android.content.Context
import android.content.Intent
import android.net.Uri
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.FilterChip
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
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
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.json.JSONException
import org.json.JSONObject

private const val FORECAST_URL = "https://api.open-meteo.com/v1/forecast"
private const val SOURCE_URL = "https://open-meteo.com/"
private const val CHINA_ZONE = "Asia/Shanghai"

internal enum class ForecastCity(val label: String, val latitude: Double, val longitude: Double) {
    HANGZHOU("杭州", 30.27, 120.15),
    BEIJING("北京", 39.90, 116.41),
    SHANGHAI("上海", 31.23, 121.47),
    GUANGZHOU("广州", 23.13, 113.26),
    SHENZHEN("深圳", 22.54, 114.06),
    CHENGDU("成都", 30.57, 104.07),
}

internal data class PhoneForecast(
    val date: String,
    val low: Double,
    val high: Double,
    val weatherCode: Int,
    val receivedAt: Long,
)

internal fun forecastDate(millis: Long): String =
    SimpleDateFormat("yyyy-MM-dd", Locale.ROOT).apply {
        timeZone = TimeZone.getTimeZone(CHINA_ZONE)
    }.format(Date(millis))

private fun readForecast(response: JSONObject, receivedAt: Long): PhoneForecast {
    require(response.getString("timezone") == CHINA_ZONE)
    val daily = response.getJSONObject("daily")
    val date = daily.getJSONArray("time").getString(0)
    val low = daily.getJSONArray("temperature_2m_min").getDouble(0)
    val high = daily.getJSONArray("temperature_2m_max").getDouble(0)
    val code = daily.getJSONArray("weather_code").getInt(0)
    require(date == forecastDate(receivedAt) && low.isFinite() && high.isFinite() && low <= high)
    return PhoneForecast(date, low, high, code, receivedAt)
}

private fun fetchForecast(city: ForecastCity): String {
    val url = URL(
        "$FORECAST_URL?latitude=${city.latitude}&longitude=${city.longitude}" +
            "&daily=weather_code,temperature_2m_min,temperature_2m_max&timezone=Asia%2FShanghai&forecast_days=1",
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

class PhoneWeatherViewModel(application: Application) : AndroidViewModel(application) {
    private val preferences = application.getSharedPreferences("phone_forecast", Context.MODE_PRIVATE)
    internal var city by mutableStateOf(ForecastCity.entries.firstOrNull {
        it.name == preferences.getString("city", null)
    } ?: ForecastCity.HANGZHOU)
        private set
    internal var forecast by mutableStateOf(loadCache(city))
        private set
    internal var loading by mutableStateOf(false)
        private set
    internal var failed by mutableStateOf(false)
        private set

    internal fun choose(next: ForecastCity) {
        if (loading || next == city) return
        city = next
        forecast = loadCache(next)
        failed = false
        preferences.edit().putString("city", next.name).apply()
    }

    internal fun refresh() {
        if (loading) return
        val chosen = city
        loading = true
        failed = false
        viewModelScope.launch {
            try {
                val json = withContext(Dispatchers.IO) { fetchForecast(chosen) }
                val now = System.currentTimeMillis()
                forecast = readForecast(JSONObject(json), now)
                preferences.edit().putString(chosen.name, json).putLong("${chosen.name}_received", now).apply()
            } catch (_: IOException) {
                failed = true
            } catch (_: JSONException) {
                failed = true
            } catch (_: IllegalArgumentException) {
                failed = true
            } finally {
                loading = false
            }
        }
    }

    private fun loadCache(target: ForecastCity): PhoneForecast? {
        val json = preferences.getString(target.name, null) ?: return null
        return runCatching {
            readForecast(JSONObject(json), preferences.getLong("${target.name}_received", 0))
        }.getOrNull()
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
internal fun PhoneWeatherScreen(model: PhoneWeatherViewModel, now: Long, onBack: () -> Unit, modifier: Modifier) {
    val forecast = model.forecast
    val current = forecast?.takeIf { it.date == forecastDate(now) }
    val context = LocalContext.current
    Column(
        modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(20.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        OutlinedButton(onClick = onBack) { Text("返回首页") }
        Text("手机天气", style = MaterialTheme.typography.headlineSmall)
        FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            ForecastCity.entries.forEach { city ->
                FilterChip(selected = model.city == city, enabled = !model.loading,
                    onClick = { model.choose(city) }, label = { Text(city.label) })
            }
        }
        Button(onClick = model::refresh, enabled = !model.loading) { Text("刷新预报") }
        when {
            model.loading -> Text("正在查询…")
            model.failed && current != null -> Text("刷新失败 · 显示当天缓存")
            model.failed -> Text("刷新失败，请检查网络后重试")
            forecast != null && current == null -> Text("旧预报已过期，请刷新")
            current == null -> Text("选择城市后刷新当日预报")
        }
        if (current != null) {
            Text("${model.city.label} · ${weatherLabel(current.weatherCode)}", style = MaterialTheme.typography.titleLarge)
            Text("最低 ${current.low} °C · 最高 ${current.high} °C")
            Text("${current.date} · 最近获取 ${beijingTime(current.receivedAt)}", style = MaterialTheme.typography.bodySmall)
        }
        Text("同步至手表待联调", color = MaterialTheme.colorScheme.onSurfaceVariant)
        TextButton(onClick = {
            try {
                context.startActivity(Intent(Intent.ACTION_VIEW, Uri.parse(SOURCE_URL)))
            } catch (_: ActivityNotFoundException) {
                // 数据来源始终保留在按钮上。
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
