package com.team.smartwatch

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.dp

internal enum class HealthTrendMetric(
    val title: String,
    val unit: String,
    val min: Int,
    val max: Int,
    val accent: Color,
    val samples: List<Int?>,
) {
    HEART_RATE(
        "心率", "bpm", 50, 110, Color(0xFFA32751),
        listOf(68, 74, 89, 82, null, 73, 66, 62, 58, null, 65, 78, 72),
    ),
    OXYGEN(
        "血氧估算", "%", 90, 100, Color(0xFF17598E),
        listOf(97, 98, 97, 96, null, 97, 96, 95, 96, null, 97, 98, 97),
    );

    fun format(value: Int): String = if (unit == "%") "$value%" else "$value $unit"
}

@Composable
internal fun HealthTrendScreen(
    metric: HealthTrendMetric,
    onBack: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val colors = MaterialTheme.colorScheme
    val typography = MaterialTheme.typography
    val latest = metric.samples.last()
    Column(
        modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(20.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        OutlinedButton(onClick = onBack) { Text("返回首页") }
        Text("${metric.title} · 24 小时记录", style = typography.headlineSmall)
        Text("模拟数据 · 11 月 5 日 09:30 至 11 月 6 日 09:30", color = colors.onSurfaceVariant)
        Card(
            Modifier.fillMaxWidth(),
            colors = CardDefaults.cardColors(containerColor = colors.surface),
        ) {
            Column(Modifier.padding(14.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                Text("最新示例记录", style = typography.labelLarge, color = colors.onSurfaceVariant)
                Text(latest?.let(metric::format) ?: "无记录", color = metric.accent, style = typography.headlineMedium)
                TrendChart(metric)
                Text("折线断开处表示无记录", style = typography.bodySmall, color = colors.onSurfaceVariant)
            }
        }
        Text("分时记录", style = typography.titleMedium)
        metric.samples.indices.reversed().forEach { index ->
            val hour = (9 + 2 * index) % 24
            val day = if (index < 8) "11 月 5 日" else "11 月 6 日"
            val time = "${hour.toString().padStart(2, '0')}:30"
            val value = metric.samples[index]?.let(metric::format) ?: "无记录"
            Row(Modifier.fillMaxWidth().padding(vertical = 3.dp), horizontalArrangement = Arrangement.SpaceBetween) {
                Text("$day $time", color = colors.onSurfaceVariant)
                Text(value, color = colors.onSurface)
            }
        }
    }
}

@Composable
private fun TrendChart(metric: HealthTrendMetric) {
    val colors = MaterialTheme.colorScheme
    val labelStyle = MaterialTheme.typography.labelSmall
    val gridColor = colors.outlineVariant
    val chartHeight = 154.dp
    Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
        Column(Modifier.height(chartHeight), verticalArrangement = Arrangement.SpaceBetween) {
            for (tick in listOf(metric.max, (metric.min + metric.max) / 2, metric.min)) {
                Text("$tick", style = labelStyle, color = colors.onSurfaceVariant)
            }
        }
        Column(Modifier.weight(1f)) {
            Canvas(Modifier.fillMaxWidth().height(chartHeight).semantics {
                contentDescription = "${metric.title} 24 小时折线图，单位 ${metric.unit}。缺测处断开，具体数值见下方分时记录。"
            }) {
                val inset = 4.dp.toPx()
                val width = size.width - 2 * inset
                val height = size.height - 2 * inset
                for (fraction in listOf(0f, 0.5f, 1f)) {
                    val y = inset + height * fraction
                    drawLine(gridColor, Offset(inset, y), Offset(size.width - inset, y), strokeWidth = 1.dp.toPx())
                }
                var previous: Offset? = null
                metric.samples.forEachIndexed { index, value ->
                    val point = value?.let {
                        val fraction = (it - metric.min).toFloat() / (metric.max - metric.min)
                        Offset(inset + width * index / metric.samples.lastIndex, inset + height * (1f - fraction))
                    }
                    if (point != null) {
                        previous?.let { drawLine(metric.accent, it, point, strokeWidth = 2.dp.toPx()) }
                        drawCircle(metric.accent, radius = 3.dp.toPx(), center = point)
                    }
                    previous = point
                }
            }
            Box(Modifier.fillMaxWidth()) {
                Text("09:30", Modifier.align(Alignment.CenterStart), style = labelStyle)
                Text("21:30", Modifier.align(Alignment.Center), style = labelStyle)
                Text("次日 09:30", Modifier.align(Alignment.CenterEnd), style = labelStyle)
            }
        }
    }
}
