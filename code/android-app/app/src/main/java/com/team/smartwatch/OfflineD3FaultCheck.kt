package com.team.smartwatch

import kotlin.math.abs

/** Drives the same D3 receive and retry logic on an in-process transport. */
internal suspend fun checkOfflineD3Faults(report: (String) -> Unit) {
    for (fault in D3Fault.entries.filter { it != D3Fault.NONE }) {
        val start = System.nanoTime()
        val utc = 1_795_219_200L
        val writes = mutableListOf<ByteArray>()
        var disconnected = 0
        lateinit var client: D3Client
        fun clock(): D3Timestamp {
            val elapsed = (System.nanoTime() - start) / 1_000_000L
            return D3Timestamp(elapsed, utc * 1000L + elapsed)
        }
        client = D3Client(
            write = { bytes ->
                if (bytes[4].toInt() == D3Command.INFO.wire) {
                    client.receive(D3Frame(true, 1, 1, 7L, byteArrayOf(0)).encode())
                } else {
                    writes.add(bytes.copyOf())
                    if (bytes.size == 21) {
                        val payload = byteArrayOf(0) + d3TimePayload(utc)
                        val reply = D3Frame(true, 3, 2, 7L, payload).encode()
                        client.receive(reply.copyOfRange(0, 16))
                        client.receive(reply.copyOfRange(16, reply.size))
                    }
                }
                true
            },
            clock = ::clock,
            nextSessionId = { 7L },
            responseTimeoutMs = 30L,
            testDisconnect = { disconnected++ },
        )
        client.newSession()
        client.request(D3Command.INFO)
        val result = runCatching { client.request(D3Command.SET_TIME, fault) { d3TimePayload(utc) } }
        val reply = result.getOrNull()
        when (fault) {
            D3Fault.DISCONNECT_BEFORE_RESPONSE, D3Fault.DISCONNECT_AFTER_FIRST_CHUNK -> {
                val sentBytes = if (fault == D3Fault.DISCONNECT_BEFORE_RESPONSE) 21 else 17
                check(disconnected == 1 && writes.single().size == sentBytes && result.exceptionOrNull() is D3WriteFailed)
            }
            D3Fault.PARTIAL_RESPONSE -> {
                check(writes.size == 2 && writes.first().contentEquals(writes.last()))
                check(reply != null && reply.retransmitted && reply.elapsedMs >= 1_001L)
                check(d3StoredTime(reply.payload, utc) == utc)
            }
            D3Fault.DROP_RESPONSES -> {
                check(writes.size == 3 && writes.all { it.contentEquals(writes.first()) })
                check(result.exceptionOrNull() is D3ResponseTimeout)
            }
            D3Fault.DELAY_RESPONSE, D3Fault.CLOCK_CHANGE -> {
                checkNotNull(reply)
                check(writes.size == 1 && d3StoredTime(reply.payload, utc) == utc)
                if (fault == D3Fault.DELAY_RESPONSE) check(reply.elapsedMs >= 1_200L)
                val read = D3Reply(byteArrayOf(), reply.received, reply.received, false)
                val valid = D3Reading(D3DataState.VALID, utc, null)
                val missing = D3Reading(D3DataState.MISSING, null, null)
                val status = D3Status(0, valid, missing, 0, missing, missing, missing, missing)
                check(d3TimeVerification(reply, read, status).outcome == D3TimeCheck.REVIEW_DELAY)
                if (fault == D3Fault.CLOCK_CHANGE) check(abs(reply.received.wallRevision - reply.sent.wallRevision) == 1)
            }
            D3Fault.NONE -> error("需要具体故障场景")
        }
        report("D3 异常演练通过 · ${fault.label}")
    }
}
