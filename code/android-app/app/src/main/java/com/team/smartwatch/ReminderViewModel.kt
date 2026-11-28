package com.team.smartwatch

import android.app.Application
import android.content.Context
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.lifecycle.AndroidViewModel
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch

internal enum class ReminderPhase(val busy: Boolean = false) {
    IDLE, SENDING(true), VERIFYING(true), SUCCESS, FAILED,
}

internal enum class ReminderReply(val label: String) {
    SUCCESS("正常应答"), REJECTED("设备拒绝"), MISMATCH("回读不一致"), TIMEOUT("应答超时"),
}

internal data class ReminderConfig(val time: String, val enabled: Boolean)

// 单个每日提醒的模拟设置。正式重复规则和协议参数待双方确认。
class ReminderViewModel(application: Application) : AndroidViewModel(application) {
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Main.immediate)
    private val drafts = application.getSharedPreferences("reminder_drafts", Context.MODE_PRIVATE)
    private val timeFormat = Regex("(?:[01][0-9]|2[0-3]):[0-5][0-9]")
    private var job: Job? = null
    private val delayDevice = OfflineWatchSession()
    private val delayRepository = WatchRepository(delayDevice)
    private var savingDelay: Job? = null
    private var countdown: Job? = null
    internal var delaySeconds by mutableIntStateOf(
        drafts.getInt("reminder_delay_seconds", 30).takeIf { it in reminderDelayOptions } ?: 30,
    )
        private set
    internal var confirmedDelay by mutableStateOf<Int?>(null)
        private set
    internal var delaySaving by mutableStateOf(false)
        private set
    internal var countdownActive by mutableStateOf(false)
        private set
    internal var delayMessage by mutableStateOf("等待模拟手表回读")
        private set

    private var connection = Connection.DISCONNECTED
    private var revision = -1
    internal var time by mutableStateOf(drafts.getString("alarm_time", null)?.takeIf(timeFormat::matches) ?: "08:30")
        private set
    internal var enabled by mutableStateOf(drafts.getBoolean("alarm_enabled", true))
        private set
    internal var phase by mutableStateOf(ReminderPhase.IDLE)
        private set
    internal var message by mutableStateOf("等待设置")
        private set
    internal var error by mutableStateOf<String?>(null)
        private set
    internal var confirmed by mutableStateOf<ReminderConfig?>(null)
        private set
    internal var reply by mutableStateOf(ReminderReply.SUCCESS)
        private set

    internal fun connectionChanged(next: Connection, nextRevision: Int) {
        if (connection == next && revision == nextRevision) return
        val interrupted = phase.busy
        job?.cancel()
        savingDelay?.cancel()
        countdown?.cancel()
        countdownActive = false
        confirmedDelay = null
        delayMessage = if (delaySaving) "连接已变化，本次设置未确认" else "等待模拟手表回读"
        delaySaving = false
        connection = next
        revision = nextRevision
        confirmed = null
        phase = ReminderPhase.IDLE
        message = if (interrupted) "连接已变化，本次设置未确认" else "等待设置"
    }

    internal fun adjustDelay(delta: Int) {
        if (delaySaving || countdownActive) return
        val target = delaySeconds + delta
        if (target !in reminderDelayOptions) return
        delaySeconds = target
        drafts.edit().putInt("reminder_delay_seconds", target).apply()
        delayMessage = "延时草稿已保存在手机"
    }

    internal fun saveDelay() {
        if (connection != Connection.SYNCED || delaySaving || countdownActive) return
        val requested = delaySeconds
        val requestedReply = reply
        delaySaving = true
        delayMessage = "正在等待模拟手表提交并回读…"
        savingDelay = scope.launch {
            try {
                delay(700)
                delayDevice.mismatchSettings = requestedReply == ReminderReply.MISMATCH
                when (requestedReply) {
                    ReminderReply.REJECTED -> delayMessage = "模拟设备拒绝，保留上次确认值"
                    ReminderReply.TIMEOUT -> {
                        delay(900)
                        delayMessage = "模拟应答超时，保留上次确认值"
                    }
                    else -> {
                        confirmedDelay = delayRepository.saveReminderDelay(requested)
                        delayMessage = "模拟延时保存成功 · 已回读"
                    }
                }
            } catch (_: WatchRequestException) {
                delayMessage = "模拟回读不一致，保留上次确认值"
            } finally {
                delayDevice.mismatchSettings = false
                delaySaving = false
            }
        }
    }

    internal fun startCountdown() {
        val saved = confirmedDelay?.takeIf { it == delaySeconds } ?: return
        if (connection != Connection.SYNCED || delaySaving || countdownActive) return
        countdownActive = true
        delayMessage = "模拟单次提醒已启动 · ${saved} 秒后触发"
        countdown = scope.launch {
            delay(saved * 1_000L)
            countdownActive = false
            delayMessage = "模拟单次提醒已到时"
        }
    }

    internal fun cancelCountdown() {
        if (!countdownActive) return
        countdown?.cancel()
        countdownActive = false
        delayMessage = "模拟单次提醒已取消"
    }

    internal fun editTime(value: String) {
        if (phase.busy) return
        time = value
        draftChanged()
    }

    internal fun setEnabled(value: Boolean) {
        if (phase.busy) return
        enabled = value
        draftChanged()
    }

    private fun draftChanged() {
        drafts.edit().putBoolean("alarm_enabled", enabled).apply {
            if (timeFormat.matches(time)) putString("alarm_time", time)
        }.apply()
        phase = ReminderPhase.IDLE
        message = "草稿已保存在手机上"
        error = null
    }

    internal fun nextReply() {
        if (!phase.busy && !delaySaving) reply = ReminderReply.entries[(reply.ordinal + 1) % ReminderReply.entries.size]
    }

    internal fun save() {
        if (connection != Connection.SYNCED || phase.busy) return
        if (!timeFormat.matches(time)) {
            error = "请输入 00:00–23:59，格式如 08:30"
            return
        }
        val requested = ReminderConfig(time, enabled)
        val requestRevision = revision
        val requestedReply = reply
        phase = ReminderPhase.SENDING
        error = null
        message = "正在模拟发送闹钟设置…"
        job = scope.launch {
            delay(900)
            if (requestRevision != revision || connection != Connection.SYNCED) return@launch
            when (requestedReply) {
                ReminderReply.REJECTED -> { fail("模拟设备拒绝设置，请重试"); return@launch }
                ReminderReply.TIMEOUT -> { delay(900); fail("模拟应答超时，请重试"); return@launch }
                else -> Unit
            }
            phase = ReminderPhase.VERIFYING
            message = "模拟手表已应答，正在回读闹钟…"
            delay(900)
            if (requestedReply == ReminderReply.MISMATCH) {
                fail("模拟回读不一致，请重试")
                return@launch
            }
            confirmed = requested
            phase = ReminderPhase.SUCCESS
            message = "模拟保存成功 · 已回读"
        }
    }

    private fun fail(reason: String) {
        phase = ReminderPhase.FAILED
        message = reason
    }

    override fun onCleared() {
        scope.cancel()
        super.onCleared()
    }
}
