package com.team.smartwatch

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch

private const val LIVE_SNAPSHOT_MAX_AGE_MS = 45_000L

internal data class D3Snapshot(
    val status: D3Status,
    val receivedAtMs: Long,
    val receivedElapsedMs: Long,
) {
    fun isFresh(nowElapsedMs: Long): Boolean =
        nowElapsedMs - receivedElapsedMs in 0L..LIVE_SNAPSHOT_MAX_AGE_MS
}

/** D3 state is scoped to the ready serial channel. Screens only display verified replies. */
internal class D3Watch(
    private val scope: CoroutineScope,
    private val client: D3Client,
    private val available: () -> Boolean,
    unconfirmedBeforeConnect: Boolean,
    private val onUnconfirmedChanged: (Boolean) -> Unit,
    val diagnostics: D3CalibrationDiagnostics? = null,
) {
    private var job: Job? = null
    private var connected = true
    private var initialized = false
    private var unconfirmed = unconfirmedBeforeConnect
    private var rawUsed = false
    var busy by mutableStateOf(false)
        private set
    var identity by mutableStateOf<D3Identity?>(null)
        private set
    var snapshot by mutableStateOf<D3Snapshot?>(null)
        private set
    val status: D3Status? get() = snapshot?.status
    var message by mutableStateOf("等待 BLE 串口就绪")
        private set
    var timeResult by mutableStateOf<String?>(
        if (unconfirmedBeforeConnect) "上次校时结果待确认。重连后读取手表时间" else null,
    )
        private set
    var timeAcknowledged by mutableStateOf<Long?>(null)
        private set
    val canRefresh: Boolean get() = connected && available() && !busy
    val canSetTime: Boolean get() = canRefresh && !unconfirmed && status != null &&
        identity?.supports(D3Command.SET_TIME) == true

    fun transportReady() {
        if (initialized || !canRefresh) return
        initialized = true
        perform {
            // 若上次写入结果未知，本次先读回供用户确认。
            val verifyPreviousCalibration = unconfirmed
            waitForQuietLink()
            queryStatus()
            if (!verifyPreviousCalibration && connected && available() &&
                identity?.supports(D3Command.SET_TIME) == true
            ) {
                calibrateTime(automatic = true)
            }
        }
    }

    fun refresh() = perform {
        if (rawUsed) waitForQuietLink()
        queryStatus()
    }

    fun autoRefresh() {
        if (rawUsed || unconfirmed || identity?.supports(D3Command.STATUS) != true) return
        perform { queryStatus(keepSnapshot = true) }
    }

    private suspend fun waitForQuietLink() {
        // The phone cannot see bytes still queued on the UART bridge, including after process death.
        message = "等待串口静默（5.5 秒）"
        client.trace("开始 5500 ms 恢复静默")
        delay(5_500L)
        client.trace("恢复静默结束")
        rawUsed = false
    }

    private suspend fun ensureIdentity() {
        if (identity != null && !client.needsIdentity) return
        identity = null
        snapshot = null
        client.newSession()
        message = "正在读取设备信息"
        val reply = client.request(D3Command.INFO)
        identity = d3Identity(reply.payload)
    }

    private suspend fun queryStatus(keepSnapshot: Boolean = false): Pair<D3Reply, D3Status> {
        ensureIdentity()
        require(identity?.supports(D3Command.STATUS) == true) { "手表未声明状态查询能力" }
        message = "正在查询手表状态"
        if (!keepSnapshot) snapshot = null
        val reply = client.request(D3Command.STATUS)
        val fresh = d3Status(reply.payload)
        snapshot = D3Snapshot(fresh, reply.received.unixMs, reply.received.elapsedMs)
        message = "已读取手表状态"
        if (unconfirmed) {
            setUnconfirmed(false)
            val offset = fresh.time.value?.minus(Math.floorDiv(reply.received.unixMs, 1000L))
            timeResult = when {
                offset == null -> "手表当前时间不可用，可重新发起校时"
                kotlin.math.abs(offset) <= 2L -> "当前时间已核对（偏差 $offset 秒）。上次校时结果待确认"
                else -> "上次校时结果待确认。已读回当前时间，偏差 $offset 秒，可重新校时"
            }
        }
        return reply to fresh
    }

    fun calibrate() {
        if (!canSetTime) return
        perform { calibrateTime(automatic = false) }
    }

    private suspend fun calibrateTime(automatic: Boolean) {
        // If there is only one sequence left, start a new session for both calibration requests.
        if (client.remainingSequences < 2) {
            identity = null
            ensureIdentity()
            require(identity?.supports(D3Command.SET_TIME) == true &&
                identity?.supports(D3Command.STATUS) == true) { "手表不支持校时或读回" }
        }
        timeAcknowledged = null
        val prefix = if (automatic) "自动校时 · " else ""
        timeResult = prefix + "正在发送手机当前时间"
        var requested = 0L
        try {
            val fault = if (automatic) D3Fault.NONE else diagnostics?.consume() ?: D3Fault.NONE
            val set = client.request(D3Command.SET_TIME, fault) { sent ->
                requested = Math.floorDiv(sent.unixMs, 1000L)
                d3TimePayload(requested).also { setUnconfirmed(true) }
            }
            timeAcknowledged = d3StoredTime(set.payload, requested)
            timeResult = prefix + "设备已应答，正在读取当前时间"
            val (read, fresh) = queryStatus()
            val report = d3TimeVerification(set, read, fresh)
            client.trace("校时时序 t0=${set.sent.elapsedMs} t1=${set.received.elapsedMs} " +
                "t2=${read.sent.elapsedMs} t3=${read.received.elapsedMs}")
            timeResult = prefix + when (report.outcome) {
                D3TimeCheck.VERIFIED -> "低延迟校时已验证（偏差 ${report.offsetSeconds} 秒）"
                D3TimeCheck.REVIEW_DELAY -> "当前时间已核对。校时用时较长或手机时间有变化，待复核（偏差 ${report.offsetSeconds} 秒）"
                D3TimeCheck.DIFFERENT -> "读回偏差 ${report.offsetSeconds} 秒，超过 2 秒"
                D3TimeCheck.UNKNOWN -> "校时结果待确认，手表未返回有效时间"
            }
        } catch (cancelled: CancellationException) {
            throw cancelled
        } catch (error: RuntimeException) {
            if (unconfirmed) {
                timeResult = prefix + "校时结果待确认。${error.message.orEmpty()}"
                if (connected && available() && error !is D3WriteFailed) {
                    try {
                        queryStatus()
                    } catch (cancelled: CancellationException) {
                        throw cancelled
                    } catch (readError: RuntimeException) {
                        if (readError is D3DeviceError && readError.code == 4) identity = null
                        message = "当前时间未读到，请查询状态或重连"
                    }
                }
            } else {
                timeResult = prefix + "校时未开始。${error.message.orEmpty()}"
            }
        }
    }

    fun beforeRawOperation(): Boolean {
        if (!canRefresh) return false
        identity = null
        snapshot = null
        rawUsed = true
        message = "正在使用原始串口通道，可重新读取 D3 状态"
        return true
    }

    fun close() {
        connected = false
        job?.cancel()
        diagnostics?.select(D3Fault.NONE)
        client.trace(if (unconfirmed) "连接关闭 · 校时结果待确认" else "连接关闭")
        identity = null
        snapshot = null
        message = "BLE 已断开"
        if (unconfirmed) timeResult = "校时结果待确认。请重连后读取手表时间"
        busy = false
    }

    private fun setUnconfirmed(value: Boolean) {
        unconfirmed = value
        onUnconfirmedChanged(value)
    }

    private fun perform(operation: suspend () -> Unit) {
        if (!canRefresh) return
        busy = true
        job = scope.launch {
            try {
                operation()
            } catch (cancelled: CancellationException) {
                throw cancelled
            } catch (error: RuntimeException) {
                snapshot = null
                if (error is D3DeviceError && error.code == 4) identity = null
                if (connected) message = error.message ?: "D3 请求失败"
            } finally {
                busy = false
            }
        }
    }
}
