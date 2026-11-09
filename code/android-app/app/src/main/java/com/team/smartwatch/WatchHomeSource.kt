package com.team.smartwatch

sealed interface Reading<out T> {
    data object Missing : Reading<Nothing>
    data class Valid<T>(val value: T, val receivedAt: String) : Reading<T>
    data class Invalid(val reason: String) : Reading<Nothing>
    data class Stale(val lastReceivedAt: String) : Reading<Nothing>
}

data class DailySteps(val count: Int, val date: String)

data class StepHistoryDay(val day: String, val date: String, val count: Int?)

enum class Connection(val label: String) {
    DISCONNECTED("未连接"), CONNECTING("连接中"), SYNCING("同步中"), SYNCED("已同步"),
    BLUETOOTH_OFF("蓝牙已关闭"), PERMISSION_REQUIRED("缺少附近设备权限"), FAILED("连接失败")
}

data class WatchHomeState(
    val source: String,
    val connection: Connection,
    val lastSync: String? = null,
    val watchTime: Reading<String> = Reading.Missing,
    val steps: Reading<DailySteps> = Reading.Missing,
    val battery: Reading<Int> = Reading.Missing,
    val heartRate: Reading<Int> = Reading.Missing,
    val oxygen: Reading<Int> = Reading.Missing,
)

fun interface WatchHomeSource {
    fun read(): WatchHomeState
}

data class HomeScenario(val title: String, val source: WatchHomeSource)

object SimulatedWatch {
    const val DEMO_DATE = "11 月 6 日"
    val pastSteps = listOf(
        StepHistoryDay("31", "10 月 31 日", 3180),
        StepHistoryDay("1", "11 月 1 日", 5940),
        StepHistoryDay("2", "11 月 2 日", null),
        StepHistoryDay("3", "11 月 3 日", 0),
        StepHistoryDay("4", "11 月 4 日", 4560),
        StepHistoryDay("5", "11 月 5 日", 3720),
    )
    private const val RECEIVED_AT = "09:30"
    private val empty = WatchHomeState("模拟数据", Connection.DISCONNECTED)
    private val synced = empty.copy(
        connection = Connection.SYNCED,
        lastSync = RECEIVED_AT,
        watchTime = Reading.Valid("$DEMO_DATE 09:29", RECEIVED_AT),
        steps = Reading.Valid(DailySteps(0, DEMO_DATE), RECEIVED_AT),
        battery = Reading.Valid(62, RECEIVED_AT),
        heartRate = Reading.Valid(72, RECEIVED_AT),
        oxygen = Reading.Valid(97, RECEIVED_AT),
    )

    val scenarios = listOf(
        HomeScenario("首次打开") { empty },
        HomeScenario("同步成功 · 零步数") { synced },
        HomeScenario("健康读数无效") {
            synced.copy(
                heartRate = Reading.Invalid("佩戴接触不良"),
                oxygen = Reading.Invalid("运动干扰"),
            )
        },
        HomeScenario("心率过期") {
            synced.copy(heartRate = Reading.Stale(RECEIVED_AT))
        },
        HomeScenario("同步后断线") { empty.copy(lastSync = RECEIVED_AT) },
        HomeScenario("重连 · 等待新数据") {
            empty.copy(connection = Connection.SYNCING, lastSync = RECEIVED_AT)
        },
        HomeScenario("重连 · 收到新数据") {
            synced.copy(
                lastSync = "09:32",
                watchTime = Reading.Valid("$DEMO_DATE 09:32", "09:32"),
                steps = Reading.Valid(DailySteps(12, DEMO_DATE), "09:32"),
                battery = Reading.Valid(62, "09:32"),
                heartRate = Reading.Valid(74, "09:32"),
                oxygen = Reading.Valid(97, "09:32"),
            )
        },
        HomeScenario("正在连接") { empty.copy(connection = Connection.CONNECTING) },
        HomeScenario("蓝牙关闭") { empty.copy(connection = Connection.BLUETOOTH_OFF) },
        HomeScenario("权限不足") { empty.copy(connection = Connection.PERMISSION_REQUIRED) },
        HomeScenario("连接失败") { empty.copy(connection = Connection.FAILED) },
    )
}

