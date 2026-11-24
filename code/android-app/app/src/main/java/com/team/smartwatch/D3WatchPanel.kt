package com.team.smartwatch

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.text.selection.SelectionContainer
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import java.util.TimeZone

@Composable
internal fun D3WatchPanel(watch: D3Watch) {
    Card(Modifier.fillMaxWidth()) {
        Column(Modifier.padding(14.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            Text("手表数据 · D3", style = MaterialTheme.typography.titleMedium)
            Text(watch.message)
            watch.identity?.let { identity ->
                SelectionContainer { Text("设备 UID ${identity.uid}") }
                Text("固件 ${identity.firmware}")
                val board = if (identity.hardware == 1) "STM32F411CEU6" else identity.hardware.toString()
                val revision = if (identity.revision == 0) "未知" else identity.revision.toString()
                Text("硬件 $board · 修订 $revision")
            }
            watch.status?.let { status ->
                Text("最近一次状态快照", style = MaterialTheme.typography.titleSmall)
                Text("手表时间 ${renderReading(status.time, ::formatD3Time)}")
                val stepsLabel = if (status.stepsScope == 0) "开机或清零后累计步数" else "今日步数（北京时间）"
                Text("$stepsLabel ${renderReading(status.steps) { "$it 步" }}")
                Text("电池电压 ${renderReading(status.voltage) { "$it mV" }}")
                Text("估算电量 ${renderReading(status.percentage) { "$it%" }}")
                Text("心率 ${renderReading(status.heart) { String.format(Locale.ROOT, "%.1f bpm", it / 10.0) }}")
                Text("估算血氧 ${renderReading(status.oxygen) { String.format(Locale.ROOT, "%.2f%%", it / 100.0) }}")
            }
            OutlinedButton(enabled = watch.canRefresh, onClick = watch::refresh) { Text("读取手表状态") }
            Button(enabled = watch.canSetTime, onClick = watch::calibrate) { Text("用手机当前时间校时") }
            watch.timeAcknowledged?.let { Text("设备应答的读回时间 ${formatD3Time(it)}") }
            watch.timeResult?.let { Text(it) }
        }
    }
}

private fun renderReading(reading: D3Reading, format: (Long) -> String): String {
    val value = reading.value ?: return reading.state.label
    val age = reading.ageMs?.let { " · 采样距快照 $it ms" }.orEmpty()
    return format(value) + age
}

private fun formatD3Time(seconds: Long) = SimpleDateFormat("yyyy-MM-dd HH:mm:ss", Locale.ROOT).apply {
    timeZone = TimeZone.getTimeZone("GMT+08:00")
}.format(Date(seconds * 1_000L)) + " UTC+8"
