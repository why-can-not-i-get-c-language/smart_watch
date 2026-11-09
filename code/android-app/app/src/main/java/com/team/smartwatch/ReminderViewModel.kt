package com.team.smartwatch

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.lifecycle.ViewModel
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
class ReminderViewModel : ViewModel() {
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Main.immediate)
    private var job: Job? = null
    private var connection = Connection.DISCONNECTED
    private var revision = -1
    internal var time by mutableStateOf("08:30")
        private set
    internal var enabled by mutableStateOf(true)
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
        connection = next
        revision = nextRevision
        confirmed = null
        phase = ReminderPhase.IDLE
        message = if (interrupted) "连接已变化，本次设置未确认" else "等待设置"
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
        phase = ReminderPhase.IDLE
        message = "草稿尚未发送"
        error = null
    }

    internal fun nextReply() {
        if (!phase.busy) reply = ReminderReply.entries[(reply.ordinal + 1) % ReminderReply.entries.size]
    }

    internal fun save() {
        if (connection != Connection.SYNCED || phase.busy) return
        if (!Regex("(?:[01][0-9]|2[0-3]):[0-5][0-9]").matches(time)) {
            error = "请输入 00:00–23:59，格式如 08:30"
            return
        }
        val requested = ReminderConfig(time, enabled)
        val requestRevision = revision
        val requestedReply = reply
        phase = ReminderPhase.SENDING
        error = null
        message = "正在模拟发送提醒设置…"
        job = scope.launch {
            delay(900)
            if (requestRevision != revision || connection != Connection.SYNCED) return@launch
            when (requestedReply) {
                ReminderReply.REJECTED -> { fail("模拟设备拒绝设置，请重试"); return@launch }
                ReminderReply.TIMEOUT -> { delay(900); fail("模拟应答超时，请重试"); return@launch }
                else -> Unit
            }
            phase = ReminderPhase.VERIFYING
            message = "模拟设备已应答，正在回读提醒…"
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
