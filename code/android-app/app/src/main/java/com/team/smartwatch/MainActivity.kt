package com.team.smartwatch

import android.content.Intent

import android.os.Bundle
import android.os.SystemClock
import androidx.activity.ComponentActivity
import androidx.activity.SystemBarStyle
import androidx.activity.compose.BackHandler
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
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
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableLongStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.saveable.rememberSaveableStateHolder
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.lifecycle.ViewModelProvider
import kotlinx.coroutines.delay

private val HealthColors = lightColorScheme(
    primary = Color(0xFFA9360B),
    secondary = Color(0xFF17598E),
    tertiary = Color(0xFF814406),
    background = Color(0xFFF8F7FB),
    surface = Color.White,
    surfaceContainer = Color.White,
    primaryContainer = Color(0xFFFFE3D1),
    onBackground = Color(0xFF1E2631),
    onSurface = Color(0xFF1E2631),
    onSurfaceVariant = Color(0xFF5E6673),
    outline = Color(0xFF767D88),
)

class MainActivity : ComponentActivity() {
    private var sosNotificationOpen by mutableIntStateOf(0)
    private val timeSync by lazy { ViewModelProvider(this)[TimeSyncViewModel::class.java] }
    private val reminder by lazy { ViewModelProvider(this)[ReminderViewModel::class.java] }
    private val lowActivity by lazy { ViewModelProvider(this)[LowActivityViewModel::class.java] }
    private val fallSos by lazy { ViewModelProvider(this)[FallSosViewModel::class.java] }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        if (intent.action == SosNotification.OPEN_SOS) sosNotificationOpen++
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        if (savedInstanceState == null && intent.action == SosNotification.OPEN_SOS) sosNotificationOpen++
        enableEdgeToEdge(
            statusBarStyle = SystemBarStyle.light(android.graphics.Color.TRANSPARENT, android.graphics.Color.BLACK),
            navigationBarStyle = SystemBarStyle.light(android.graphics.Color.TRANSPARENT, android.graphics.Color.BLACK),
        )
        setContent {
            MaterialTheme(colorScheme = HealthColors) {
                val scenarios = SimulatedWatch.scenarios
                var index by rememberSaveable { mutableIntStateOf(0) }
                var connectionRevision by rememberSaveable { mutableIntStateOf(0) }
                var showConnection by rememberSaveable { mutableStateOf(false) }
                var showTimeSync by rememberSaveable { mutableStateOf(false) }
                var showReminder by rememberSaveable { mutableStateOf(false) }
                var showLowActivity by rememberSaveable { mutableStateOf(false) }
                var showFallSos by rememberSaveable { mutableStateOf(false) }
                var clockTick by remember { mutableLongStateOf(SystemClock.elapsedRealtime()) }
                var trend by rememberSaveable { mutableStateOf<HealthTrendMetric?>(null) }
                val homeState = rememberSaveableStateHolder()
                val scenario = scenarios[index]
                val original = scenario.source.read()
                LaunchedEffect(sosNotificationOpen) {
                    if (sosNotificationOpen > 0) {
                        showConnection = false
                        showReminder = false
                        showLowActivity = false
                        showTimeSync = false
                        trend = null
                        showFallSos = true
                    }
                }
                LaunchedEffect(original.connection, connectionRevision) {
                    timeSync.connectionChanged(original.connection, connectionRevision)
                    reminder.connectionChanged(original.connection, connectionRevision)
                    lowActivity.connectionChanged(original.connection, connectionRevision)
                    fallSos.connectionChanged(original.connection, connectionRevision)
                }
                LaunchedEffect(Unit) {
                    while (true) {
                        delay(1000)
                        clockTick = SystemClock.elapsedRealtime()
                    }
                }
                val current = original.copy(watchTime = timeSync.watchReading(clockTick, connectionRevision) ?: original.watchTime)
                BackHandler(enabled = trend != null || showConnection || showTimeSync || showReminder || showLowActivity || showFallSos) {
                    when {
                        showConnection -> showConnection = false
                        showTimeSync -> showTimeSync = false
                        showReminder -> showReminder = false
                        showLowActivity -> showLowActivity = false
                        showFallSos -> showFallSos = false
                        else -> trend = null
                    }
                }
                Scaffold { padding ->
                    val metric = trend
                    if (showConnection) {
                        ConnectionScreen(
                            connection = scenario.source.read().connection,
                            onChange = { connection ->
                                val next = scenarios.indexOfLast { it.source.read().connection == connection }
                                if (next != index) {
                                    index = next
                                    connectionRevision++
                                }
                            },
                            onBack = { showConnection = false },
                            modifier = Modifier.padding(padding),
                        )
                    } else if (showFallSos) {
                        FallSosScreen(
                            model = fallSos,
                            connection = current.connection,
                            onBack = { showFallSos = false },
                            onConnection = { showConnection = true },
                            modifier = Modifier.padding(padding),
                        )
                    } else if (showLowActivity) {
                        LowActivityScreen(
                            model = lowActivity,
                            connection = current.connection,
                            onBack = { showLowActivity = false },
                            onConnection = { showConnection = true },
                            modifier = Modifier.padding(padding),
                        )
                    } else if (showReminder) {
                        ReminderScreen(
                            model = reminder,
                            connection = current.connection,
                            onBack = { showReminder = false },
                            onConnection = { showConnection = true },
                            modifier = Modifier.padding(padding),
                        )
                    } else if (showTimeSync) {
                        TimeSyncScreen(
                            model = timeSync,
                            connection = current.connection,
                            watchTime = current.watchTime,
                            phoneTime = beijingTime(System.currentTimeMillis()),
                            onBack = { showTimeSync = false },
                            onConnection = { showConnection = true },
                            modifier = Modifier.padding(padding),
                        )
                    } else if (metric == null) {
                        homeState.SaveableStateProvider("home") {
                            WatchHome(
                                state = current,
                                scenario = scenario.title,
                                onNext = {
                                    index = (index + 1) % scenarios.size
                                    connectionRevision++
                                },
                                onConnection = { showConnection = true },
                                onTimeSync = { showTimeSync = true },
                                onReminder = { showReminder = true },
                                onLowActivity = { showLowActivity = true },
                                lowActivityReminder = current.connection == Connection.SYNCED && lowActivity.reminding,
                                onFallSos = { showFallSos = true },
                                connectionRevision = connectionRevision,
                                onTrend = { trend = it },
                                modifier = Modifier.padding(padding),
                            )
                        }
                    } else {
                        HealthTrendScreen(metric, onBack = { trend = null }, Modifier.padding(padding))
                    }
                }
            }
        }
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun WatchHome(
    state: WatchHomeState,
    scenario: String,
    onNext: () -> Unit,
    onConnection: () -> Unit,
    onTimeSync: () -> Unit,
    onReminder: () -> Unit,
    onLowActivity: () -> Unit,
    lowActivityReminder: Boolean,
    onFallSos: () -> Unit,
    connectionRevision: Int,
    onTrend: (HealthTrendMetric) -> Unit,
    modifier: Modifier = Modifier,
) {
    val colors = MaterialTheme.colorScheme
    val statusColor = when (state.connection) {
        Connection.SYNCED -> Color(0xFF15664C)
        Connection.SYNCING, Connection.CONNECTING -> colors.secondary
        else -> colors.tertiary
    }
    Column(
        modifier = modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(20.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        Text("健康", style = MaterialTheme.typography.headlineMedium, fontWeight = FontWeight.Bold)
        Row(
            modifier = Modifier
                .background(statusColor.copy(alpha = 0.10f), CircleShape)
                .padding(horizontal = 12.dp, vertical = 6.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            Box(Modifier.size(8.dp).background(statusColor, CircleShape))
            Text("${state.source} · ${state.connection.label}", color = statusColor)
        }
        state.lastSync?.let {
            Text("上次同步 $it", color = colors.onSurfaceVariant, style = MaterialTheme.typography.bodySmall)
        }
        FlowRow(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
            OutlinedButton(onClick = onConnection) { Text("设备连接") }
            OutlinedButton(onClick = onTimeSync) { Text("时间校准") }
            OutlinedButton(onClick = onReminder) { Text("手表闹钟") }
            OutlinedButton(onClick = onLowActivity) { Text("低运动提醒") }
            OutlinedButton(onClick = onFallSos) { Text("跌倒 SOS") }
        }

        if (lowActivityReminder) LowActivityAlert()
        ReadingCard("今日步数", state.steps, featured = true, detail = { it.date }) { "${it.count} 步" }
        StepHistoryCard(state.steps)
        Row(Modifier.height(IntrinsicSize.Min), horizontalArrangement = Arrangement.spacedBy(12.dp)) {
            ReadingCard(
                "心率", state.heartRate, Modifier.weight(1f).fillMaxHeight(),
                containerColor = Color(0xFFFCE8EE), accent = HealthTrendMetric.HEART_RATE.accent,
                onClick = { onTrend(HealthTrendMetric.HEART_RATE) },
            ) { "$it bpm" }
            ReadingCard(
                "血氧估算", state.oxygen, Modifier.weight(1f).fillMaxHeight(),
                containerColor = Color(0xFFE5F3FF), accent = HealthTrendMetric.OXYGEN.accent,
                onClick = { onTrend(HealthTrendMetric.OXYGEN) },
            ) { "$it%" }
        }
        Row(Modifier.height(IntrinsicSize.Min), horizontalArrangement = Arrangement.spacedBy(12.dp)) {
            ReadingCard("手表时间", state.watchTime, Modifier.weight(1f).fillMaxHeight()) { it }
            ReadingCard("电量估算", state.battery, Modifier.weight(1f).fillMaxHeight()) { "$it%" }
        }
        WeatherCard(state.connection, connectionRevision)
        Text("场景：$scenario", color = colors.onSurfaceVariant, style = MaterialTheme.typography.bodySmall)
        OutlinedButton(onClick = onNext) { Text("切换模拟场景") }
    }
}

@Composable
private fun <T> ReadingCard(
    title: String,
    reading: Reading<T>,
    modifier: Modifier = Modifier,
    featured: Boolean = false,
    detail: ((T) -> String)? = null,
    containerColor: Color = Color.Unspecified,
    accent: Color = Color.Unspecified,
    onClick: (() -> Unit)? = null,
    format: (T) -> String,
) {
    val colors = MaterialTheme.colorScheme
    val typography = MaterialTheme.typography
    val value = when (reading) {
        is Reading.Valid -> format(reading.value)
        Reading.Missing -> "— · 待同步"
        is Reading.Invalid -> "— · ${reading.reason}"
        is Reading.Stale -> "— · 数据已过期"
    }
    val time = when (reading) {
        is Reading.Valid -> "手机接收于 ${reading.receivedAt}"
        is Reading.Stale -> "上次接收于 ${reading.lastReceivedAt}"
        else -> null
    }
    val valueColor = when (reading) {
        is Reading.Valid -> when {
            featured -> colors.primary
            accent != Color.Unspecified -> accent
            else -> colors.onSurface
        }
        Reading.Missing -> colors.onSurfaceVariant
        is Reading.Invalid, is Reading.Stale -> colors.tertiary
    }
    val valueStyle = when {
        reading !is Reading.Valid -> typography.bodyLarge
        featured -> typography.displaySmall
        else -> typography.titleLarge
    }
    val action = if (onClick != null) {
        Modifier.clickable(role = Role.Button, onClickLabel = "查看${title}24 小时记录", onClick = onClick)
    } else {
        Modifier
    }
    Card(
        modifier = modifier.fillMaxWidth().then(action),
        colors = CardDefaults.cardColors(
            containerColor = when {
                featured -> colors.primaryContainer
                containerColor != Color.Unspecified -> containerColor
                else -> colors.surfaceContainer
            },
            contentColor = colors.onSurface,
        ),
    ) {
        val fill = if (featured) {
            Modifier.background(Brush.horizontalGradient(listOf(Color(0xFFFFF1E5), Color(0xFFFFD8BD))))
        } else {
            Modifier
        }
        Column(
            Modifier.fillMaxWidth().then(fill).padding(14.dp),
            verticalArrangement = Arrangement.spacedBy(4.dp),
        ) {
            Text(title, color = colors.onSurfaceVariant, style = typography.labelLarge)
            Text(value, color = valueColor, style = valueStyle, fontWeight = FontWeight.SemiBold)
            if (reading is Reading.Valid && detail != null) {
                Text(detail(reading.value), color = colors.onSurfaceVariant, style = typography.bodySmall)
            }
            time?.let { Text(it, color = colors.onSurfaceVariant, style = typography.bodySmall) }
            if (onClick != null) {
                Text("查看 24 小时记录 ›", color = accent, style = typography.labelMedium)
            }
        }
    }
}

