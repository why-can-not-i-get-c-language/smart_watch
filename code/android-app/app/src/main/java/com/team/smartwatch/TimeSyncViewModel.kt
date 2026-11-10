package com.team.smartwatch

import android.app.Application
import android.os.SystemClock
import android.util.Log
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.lifecycle.AndroidViewModel
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import java.util.TimeZone
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch

internal fun beijingTime(millis: Long): String =
    SimpleDateFormat("yyyy-MM-dd HH:mm:ss", Locale.CHINA).apply {
        timeZone = TimeZone.getTimeZone("GMT+08:00")
    }.format(Date(millis))

internal data class ClockSample(val utcMillis: Long, val elapsedMillis: Long) {
    companion object {
        fun now() = ClockSample(System.currentTimeMillis(), SystemClock.elapsedRealtime())
    }
}

internal enum class TimeSyncPhase(val busy: Boolean = false) {
    IDLE, SENDING(true), VERIFYING(true), SUCCESS, FAILED,
}

internal enum class SimulatedTimeReply(val label: String) {
    SUCCESS("正常应答"), REJECTED("设备拒绝"), MISMATCH("回读不一致"), TIMEOUT("应答超时"),
}

/** 仅用于模拟界面与流程；不连接 BLE，不代表真实手表执行了校时。 */
class TimeSyncViewModel(application: Application) : AndroidViewModel(application) {
    private val preferences = application.getSharedPreferences("time_sync", 0)
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Main.immediate)
    private var job: Job? = null
    private var connection = Connection.DISCONNECTED
    private var revision = -1
    private var lastAutoRevision = -1
    private var runningAutomatically = false
    private var attempted = false
    private var attemptId = 0
    private data class Clock(val utcMillis: Long, val elapsedMillis: Long, val receivedMillis: Long)
    private var clock by mutableStateOf<Clock?>(null)

    internal var automatic by mutableStateOf(preferences.getBoolean("automatic", false))
        private set
    internal var phase by mutableStateOf(TimeSyncPhase.IDLE)
        private set
    internal var message by mutableStateOf("等待校时")
        private set
    internal var reply by mutableStateOf(SimulatedTimeReply.SUCCESS)
        private set

    internal fun connectionChanged(next: Connection, nextRevision: Int) {
        if (connection == next && revision == nextRevision) return
        val interrupted = phase.busy
        job?.cancel()
        connection = next
        revision = nextRevision
        clock = null
        attempted = false
        phase = TimeSyncPhase.IDLE
        message = if (interrupted) "连接已变化，本次校时未确认" else "等待校时"
        maybeAutomatic()
    }

    internal fun setAutomatic(enabled: Boolean) {
        automatic = enabled
        preferences.edit().putBoolean("automatic", enabled).apply()
        if (!enabled && runningAutomatically && phase.busy) {
            job?.cancel()
            phase = TimeSyncPhase.IDLE
            message = "自动校时已关闭，本次操作未确认"
        }
        if (enabled) maybeAutomatic()
    }

    private fun maybeAutomatic() {
        if (automatic && connection == Connection.SYNCED && lastAutoRevision != revision) {
            lastAutoRevision = revision
            synchronize(automatically = true)
        }
    }

    internal fun nextReply() {
        if (!phase.busy) reply = SimulatedTimeReply.entries[(reply.ordinal + 1) % SimulatedTimeReply.entries.size]
    }

    internal fun synchronize(automatically: Boolean = false) {
        if (connection != Connection.SYNCED || phase.busy) return
        // 模拟时钟保留毫秒，显示时才截到秒。
        val start = ClockSample.now()
        val utc = start.utcMillis / 1000L
        if (utc !in 946684800L..4102444799L) {
            phase = TimeSyncPhase.FAILED
            message = "手机时间超出手表支持范围（2000—2099 年 UTC）"
            return
        }
        val requestRevision = revision
        val requestedReply = reply
        runningAutomatically = automatically
        attempted = true
        clock = null
        phase = TimeSyncPhase.SENDING
        message = if (automatically) "自动校时：正在模拟发送…" else "手动校时：正在模拟发送…"
        Log.i("TimeSyncSimulator", "start id=${++attemptId} automatic=$automatically session=$revision")
        job = scope.launch {
            delay(900)
            if (requestRevision != revision || connection != Connection.SYNCED) return@launch
            if (requestedReply == SimulatedTimeReply.REJECTED) {
                fail("模拟设备拒绝校时，请重试")
                return@launch
            }
            if (requestedReply == SimulatedTimeReply.TIMEOUT) {
                delay(900)
                fail("模拟应答超时，请重试")
                return@launch
            }
            phase = TimeSyncPhase.VERIFYING
            message = "模拟设备已应答，正在回读核对…"
            delay(900)
            val received = ClockSample.now()
            val expected = start.utcMillis + (received.elapsedMillis - start.elapsedMillis)
            val readback = expected + if (requestedReply == SimulatedTimeReply.MISMATCH) 60_000L else 0L
            // 此处是可控模拟数据的相等核对，不是实机精度或误差阈值结论。
            if (readback != expected) {
                fail("模拟回读时间不一致，请重试")
                return@launch
            }
            clock = Clock(readback, received.elapsedMillis, received.utcMillis)
            phase = TimeSyncPhase.SUCCESS
            message = "模拟校时成功 · ${if (automatically) "自动" else "手动"}"
            Log.i("TimeSyncSimulator", "confirmed id=$attemptId session=$revision")
        }
    }

    private fun fail(reason: String) {
        phase = TimeSyncPhase.FAILED
        message = reason
        Log.i("TimeSyncSimulator", "failed id=$attemptId reason=$reason")
    }

    internal fun watchReading(elapsed: Long, currentRevision: Int): Reading<String>? {
        if (connection != Connection.SYNCED || revision != currentRevision) return null
        val confirmed = clock
        if (confirmed != null) {
            val utcMillis = confirmed.utcMillis + (elapsed - confirmed.elapsedMillis)
            return Reading.Valid(beijingTime(utcMillis), beijingTime(confirmed.receivedMillis))
        }
        return if (attempted) Reading.Invalid("校时尚未确认") else null
    }

    override fun onCleared() {
        scope.cancel()
        super.onCleared()
    }
}
