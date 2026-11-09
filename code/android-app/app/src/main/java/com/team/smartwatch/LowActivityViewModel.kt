package com.team.smartwatch

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch

internal val lowActivityDemoDurations = listOf(30, 60, 90)
internal const val LOW_ACTIVITY_DEMO_STEP_MINUTES = 15

internal data class LowActivityConfig(val minutes: Int = 60, val enabled: Boolean = true)

// 模拟手表的设置回读与活动窗口。真实运动判定及到时振动由手表负责。
class LowActivityViewModel : ViewModel() {
    private var connection = Connection.DISCONNECTED
    private var revision = -1
    private var savingJob: Job? = null

    internal var draft by mutableStateOf(LowActivityConfig())
        private set
    internal var confirmed by mutableStateOf<LowActivityConfig?>(null)
        private set
    internal var saving by mutableStateOf(false)
        private set
    internal var message by mutableStateOf("尚未保存设置")
        private set
    internal var accumulatedMinutes by mutableIntStateOf(0)
        private set

    internal val canSimulate: Boolean
        get() = connection == Connection.SYNCED && !saving && confirmed?.enabled == true
    internal val reminding: Boolean
        get() = confirmed?.let { it.enabled && accumulatedMinutes >= it.minutes } == true

    internal fun connectionChanged(next: Connection, nextRevision: Int) {
        if (next == connection && nextRevision == revision) return
        val interrupted = saving
        savingJob?.cancel()
        connection = next
        revision = nextRevision
        saving = false
        confirmed = null
        accumulatedMinutes = 0
        message = if (interrupted) "连接已变化，本次设置未确认" else "尚未确认当前设备的设置"
    }

    internal fun selectMinutes(minutes: Int) {
        if (minutes in lowActivityDemoDurations && !saving && minutes != draft.minutes) {
            draft = draft.copy(minutes = minutes)
            message = "设置已修改，尚未保存"
        }
    }

    internal fun setEnabled(enabled: Boolean) {
        if (saving || draft.enabled == enabled) return
        draft = draft.copy(enabled = enabled)
        message = "设置已修改，尚未保存"
    }

    internal fun save() {
        if (connection != Connection.SYNCED || saving) return
        val request = draft
        val requestRevision = revision
        saving = true
        message = "正在等待模拟手表确认…"
        savingJob = viewModelScope.launch {
            delay(700)
            if (connection != Connection.SYNCED || revision != requestRevision) return@launch
            if (confirmed != request) accumulatedMinutes = 0
            confirmed = request
            saving = false
            message = "模拟保存成功 · 已回读"
        }
    }

    internal fun simulateLowActivity() {
        val config = confirmed ?: return
        if (!canSimulate || reminding) return
        accumulatedMinutes = (accumulatedMinutes + LOW_ACTIVITY_DEMO_STEP_MINUTES).coerceAtMost(config.minutes)
    }

    internal fun simulateActivityResumed() {
        if (canSimulate) accumulatedMinutes = 0
    }
}
