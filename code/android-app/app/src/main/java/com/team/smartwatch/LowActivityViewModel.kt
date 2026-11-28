package com.team.smartwatch

import android.app.Application
import android.content.Context
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch

internal const val LOW_ACTIVITY_DEMO_STEP_MINUTES = 15

internal data class LowActivityConfig(val minutes: Int = 60, val enabled: Boolean = true)

// 模拟手表的设置回读与活动窗口。真实运动判定及到时振动由手表负责。
class LowActivityViewModel(application: Application) : AndroidViewModel(application) {
    private val drafts = application.getSharedPreferences("reminder_drafts", Context.MODE_PRIVATE)
    private val repository = WatchRepository(OfflineWatchSession())
    private var connection = Connection.DISCONNECTED
    private var revision = -1
    private var savingJob: Job? = null

    internal var draft by mutableStateOf(
        LowActivityConfig(
            drafts.getInt("low_activity_minutes", 60).takeIf { it in lowActivityOptions } ?: 60,
            drafts.getBoolean("low_activity_enabled", true),
        ),
    )
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
        if (minutes in lowActivityOptions && !saving && minutes != draft.minutes) {
            draft = draft.copy(minutes = minutes)
            saveDraft()
        }
    }

    internal fun setEnabled(enabled: Boolean) {
        if (saving || draft.enabled == enabled) return
        draft = draft.copy(enabled = enabled)
        saveDraft()
    }

    private fun saveDraft() {
        drafts.edit().putInt("low_activity_minutes", draft.minutes)
            .putBoolean("low_activity_enabled", draft.enabled).apply()
        message = "草稿已保存在手机上"
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
            try {
                val readback = repository.saveLowActivity(request.enabled, request.minutes)
                val saved = LowActivityConfig(readback.lowActivityMinutes, readback.lowActivityEnabled)
                if (confirmed != saved) accumulatedMinutes = 0
                confirmed = saved
                message = "模拟保存成功 · 已回读"
            } catch (_: WatchRequestException) {
                message = "模拟保存未确认，保留上次确认值"
            } finally {
                saving = false
            }
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
