package com.team.smartwatch

import android.content.Intent

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.SystemBarStyle
import androidx.activity.compose.BackHandler
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.SideEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.saveable.rememberSaveableStateHolder
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.core.view.WindowCompat
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

private enum class BleEntryAction { BROWSE, CONNECT_FAVORITE, SCAN }
private enum class PhonePage { HISTORY, WEATHER, CONTACTS }

class MainActivity : ComponentActivity() {
    private var sosNotificationOpen by mutableIntStateOf(0)
    private val timeSync by lazy { ViewModelProvider(this)[TimeSyncViewModel::class.java] }
    private val reminder by lazy { ViewModelProvider(this)[ReminderViewModel::class.java] }
    private val lowActivity by lazy { ViewModelProvider(this)[LowActivityViewModel::class.java] }
    private val fallSos by lazy { ViewModelProvider(this)[FallSosViewModel::class.java] }
    private val forecast by lazy { ViewModelProvider(this)[PhoneWeatherViewModel::class.java] }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        if (intent.action == SosNotification.OPEN_SOS) sosNotificationOpen++
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val ble = BleIntegration(this)
        if (savedInstanceState == null && intent.action == SosNotification.OPEN_SOS) sosNotificationOpen++
        enableEdgeToEdge(
            statusBarStyle = SystemBarStyle.light(android.graphics.Color.TRANSPARENT, android.graphics.Color.BLACK),
            navigationBarStyle = SystemBarStyle.light(android.graphics.Color.TRANSPARENT, android.graphics.Color.BLACK),
        )
        setContent {
            MaterialTheme(colorScheme = HealthColors) {
                val demoAvailable = BuildConfig.D3_FAULT_INJECTION
                var showDemo by rememberSaveable { mutableStateOf(false) }
                val demoMode = demoAvailable && showDemo
                val scenarios = SimulatedWatch.scenarios
                var index by rememberSaveable { mutableIntStateOf(0) }
                var connectionRevision by rememberSaveable { mutableIntStateOf(0) }
                var showBle by rememberSaveable { mutableStateOf(false) }
                var bleEntryAction by remember { mutableStateOf(BleEntryAction.BROWSE) }
                var showConnection by rememberSaveable { mutableStateOf(false) }
                var showTimeSync by rememberSaveable { mutableStateOf(false) }
                var showReminder by rememberSaveable { mutableStateOf(false) }
                var showLowActivity by rememberSaveable { mutableStateOf(false) }
                var showFallSos by rememberSaveable { mutableStateOf(false) }
                var phonePage by rememberSaveable { mutableStateOf<PhonePage?>(null) }
                var clockTick by remember { mutableStateOf(ClockSample.now()) }
                var trend by rememberSaveable { mutableStateOf<HealthTrendMetric?>(null) }
                val homeState = rememberSaveableStateHolder()
                val scenario = scenarios[index]
                val original = scenario.source.read()
                val returnHome = {
                    showBle = false
                    bleEntryAction = BleEntryAction.BROWSE
                    showConnection = false
                    showTimeSync = false
                    showReminder = false
                    showLowActivity = false
                    showFallSos = false
                    phonePage = null
                    trend = null
                }
                val openBle: (BleEntryAction) -> Unit = { action ->
                    bleEntryAction = action
                    showBle = true
                }
                LaunchedEffect(sosNotificationOpen) {
                    if (sosNotificationOpen > 0) {
                        returnHome()
                        if (demoAvailable) {
                            showDemo = true
                            showFallSos = true
                        } else {
                            phonePage = PhonePage.CONTACTS
                        }
                    }
                }
                LaunchedEffect(demoMode, original.connection, connectionRevision) {
                    val connection = if (demoMode) original.connection else Connection.DISCONNECTED
                    if (demoAvailable) {
                        timeSync.connectionChanged(connection, connectionRevision)
                        reminder.connectionChanged(connection, connectionRevision)
                        lowActivity.connectionChanged(connection, connectionRevision)
                        fallSos.connectionChanged(connection, connectionRevision)
                    }
                }
                LaunchedEffect(Unit) {
                    while (true) {
                        // 两行时间共用一次采样，整秒后留 25 毫秒避开采样边界。
                        delay(1_025L - clockTick.utcMillis % 1_000L)
                        clockTick = ClockSample.now()
                    }
                }
                val current = original.copy(
                    watchTime = timeSync.watchReading(clockTick.elapsedMillis, connectionRevision) ?: original.watchTime,
                )
                val demoSubpage = demoMode && (trend != null || showConnection || showTimeSync ||
                    showReminder || showLowActivity || showFallSos || phonePage == PhonePage.HISTORY)
                val isHome = !showBle && !demoSubpage &&
                    phonePage != PhonePage.WEATHER && phonePage != PhonePage.CONTACTS
                SideEffect {
                    val bars = WindowCompat.getInsetsController(window, window.decorView)
                    bars.isAppearanceLightStatusBars = !isHome
                    bars.isAppearanceLightNavigationBars = !isHome
                }
                BackHandler(enabled = !isHome || demoMode) {
                    when {
                        showBle -> showBle = false
                        isHome && demoMode -> {
                            returnHome()
                            showDemo = false
                        }
                        phonePage == PhonePage.WEATHER || phonePage == PhonePage.CONTACTS -> phonePage = null
                        showConnection -> showConnection = false
                        showTimeSync -> showTimeSync = false
                        showReminder -> showReminder = false
                        showLowActivity -> showLowActivity = false
                        showFallSos -> showFallSos = false
                        phonePage != null -> phonePage = null
                        else -> trend = null
                    }
                }
                if (showBle) {
                    ble.Screen(onHome = returnHome)
                    LaunchedEffect(Unit) {
                        val action = bleEntryAction
                        bleEntryAction = BleEntryAction.BROWSE
                        when (action) {
                            BleEntryAction.BROWSE -> Unit
                            BleEntryAction.CONNECT_FAVORITE -> ble.connectFavoriteOnEntry()
                            BleEntryAction.SCAN -> ble.rescan()
                        }
                    }
                    return@MaterialTheme
                }
                Scaffold(containerColor = if (isHome) WatchOverviewColors.background else HealthColors.background) { padding ->
                    val metric = trend
                    if (phonePage == PhonePage.CONTACTS) {
                        EmergencyContactScreen(
                            fallSos, returnHome, Modifier.padding(padding), demonstration = false,
                        )
                    } else if (phonePage == PhonePage.WEATHER) {
                        PhoneWeatherScreen(forecast, clockTick.utcMillis, returnHome, Modifier.padding(padding))
                    } else if (!demoMode) {
                        homeState.SaveableStateProvider("liveHome") {
                            LiveHomeScreen(
                                onConnection = { openBle(BleEntryAction.CONNECT_FAVORITE) },
                                onFavorites = { openBle(BleEntryAction.BROWSE) },
                                onWeather = { phonePage = PhonePage.WEATHER },
                                onEmergencyContacts = if (demoAvailable) null else ({ phonePage = PhonePage.CONTACTS }),
                                onDemo = if (demoAvailable) ({ showDemo = true }) else null,
                                modifier = Modifier.padding(padding),
                            ) {
                                LiveWatchStatusCard(
                                    watch = ble.d3Watch,
                                    linking = ble.connectingOrConnected,
                                    connectionMessage = ble.connectionStatus,
                                    nowElapsedMillis = clockTick.elapsedMillis,
                                    onDisconnect = ble::disconnect,
                                    onRescan = { openBle(BleEntryAction.SCAN) },
                                    lifecycle = this@MainActivity.lifecycle,
                                )
                            }
                        }
                    } else if (showConnection) {
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
                            onBle = { openBle(BleEntryAction.CONNECT_FAVORITE) },
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
                            phoneTime = beijingTime(clockTick.utcMillis),
                            onBack = { showTimeSync = false },
                            onConnection = { showConnection = true },
                            modifier = Modifier.padding(padding),
                        )
                    } else if (phonePage == PhonePage.HISTORY) {
                        WatchHistoryScreen(current.steps, returnHome, Modifier.padding(padding))
                    } else if (metric == null) {
                        homeState.SaveableStateProvider("home") {
                            WatchHomeScreen(
                                state = current,
                                scenarios = scenarios,
                                selectedScenario = index,
                                onScenario = { next ->
                                    if (index != next) {
                                        index = next
                                        connectionRevision++
                                    }
                                },
                                onSimulatedConnection = { showConnection = true },
                                onTimeSync = { showTimeSync = true },
                                onReminder = { showReminder = true },
                                onLowActivity = { showLowActivity = true },
                                lowActivityReminder = current.connection == Connection.SYNCED && lowActivity.reminding,
                                onFallSos = { showFallSos = true },
                                connectionRevision = connectionRevision,
                                onTrend = { trend = it },
                                onHistory = { phonePage = PhonePage.HISTORY },
                                onExitDemo = { returnHome(); showDemo = false },
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
