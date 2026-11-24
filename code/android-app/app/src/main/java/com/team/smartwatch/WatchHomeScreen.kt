package com.team.smartwatch

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.IntrinsicSize
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import java.text.NumberFormat
import java.util.Locale
import kotlin.math.roundToInt

internal val WatchOverviewColors = darkColorScheme(
    primary = Color(0xFFB8F46E),
    onPrimary = Color(0xFF213708),
    primaryContainer = Color(0xFF243420),
    onPrimaryContainer = Color(0xFFF3F6FA),
    secondary = Color(0xFFA6D9FF),
    tertiary = Color(0xFFFFC278),
    background = Color(0xFF0E1319),
    onBackground = Color(0xFFF3F6FA),
    surface = Color(0xFF19232D),
    surfaceContainer = Color(0xFF19232D),
    surfaceContainerHighest = Color(0xFF22303D),
    onSurface = Color(0xFFF3F6FA),
    onSurfaceVariant = Color(0xFFAEBCCB),
    outline = Color(0xFF94A5B7),
    outlineVariant = Color(0xFF344454),
)

@OptIn(ExperimentalLayoutApi::class)
@Composable
internal fun WatchHomeScreen(
    state: WatchHomeState,
    scenarios: List<HomeScenario>,
    selectedScenario: Int,
    onScenario: (Int) -> Unit,
    onConnection: () -> Unit,
    onBleFavorites: () -> Unit,
    onSimulatedConnection: () -> Unit,
    liveWatchStatus: @Composable () -> Unit,
    onTimeSync: () -> Unit,
    onReminder: () -> Unit,
    onLowActivity: () -> Unit,
    lowActivityReminder: Boolean,
    onFallSos: () -> Unit,
    connectionRevision: Int,
    onTrend: (HealthTrendMetric) -> Unit,
    modifier: Modifier = Modifier,
) {
    MaterialTheme(colorScheme = WatchOverviewColors) {
        val colors = MaterialTheme.colorScheme
        Surface(modifier.fillMaxSize(), color = colors.background, contentColor = colors.onBackground) {
            Column(
                Modifier.verticalScroll(rememberScrollState()).padding(16.dp),
                verticalArrangement = Arrangement.spacedBy(12.dp),
            ) {
                Text("智能手表", style = MaterialTheme.typography.headlineMedium, fontWeight = FontWeight.Bold)
                FlowRow(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                    OutlinedButton(onClick = onConnection) { Text("设备连接") }
                    OutlinedButton(onClick = onBleFavorites) { Text("蓝牙收藏") }
                }
                liveWatchStatus()
                Row(
                    Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically,
                ) {
                    Text(state.source, color = colors.onSurfaceVariant, style = MaterialTheme.typography.labelLarge)
                    TextButton(onClick = onSimulatedConnection) { Text("模拟连接") }
                }
                ConnectionSummary(state)
                StepOverview(state.steps)
                Row(Modifier.height(IntrinsicSize.Min), horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                    HealthReadingCard(
                        HealthTrendMetric.HEART_RATE, state.heartRate, Color(0xFFFFA9BC),
                        Modifier.weight(1f).fillMaxHeight(),
                        onClick = { onTrend(HealthTrendMetric.HEART_RATE) },
                    )
                    HealthReadingCard(
                        HealthTrendMetric.OXYGEN, state.oxygen, colors.secondary,
                        Modifier.weight(1f).fillMaxHeight(),
                        onClick = { onTrend(HealthTrendMetric.OXYGEN) },
                    )
                }
                DeviceOverview(state.battery, state.watchTime)
                if (lowActivityReminder) LowActivityAlert()
                FlowRow(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                    OutlinedButton(onClick = onTimeSync) { Text("时间校准") }
                    OutlinedButton(onClick = onReminder) { Text("手表闹钟") }
                    OutlinedButton(onClick = onLowActivity) { Text("低运动提醒") }
                    OutlinedButton(onClick = onFallSos) { Text("跌倒 SOS") }
                }
                ScenarioSelector(scenarios, selectedScenario, onScenario)
                StepHistoryCard(state.steps)
                WeatherCard(state.connection, connectionRevision)
            }
        }
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun ConnectionSummary(state: WatchHomeState) {
    val colors = MaterialTheme.colorScheme
    val statusColor = when (state.connection) {
        Connection.SYNCED -> colors.primary
        Connection.SYNCING, Connection.CONNECTING -> colors.secondary
        else -> colors.tertiary
    }
    FlowRow(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(12.dp)) {
        Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(7.dp)) {
            Box(Modifier.size(8.dp).background(statusColor, CircleShape))
            Text(state.connection.label, color = statusColor, fontWeight = FontWeight.SemiBold)
        }
        Text(
            state.lastSync?.let { "最近同步 $it" } ?: "尚未同步",
            color = colors.onSurfaceVariant,
            style = MaterialTheme.typography.bodyMedium,
        )
    }
}

@Composable
private fun StepOverview(reading: Reading<DailySteps>) {
    val colors = MaterialTheme.colorScheme
    val goal = SimulatedWatch.DEMO_STEP_GOAL
    val formatter = NumberFormat.getIntegerInstance(Locale.CHINA)
    val sample = reading as? Reading.Valid
    val fraction = sample?.let { it.value.count.toFloat() / goal }
    Card(
        colors = CardDefaults.cardColors(containerColor = colors.primaryContainer),
        modifier = Modifier.fillMaxWidth(),
    ) {
        Row(
            Modifier.padding(14.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            Column(Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(4.dp)) {
                Text("今日步数", color = colors.onSurfaceVariant, style = MaterialTheme.typography.labelLarge)
                if (sample != null && fraction != null) {
                    Text(
                        "${formatter.format(sample.value.count)} 步",
                        color = colors.primary,
                        style = MaterialTheme.typography.displaySmall,
                        fontWeight = FontWeight.Bold,
                    )
                    Text(
                        "达成 ${(fraction * 100).roundToInt()}% · 演示目标 ${formatter.format(goal)} 步",
                        color = colors.onSurfaceVariant,
                        style = MaterialTheme.typography.bodySmall,
                    )
                    Text(
                        "${sample.value.date} · 手机接收于 ${sample.receivedAt}",
                        color = colors.onSurfaceVariant,
                        style = MaterialTheme.typography.bodySmall,
                    )
                } else {
                    ReadingValue(reading) { formatter.format(it.count) }
                }
            }
            if (fraction != null) {
                CircularProgressIndicator(
                    progress = { fraction.coerceIn(0f, 1f) },
                    modifier = Modifier.size(64.dp).semantics { contentDescription = "今日步数达成比例" },
                    color = colors.primary,
                    trackColor = colors.outlineVariant,
                    strokeWidth = 7.dp,
                )
            }
        }
    }
}

@Composable
private fun HealthReadingCard(
    metric: HealthTrendMetric,
    reading: Reading<Int>,
    accent: Color,
    modifier: Modifier,
    onClick: () -> Unit,
) {
    val title = metric.title
    Card(
        modifier.clickable(role = Role.Button, onClickLabel = "查看${title}24 小时记录", onClick = onClick),
        colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceContainer),
    ) {
        Column(Modifier.padding(12.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
            Text(title, color = MaterialTheme.colorScheme.onSurfaceVariant, style = MaterialTheme.typography.labelLarge)
            ReadingValue(reading, accent, metric::format)
            Text("查看 24 小时记录 ›", color = accent, style = MaterialTheme.typography.labelMedium)
        }
    }
}

@Composable
private fun DeviceOverview(battery: Reading<Int>, watchTime: Reading<String>) {
    val colors = MaterialTheme.colorScheme
    Card(
        Modifier.fillMaxWidth(),
        colors = CardDefaults.cardColors(containerColor = colors.surfaceContainer),
    ) {
        Column(Modifier.padding(14.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            Text("电量估算", color = colors.onSurfaceVariant, style = MaterialTheme.typography.labelLarge)
            ReadingValue(battery, colors.primary) { "$it%" }
            if (battery is Reading.Valid) {
                LinearProgressIndicator(
                    progress = { battery.value.coerceIn(0, 100) / 100f },
                    modifier = Modifier.fillMaxWidth().semantics { contentDescription = "电量估算" },
                    trackColor = colors.outlineVariant,
                )
            }
            Text("手表时间", color = colors.onSurfaceVariant, style = MaterialTheme.typography.labelLarge)
            ReadingValue(watchTime) { it }
        }
    }
}

@Composable
private fun <T> ReadingValue(
    reading: Reading<T>,
    accent: Color = MaterialTheme.colorScheme.onSurface,
    format: (T) -> String,
) {
    val colors = MaterialTheme.colorScheme
    val value = when (reading) {
        is Reading.Valid -> format(reading.value)
        is Reading.Invalid -> "— · ${reading.reason}"
        is Reading.Stale -> "— · 数据已过期"
        Reading.Missing -> "— · 待同步"
    }
    val color = when (reading) {
        is Reading.Valid -> accent
        is Reading.Invalid, is Reading.Stale -> colors.tertiary
        Reading.Missing -> colors.onSurfaceVariant
    }
    Text(
        value,
        color = color,
        style = if (reading is Reading.Valid) MaterialTheme.typography.titleLarge else MaterialTheme.typography.bodyMedium,
        fontWeight = FontWeight.SemiBold,
    )
    ReadingTimestamp(reading)
}

@Composable
private fun ReadingTimestamp(reading: Reading<*>) {
    val time = when (reading) {
        is Reading.Valid -> "手机接收于 ${reading.receivedAt}"
        is Reading.Stale -> "上次接收于 ${reading.lastReceivedAt}"
        else -> return
    }
    Text(time, color = MaterialTheme.colorScheme.onSurfaceVariant, style = MaterialTheme.typography.bodySmall)
}

@Composable
private fun ScenarioSelector(scenarios: List<HomeScenario>, selected: Int, onSelect: (Int) -> Unit) {
    var expanded by remember { mutableStateOf(false) }
    Box {
        OutlinedButton(onClick = { expanded = true }) { Text("模拟场景 · ${scenarios[selected].title}") }
        DropdownMenu(expanded = expanded, onDismissRequest = { expanded = false }) {
            scenarios.forEachIndexed { index, scenario ->
                DropdownMenuItem(text = { Text(scenario.title) }, onClick = {
                    expanded = false
                    onSelect(index)
                })
            }
        }
    }
}
