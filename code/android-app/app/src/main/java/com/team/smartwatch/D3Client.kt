package com.team.smartwatch

import java.security.SecureRandom
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.delay
import kotlinx.coroutines.withTimeoutOrNull

internal data class D3Timestamp(val elapsedMs: Long, val unixMs: Long, val wallRevision: Int = 0)

internal data class D3Reply(
    val payload: ByteArray, val sent: D3Timestamp, val received: D3Timestamp, val retransmitted: Boolean,
) {
    val elapsedMs get() = received.elapsedMs - sent.elapsedMs
    val lowLatency get() = !retransmitted && elapsedMs in 0L..1_000L
}

internal class D3WriteFailed : IllegalStateException("BLE 写入失败，请重新连接")
internal class D3ResponseTimeout : IllegalStateException("等待手表应答超时")

/** One instance per GATT connection. Its caller and the RX callback run on the same dispatcher. */
internal class D3Client(
    private val write: suspend (ByteArray) -> Boolean,
    private val clock: () -> D3Timestamp,
    private val nextSessionId: () -> Long = { generateD3SessionId() },
    private val responseTimeoutMs: Long = 2_000L,
    private val diagnostic: ((String) -> Unit)? = null,
    private val testDisconnect: ((String) -> Unit)? = null,
) {
    private data class Pending(
        val frame: D3Frame, val reply: CompletableDeferred<TimedFrame>, val fault: D3Fault,
        var attempt: Int = 0, var truncated: Boolean = false,
    )
    private data class TimedFrame(val frame: D3Frame, val received: D3Timestamp)

    private val reader = D3FrameReader()
    private var pending: Pending? = null
    private var session = 0L
    private var sequence = 0
    val remainingSequences get() = 65_535 - sequence
    val needsIdentity get() = session == 0L || sequence == 65_535

    fun trace(message: String, elapsedMs: Long? = null) {
        diagnostic?.invoke("${elapsedMs ?: clock().elapsedMs} ms · $message")
    }

    fun newSession() {
        check(pending == null)
        val next = nextSessionId()
        require(next in 1L..0xffffffffL && next != session)
        session = next
        sequence = 0
        reader.clear()
    }

    fun receive(bytes: ByteArray, received: D3Timestamp = clock()) {
        var testPrefix: ByteArray? = null
        reader.feed(bytes, received.elapsedMs) { frame ->
            val expected = pending
            if (expected != null && frame.response && frame.session == expected.frame.session &&
                frame.sequence == expected.frame.sequence && frame.command == expected.frame.command &&
                !expected.reply.isCompleted) {
                when {
                    expected.fault == D3Fault.PARTIAL_RESPONSE && expected.attempt == 0 -> {
                        if (!expected.truncated) {
                            expected.truncated = true
                            testPrefix = frame.encode().copyOf(12)
                            trace("复现：首条校时应答只保留前 12 字节", received.elapsedMs)
                        }
                    }
                    expected.fault == D3Fault.DROP_RESPONSES ||
                        expected.fault == D3Fault.DISCONNECT_BEFORE_RESPONSE ||
                        expected.fault == D3Fault.DISCONNECT_AFTER_FIRST_CHUNK ->
                        trace("复现：屏蔽校时应答 seq ${frame.sequence}", received.elapsedMs)
                    else -> {
                        trace("RX 命令 ${frame.command} · seq ${frame.sequence}", received.elapsedMs)
                        expected.reply.complete(TimedFrame(frame, received))
                    }
                }
            }
        }
        testPrefix?.let {
            reader.clear()
            reader.feed(it, received.elapsedMs) { error("调试前缀不应构成完整帧") }
        }
    }

    suspend fun request(
        command: D3Command,
        fault: D3Fault = D3Fault.NONE,
        payloadAtSend: (D3Timestamp) -> ByteArray = { byteArrayOf() },
    ): D3Reply {
        check(!needsIdentity && pending == null)
        check(sequence != 0 || command == D3Command.INFO)
        require(fault == D3Fault.NONE || (command == D3Command.SET_TIME && testDisconnect != null))
        // A new sequence has no valid bytes yet. A prefix from an earlier reply must not mask it.
        reader.clear()
        val sent = clock()
        val payload = payloadAtSend(sent)
        val frame = D3Frame(false, command.wire, ++sequence, session, payload)
        val encoded = frame.encode()
        val reply = CompletableDeferred<TimedFrame>()
        val operation = Pending(frame, reply, fault)
        pending = operation
        try {
            repeat(3) { attempt ->
                operation.attempt = attempt
                val outgoing = if (fault == D3Fault.DISCONNECT_AFTER_FIRST_CHUNK) encoded.serialChunkAt(0) else encoded
                trace("TX ${command.name} · session ${session.toString(16)} · seq ${frame.sequence} · 尝试 ${attempt + 1} · ${outgoing.size} B")
                if (!write(outgoing)) throw D3WriteFailed()
                if (fault == D3Fault.DISCONNECT_BEFORE_RESPONSE || fault == D3Fault.DISCONNECT_AFTER_FIRST_CHUNK) {
                    testDisconnect?.invoke("故障演练：${fault.label}，已关闭 BLE 连接")
                    throw D3WriteFailed()
                }
                var received = withTimeoutOrNull(responseTimeoutMs) { reply.await() }
                if (attempt < 2) {
                    while (received == null) {
                        val remaining = reader.remainingMillis(clock().elapsedMs)
                        if (remaining == 0L) break
                        received = withTimeoutOrNull(remaining) { reply.await() }
                    }
                    if (received == null && reply.isCompleted) received = reply.await()
                }
                if (received != null) {
                    val arrival = when (fault) {
                        D3Fault.DELAY_RESPONSE -> {
                            val releaseAt = sent.elapsedMs + 1_200L
                            var remaining = releaseAt - clock().elapsedMs
                            while (remaining > 0L) {
                                delay(remaining)
                                remaining = releaseAt - clock().elapsedMs
                            }
                            clock().also { trace("复现：交付延后应答", it.elapsedMs) }
                        }
                        D3Fault.CLOCK_CHANGE -> received.received.copy(wallRevision = sent.wallRevision + 1)
                            .also { trace("复现：标记手机时钟跳变", it.elapsedMs) }
                        else -> received.received
                    }
                    return D3Reply(received.frame.payload, sent, arrival, attempt > 0)
                }
                trace("未等到完整应答 · seq ${frame.sequence}")
            }
            throw D3ResponseTimeout()
        } finally {
            pending = null
            reply.cancel()
        }
    }
}

private val d3Random = SecureRandom()
private var lastD3Session = 0L
@Synchronized
private fun generateD3SessionId(): Long {
    var next: Long
    do { next = d3Random.nextInt().toLong() and 0xffffffffL } while (next == 0L || next == lastD3Session)
    lastD3Session = next
    return next
}

internal enum class D3TimeCheck { VERIFIED, REVIEW_DELAY, DIFFERENT, UNKNOWN }
internal data class D3TimeVerification(val outcome: D3TimeCheck, val offsetSeconds: Long?)

internal fun d3TimeVerification(set: D3Reply, read: D3Reply, status: D3Status): D3TimeVerification {
    val watch = status.time.value ?: return D3TimeVerification(D3TimeCheck.UNKNOWN, null)
    val offset = watch - Math.floorDiv(read.received.unixMs, 1000L)
    val elapsed = read.received.elapsedMs - set.sent.elapsedMs
    val wallElapsed = read.received.unixMs - set.sent.unixMs
    val wallChanged = listOf(set.received, read.sent, read.received).any { it.wallRevision != set.sent.wallRevision } ||
        kotlin.math.abs(wallElapsed - elapsed) > 100L
    val outcome = when {
        !set.lowLatency || !read.lowLatency || wallChanged -> D3TimeCheck.REVIEW_DELAY
        kotlin.math.abs(offset) > 2L -> D3TimeCheck.DIFFERENT
        else -> D3TimeCheck.VERIFIED
    }
    return D3TimeVerification(outcome, offset)
}
