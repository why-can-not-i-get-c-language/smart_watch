package com.team.smartwatch

import android.app.Application
import android.content.Context
import android.os.SystemClock
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.lifecycle.AndroidViewModel
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import java.util.TimeZone
import org.json.JSONArray
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.withContext

internal enum class FallEventStage(val label: String) {
    COUNTDOWN("疑似跌倒 · 等待手表确认"),
    REQUESTED("模拟手表已发起求助"),
    CANCELLED("模拟手表已取消"),
}

// 仅模拟手表事件和手机用户操作，正式事件 ID、倒计时与补报规则待协议确认。
class FallSosViewModel(application: Application) : AndroidViewModel(application) {
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Main.immediate)
    private val preferences = application.getSharedPreferences("emergency-contact", Context.MODE_PRIVATE)
    private var countdownJob: Job? = null
    private var callJob: Job? = null
    private var callDeadline = 0L
    private var smsConsidered = false
    private var eventBody = ""
    private var connection = Connection.DISCONNECTED
    private var revision = -1
    private var eventCounter = 0L
    internal var eventId by mutableStateOf<Long?>(null)
        private set
    internal var stage by mutableStateOf<FallEventStage?>(null)
        private set
    internal var seconds by mutableIntStateOf(0)
        private set
    internal var awaitingResync by mutableStateOf(false)
        private set
    internal var phoneAction by mutableStateOf<String?>(null)
        private set
    internal var operationCount by mutableIntStateOf(0)
        private set
    internal var message by mutableStateOf("等待模拟手表事件")
        private set
    internal var contacts by mutableStateOf(loadContacts())
        private set
    internal var autoEscalationEnabled by mutableStateOf(preferences.getBoolean("auto-escalation-simulation", false))
        private set
    internal var draftContacts by mutableStateOf(contacts.ifEmpty { listOf(SosContact()) })
        private set
    internal var draftEnabled by mutableStateOf(autoEscalationEnabled)
    internal var settingsMessage by mutableStateOf("")
        private set
    internal var smsStage by mutableStateOf(SmsStage.NOT_SENT)
        private set
    internal var smsMessage by mutableStateOf("尚未发起短信")
        private set
    internal var smsSnapshot by mutableStateOf<SosMessage?>(null)
        private set
    internal var smsAttempts by mutableIntStateOf(0)
        private set
    internal var smsDemoResult by mutableStateOf(SmsDemoResult.SUCCESS)
        private set
    internal var shareMessage by mutableStateOf("")
        private set
    private var eventAutoSmsEnabled = false
    private var eventContactName = ""
    private var eventContactPhone = ""
    internal var eventContacts by mutableStateOf<List<SosContact>>(emptyList())
        private set
    internal var callStage by mutableStateOf(SosCallStage.NONE)
        private set
    internal var callIndex by mutableIntStateOf(-1)
        private set
    internal var callSeconds by mutableIntStateOf(0)
        private set
    private var eventSource = ""
    private var eventTime = ""
    private val store = SosEventStore(application)
    private data class Write(val event: SavedSosEvent, val afterSave: () -> Unit)
    private val writes = Channel<Write>(Channel.UNLIMITED)
    private var pendingWrites by mutableIntStateOf(0)
    internal var loading by mutableStateOf(true)
        private set
    internal var storageError by mutableStateOf<String?>(null)
        private set
    internal var history by mutableStateOf<List<SavedSosEvent>>(emptyList())
        private set
    internal val ready: Boolean get() = !loading && storageError == null && pendingWrites == 0

    init {
        scope.launch {
            try {
                history = withContext(Dispatchers.IO) { store.recent() }
                history.firstOrNull()?.let { restore(it) }
                loading = false
                // 将恢复出的未知结果先保存，之后才允许用户继续操作。
                if (eventId != null) persist()
                for (write in writes) {
                    if (storageError == null) {
                        withContext(Dispatchers.IO) { store.save(write.event) }
                        history = withContext(Dispatchers.IO) { store.recent() }
                        pendingWrites--
                        write.afterSave()
                    } else pendingWrites--
                }
            } catch (cancelled: CancellationException) {
                throw cancelled
            } catch (_: Exception) {
                storageError = "本地求助记录读取或保存失败，已暂停求助操作；请保留数据并检查存储。"
                loading = false
                countdownJob?.cancel()
                callJob?.cancel()
            }
        }
    }

    private fun restore(record: SavedSosEvent) {
        eventCounter = record.id
        eventId = record.id
        eventSource = record.source
        eventTime = record.time
        stage = record.stage
        eventBody = record.body
        eventAutoSmsEnabled = record.autoEnabled
        eventContactName = record.name
        eventContactPhone = record.phone
        eventContacts = record.contacts
        callIndex = record.callIndex
        callStage = record.callStage
        smsConsidered = record.considered
        smsStage = record.smsStage
        smsAttempts = record.attempts
        smsMessage = record.smsMessage
        phoneAction = record.phoneAction
        smsSnapshot = if (record.hasSmsSnapshot) SosMessage(record.name, record.phone, record.body) else null
        if (callStage == SosCallStage.DIALING) {
            callStage = SosCallStage.INTERRUPTED
            if (smsStage == SmsStage.NOT_SENT) {
                smsStage = SmsStage.BLOCKED
                smsMessage = "拨号流程中断，未提交群发短信"
            }
        }
        if (smsStage == SmsStage.SENDING) {
            smsStage = SmsStage.UNKNOWN
            smsConsidered = true
            smsMessage = "发送过程被中断，结果未知；不自动补发"
        } else if (stage == FallEventStage.REQUESTED && !smsConsidered) {
            smsConsidered = true
            smsStage = SmsStage.BLOCKED
            smsMessage = "恢复的求助尚未确认提交结果，需人工核查"
        }
        awaitingResync = stage != FallEventStage.CANCELLED
        message = if (awaitingResync) "已恢复本地记录，等待手表状态补同步" else "已恢复取消记录，不会发送"
    }

    private fun persist(afterSave: () -> Unit = {}) {
        val id = eventId ?: return
        val currentStage = stage ?: return
        if (storageError != null) return
        val record = SavedSosEvent(id, eventSource, eventTime, currentStage, eventBody,
            eventAutoSmsEnabled, eventContactName, eventContactPhone, smsConsidered,
            smsStage, smsAttempts, smsMessage, smsSnapshot != null, phoneAction,
            eventContacts, callStage, callIndex)
        pendingWrites++
        check(writes.trySend(Write(record, afterSave)).isSuccess)
    }

    internal val canStart: Boolean
        get() = ready && connection == Connection.SYNCED &&
            (eventId == null || stage == FallEventStage.CANCELLED || phoneAction != null ||
                smsAttempts > 0 || smsStage == SmsStage.BLOCKED || callStage == SosCallStage.ANSWERED) &&
            !awaitingResync && smsStage != SmsStage.SENDING && callStage != SosCallStage.DIALING
    internal val canConfirm: Boolean
        get() = ready && connection == Connection.SYNCED && !awaitingResync &&
            stage == FallEventStage.REQUESTED && phoneAction == null && callStage != SosCallStage.DIALING
    internal val canRetrySms: Boolean
        get() = ready && stage == FallEventStage.REQUESTED && !awaitingResync &&
            smsStage == SmsStage.FAILED && eventAutoSmsEnabled && smsSnapshot != null
    internal val shareBody: String get() = smsSnapshot?.body ?: eventBody
    internal val automaticEscalationForEvent: Boolean get() = eventAutoSmsEnabled
    internal val canSimulateCallResult: Boolean
        get() = ready && stage == FallEventStage.REQUESTED && callStage == SosCallStage.DIALING

    private fun loadContacts(): List<SosContact> {
        val stored = preferences.getString("ordered-contacts", null)
        if (stored != null) return runCatching { JSONArray(stored).decodeContacts() }.getOrDefault(emptyList())
        val legacy = SosContact(preferences.getString("name", "").orEmpty(), preferences.getString("phone", "").orEmpty())
        return listOf(legacy).filter { contactError(it.name, it.phone) == null }
    }

    internal fun editContact(index: Int, value: SosContact) {
        draftContacts = draftContacts.toMutableList().apply { this[index] = value }
    }

    internal fun addContact() {
        if (draftContacts.size < SOS_CONTACT_LIMIT) draftContacts = draftContacts + SosContact()
    }

    internal fun removeContact(index: Int) {
        draftContacts = draftContacts.filterIndexed { i, _ -> i != index }
    }

    internal fun moveContactUp(index: Int) {
        if (index > 0) draftContacts = draftContacts.toMutableList().apply {
            add(index - 1, removeAt(index))
        }
    }

    internal fun saveContact() {
        val updated = draftContacts.map { SosContact(it.name.trim(), it.phone.trim()) }
        if (draftEnabled && updated.isEmpty()) {
            settingsMessage = "请先添加至少一位紧急联系人"
            return
        }
        updated.forEachIndexed { i, contact ->
            val error = contactError(contact.name, contact.phone)
            if (error != null) {
                settingsMessage = "第 ${i + 1} 位：$error"
                return
            }
        }
        if (updated.distinctBy { it.phone.trimStart('+') }.size != updated.size) {
            settingsMessage = "紧急联系人号码不能重复"
            return
        }
        contacts = updated
        autoEscalationEnabled = draftEnabled
        preferences.edit().putString("ordered-contacts", updated.encodeContacts().toString())
            .putBoolean("auto-escalation-simulation", autoEscalationEnabled).apply()
        settingsMessage = "设置已保存，仅应用于新的求助事件"
    }

    internal fun fillDemoContact() {
        draftContacts = listOf(
            SosContact("演示联系人甲", "00000000001"),
            SosContact("演示联系人乙", "00000000002"),
            SosContact("演示联系人丙", "00000000003"),
        )
        settingsMessage = "已填入 3 位虚构联系人，请保存"
    }

    internal fun nextSmsResult() {
        if (smsStage != SmsStage.SENDING) {
            val values = SmsDemoResult.entries
            smsDemoResult = values[(smsDemoResult.ordinal + 1) % values.size]
        }
    }

    private fun beginEvent(source: String): Long {
        val id = ++eventCounter
        eventId = id
        phoneAction = null
        smsConsidered = false
        smsStage = SmsStage.NOT_SENT
        smsMessage = "尚未发起短信"
        smsSnapshot = null
        smsAttempts = 0
        callStage = SosCallStage.NONE
        callIndex = -1
        callSeconds = 0
        shareMessage = ""
        eventAutoSmsEnabled = autoEscalationEnabled
        eventContacts = contacts
        eventContactName = eventContacts.firstOrNull()?.name.orEmpty()
        eventContactPhone = eventContacts.firstOrNull()?.phone.orEmpty()
        eventSource = source
        val time = SimpleDateFormat("yyyy-MM-dd HH:mm:ss", Locale.CHINA).apply {
            timeZone = TimeZone.getTimeZone("Asia/Shanghai")
        }.format(Date())
        eventTime = time
        eventBody = "【模拟求助】智能手表佩戴者发起求助。来源：$source。事件 #$id；北京时间 $time。请联系佩戴者确认情况。"
        return id
    }

    internal fun connectionChanged(next: Connection, nextRevision: Int) {
        if (connection == next && revision == nextRevision) return
        countdownJob?.cancel()
        connection = next
        revision = nextRevision
        if (eventId != null && stage != FallEventStage.CANCELLED) {
            awaitingResync = true
            message = "连接已变化，手表事件状态待同步"
        }
    }

    internal fun simulateFall() {
        if (!canStart) return
        val id = beginEvent("疑似跌倒")
        stage = FallEventStage.COUNTDOWN
        seconds = 10
        message = "手机已收到模拟跌倒事件"
        persist()
        val deadline = SystemClock.elapsedRealtime() + 10_000L
        countdownJob = scope.launch {
            while (true) {
                val remaining = (deadline - SystemClock.elapsedRealtime()).coerceAtLeast(0)
                seconds = ((remaining + 999) / 1000).toInt()
                if (remaining == 0L) {
                    receiveEvent(id, FallEventStage.REQUESTED)
                    break
                }
                delay(200)
            }
        }
    }

    internal fun simulateManualSos() {
        if (!canStart) return
        val id = beginEvent("手表手动 SOS")
        stage = FallEventStage.COUNTDOWN
        receiveEvent(id, FallEventStage.REQUESTED)
    }

    internal fun simulateWatchCancel() {
        if (ready && connection == Connection.SYNCED && !awaitingResync && stage == FallEventStage.COUNTDOWN) {
            eventId?.let { receiveEvent(it, FallEventStage.CANCELLED) }
        }
    }

    private fun receiveEvent(id: Long, incoming: FallEventStage) {
        if (id != eventId || awaitingResync || stage == FallEventStage.CANCELLED ||
            incoming == stage || incoming == FallEventStage.COUNTDOWN) {
            message = "重复或旧事件已忽略"
            return
        }
        countdownJob?.cancel()
        stage = incoming
        seconds = 0
        message = if (incoming == FallEventStage.CANCELLED) "取消阶段已确认" else "手机已收到模拟求助事件"
        if (incoming == FallEventStage.REQUESTED) startEscalation(id, notify = true)
        else persist()
    }

    internal fun replayEvent() {
        if (!ready) return
        val incoming = if (stage == FallEventStage.CANCELLED) FallEventStage.REQUESTED else stage
        if (incoming != null) eventId?.let { receiveEvent(it, incoming) }
    }

    internal fun resynchronize(cancelled: Boolean) {
        if (!ready || connection != Connection.SYNCED || !awaitingResync || stage == FallEventStage.CANCELLED) return
        val previous = stage
        val id = eventId ?: return
        stage = if (cancelled) FallEventStage.CANCELLED else FallEventStage.REQUESTED
        seconds = 0
        awaitingResync = false
        message = "已收到模拟手表的最新事件阶段"
        if (!cancelled) {
            startEscalation(id, notify = previous == FallEventStage.COUNTDOWN)
        } else {
            callJob?.cancel()
            callSeconds = 0
            if (callStage == SosCallStage.DIALING) callStage = SosCallStage.CANCELLED
            persist()
        }
    }

    private fun startEscalation(id: Long, notify: Boolean) {
        if (smsConsidered) { persist(); return }
        smsConsidered = true
        if (!eventAutoSmsEnabled || eventContacts.isEmpty() ||
            eventContacts.any { contactError(it.name, it.phone) != null }) {
            smsStage = SmsStage.BLOCKED
            smsMessage = if (!eventAutoSmsEnabled) "事件发生时未启用自动拨号与短信（模拟）" else "事件发生时未配置有效联系人"
            persist { if (notify) SosNotification.show(getApplication(), id) }
            return
        }
        callStage = SosCallStage.DIALING
        callIndex = 0
        callSeconds = 30
        smsMessage = "等待第一位联系人接听"
        persist {
            if (notify) SosNotification.show(getApplication(), id)
            startCallTimer(id, 0)
        }
    }

    private fun startCallTimer(id: Long, index: Int) {
        if (eventId != id || callIndex != index || callStage != SosCallStage.DIALING) return
        callDeadline = SystemClock.elapsedRealtime() + 30_000L
        callJob = scope.launch {
            while (true) {
                val remaining = (callDeadline - SystemClock.elapsedRealtime()).coerceAtLeast(0)
                callSeconds = ((remaining + 999) / 1000).toInt()
                if (remaining == 0L) {
                    finishCall(id, index, answered = false)
                    break
                }
                delay(200)
            }
        }
    }

    internal fun simulateCallResult(id: Long, index: Int, answered: Boolean) {
        if (!canSimulateCallResult) return
        finishCall(id, index, answered && SystemClock.elapsedRealtime() < callDeadline)
    }

    private fun finishCall(id: Long, index: Int, answered: Boolean) {
        if (eventId != id || callIndex != index || callStage != SosCallStage.DIALING || storageError != null) return
        callJob?.cancel()
        callSeconds = 0
        if (answered) {
            callStage = SosCallStage.ANSWERED
            if (index == 0) smsMessage = "第一位已接听，未发起群发短信"
            persist()
            return
        }
        val nextIndex = index + 1
        val hasNext = nextIndex < eventContacts.size
        callStage = if (hasNext) SosCallStage.DIALING else SosCallStage.EXHAUSTED
        if (hasNext) {
            callIndex = nextIndex
            callSeconds = 30
        }
        val continueCalling: () -> Unit = {
            if (hasNext) startCallTimer(id, nextIndex)
        }
        if (index == 0) {
            // 第一位超时就提交整组短信，下一位的拨号计时无需等待短信回执。
            smsSnapshot = SosMessage(eventContactName, eventContactPhone, eventBody)
            submitSms(continueCalling)
        } else {
            persist(continueCalling)
        }
    }

    internal val smsRecipients: List<SosContact> get() = if (smsSnapshot == null) emptyList() else eventContacts

    private fun submitSms(onCommitted: () -> Unit = {}) {
        val id = eventId
        val result = smsDemoResult
        smsStage = SmsStage.SENDING
        smsAttempts++
        val recipients = smsRecipients.size
        smsMessage = "正在模拟向 $recipients 位联系人提交短信"
        // 已接收的求助由手机处理；后续 BLE 断线不撤销已提交的短信。
        persist {
          onCommitted()
          scope.launch {
            if (result == SmsDemoResult.WAIT_FOR_RESTART) return@launch
            delay(900)
            if (eventId != id) return@launch
            smsStage = when (result) {
                SmsDemoResult.SUCCESS -> SmsStage.SENT
                SmsDemoResult.TIMEOUT -> SmsStage.UNKNOWN
                else -> SmsStage.FAILED
            }
            smsMessage = result.label + "（$recipients 位联系人，未真实发送）"
            persist()
          }
        }
    }

    internal fun retrySms() {
        if (canRetrySms) submitSms()
    }

    internal fun shareResult(opened: Boolean) {
        shareMessage = if (opened) "已打开微信分享入口，请手动选择联系人发送；送达未知"
            else "微信未安装或无法接收分享，可复制求助内容后手动发送"
    }

    internal fun copied() { shareMessage = "求助内容已复制；请在微信中选择联系人并发送" }

    internal fun confirmAndOpen(action: String) {
        if (!canConfirm) return
        phoneAction = action
        operationCount++
        message = "已模拟打开${action}页"
        persist()
    }

    internal fun replayHistory(id: Long) {
        if (!ready || connection != Connection.SYNCED) return
        // 列表中的旧事件只用于重传验证，不能变成一个新事件。
        if (id == eventId) replayEvent() else receiveEvent(id, FallEventStage.REQUESTED)
    }

    override fun onCleared() {
        scope.cancel()
        super.onCleared()
    }
}
