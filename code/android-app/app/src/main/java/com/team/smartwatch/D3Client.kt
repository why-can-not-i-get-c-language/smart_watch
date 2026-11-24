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
) {
    private data class Pending(val frame: D3Frame, val reply: CompletableDeferred<TimedFrame>)
    private data class TimedFrame(val frame: D3Frame, val received: D3Timestamp)

    private val reader = D3FrameReader()
    private var pending: Pending? = null
    private var session = 0L
    private var sequence = 0
    val remainingSequences get() = 65_535 - sequence
    val needsIdentity get() = session == 0L || sequence == 65_535

    fun newSession() {
        check(pending == null)
        val next = nextSessionId()
        require(next in 1L..0xffffffffL && next != session)
        session = next
        sequence = 0
        reader.clear()
    }

    fun receive(bytes: ByteArray, received: D3Timestamp = clock()) {
        reader.feed(bytes, received.elapsedMs) { frame ->
            val expected = pending
            if (expected != null && frame.response && frame.session == expected.frame.session &&
                frame.sequence == expected.frame.sequence && frame.command == expected.frame.command) {
                expected.reply.complete(TimedFrame(frame, received))
            }
        }
    }

    suspend fun request(command: D3Command, payloadAtSend: (D3Timestamp) -> ByteArray = { byteArrayOf() }): D3Reply {
        check(!needsIdentity && pending == null)
        check(sequence != 0 || command == D3Command.INFO)
        val sent = clock()
        val frame = D3Frame(false, command.wire, ++sequence, session, payloadAtSend(sent))
        val encoded = frame.encode()
        val reply = CompletableDeferred<TimedFrame>()
        pending = Pending(frame, reply)
        try {
            repeat(3) { attempt ->
                if (!write(encoded)) throw D3WriteFailed()
                var received = withTimeoutOrNull(responseTimeoutMs) { reply.await() }
                if (attempt < 2) {
                    while (received == null) {
                        val remaining = reader.remainingMillis(clock().elapsedMs)
                        if (remaining == 0L) break
                        received = withTimeoutOrNull(remaining) { reply.await() }
                    }
                    if (received == null && reply.isCompleted) received = reply.await()
                }
                if (received != null) return D3Reply(received.frame.payload, sent, received.received, attempt > 0)
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
