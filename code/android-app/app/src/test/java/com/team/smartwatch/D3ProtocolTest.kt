package com.team.smartwatch

import kotlinx.coroutines.async
import kotlinx.coroutines.cancelAndJoin
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withTimeout
import kotlinx.coroutines.yield
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertThrows
import org.junit.Assert.assertTrue
import org.junit.Test

class D3ProtocolTest {
    @Test
    fun allDocumentedFramesRoundTrip() {
        val hex = listOf(
            "A5 5A 01 00 01 01 00 44 33 22 11 00 00 DD C6 CB A8",
            "A5 5A 01 01 01 01 00 44 33 22 11 1E 00 00 00 01 02 03 04 05 06 07 08 09 0A 0B 01 00 00 00 1F 00 00 00 08 44 52 41 46 54 2D 30 31 F0 A5 DC 51",
            "A5 5A 01 00 02 02 00 44 33 22 11 00 00 FB FD C9 1F",
            "A5 5A 01 01 02 02 00 44 33 22 11 2B 00 00 10 27 00 00 02 00 DF 00 6B 02 00 7B 00 00 00 14 00 00 00 02 17 0F 14 00 00 00 00 00 00 00 00 FF FF FF FF 00 00 00 FF FF FF FF D9 AA 62 FC",
            "A5 5A 01 00 03 03 00 44 33 22 11 04 00 00 DF 00 6B EA D2 18 DB",
            "A5 5A 01 01 03 03 00 44 33 22 11 05 00 00 00 DF 00 6B 1B FD 01 29",
            "A5 5A 01 00 03 04 00 44 33 22 11 04 00 00 00 00 00 B8 F1 35 F3",
            "A5 5A 01 01 03 04 00 44 33 22 11 01 00 02 8B CB E8 4A",
            "A5 5A 01 01 03 03 00 44 33 22 11 05 00 00 01 DF 00 6B 7E 9A BD 91",
            "A5 5A 01 00 02 04 00 44 33 22 11 00 00 7C F4 A6 D9",
            "A5 5A 01 01 02 04 00 44 33 22 11 2B 00 00 F8 2A 00 00 02 01 DF 00 6B 02 00 7B 00 00 00 14 00 00 00 02 17 0F 14 00 00 00 00 00 00 00 00 FF FF FF FF 00 00 00 FF FF FF FF 67 D4 C9 84",
        )
        val vectors = hex.map { checkNotNull(serialPayload(it, hex = true)) }
        val reader = D3FrameReader()
        val parsed = mutableListOf<D3Frame>()
        reader.feed(vectors.reduce(ByteArray::plus), 1, parsed::add)
        assertEquals(11, parsed.size)
        vectors.zip(parsed).forEach { (expected, actual) -> assertArrayEquals(expected, actual.encode()) }
        assertEquals(2, assertThrows(D3DeviceError::class.java) {
            d3StoredTime(parsed[7].payload, 1795219200L)
        }.code)
    }

    @Test
    fun acceptsEveryMaxFrameSplitAndRecoversFromMalformedStream() {
        val wire = D3Frame(true, 1, 1, 1, ByteArray(128) { it.toByte() }).encode()
        assertEquals(145, wire.size)
        for (split in 1 until wire.size) {
            val reader = D3FrameReader()
            val parsed = mutableListOf<D3Frame>()
            reader.feed(wire.copyOfRange(0, split), 0, parsed::add)
            assertTrue(parsed.isEmpty())
            reader.feed(wire.copyOfRange(split, wire.size), 5, parsed::add)
            assertArrayEquals(wire, parsed.single().encode())
        }
        val invalidHeader = wire.copyOf(13).apply { this[11] = 129.toByte() }
        val badCrc = wire.copyOf().apply { this[lastIndex] = (last().toInt() xor 1).toByte() }
        val received = mutableListOf<D3Frame>()
        D3FrameReader().feed(invalidHeader + badCrc + wire, 8, received::add)
        assertArrayEquals(wire, received.single().encode())
    }

    @Test
    fun stalePartialPrefixExpiresAtTheSpecifiedBoundary() {
        val wire = D3Frame(true, 1, 1, 1, byteArrayOf(0)).encode()
        val reader = D3FrameReader()
        val accepted = mutableListOf<D3Frame>()
        reader.feed(wire.copyOf(1), 0, accepted::add)
        assertEquals(1, reader.remainingMillis(1000))
        reader.feed(wire.copyOfRange(1, wire.size), 1001, accepted::add)
        assertTrue(accepted.isEmpty())
        reader.feed(wire, 1002, accepted::add)
        assertArrayEquals(wire, accepted.single().encode())
    }

    @Test
    fun matchesEarlyReplyAndRetransmitsExactlyTheSameBytes() = runBlocking {
        var writes = 0
        var clock = 0L
        lateinit var client: D3Client
        var first: ByteArray? = null
        client = D3Client(write = { encoded ->
            writes++
            if (writes == 1) {
                first = encoded.copyOf()
                client.receive(D3Frame(true, 1, 1, 2, byteArrayOf(82)).encode())
                client.receive(D3Frame(true, 2, 1, 1, byteArrayOf(83)).encode())
                client.receive(D3Frame(true, 1, 2, 1, byteArrayOf(84)).encode())
            } else {
                assertArrayEquals(first, encoded)
                clock += 3200L
                client.receive(D3Frame(true, 1, 1, 1, byteArrayOf(0)).encode())
            }
            true
        }, clock = { D3Timestamp(clock, clock + 1_795_219_200_000L) },
            nextSessionId = { 1L }, responseTimeoutMs = 20)
        client.newSession()
        val reply = client.request(D3Command.INFO)
        assertTrue(reply.retransmitted)
        assertArrayEquals(byteArrayOf(0), reply.payload)
        assertEquals(2, writes)
    }

    @Test
    fun disconnectedRequestDoesNotRetryAndNewClientIgnoresOldSession() = runBlocking {
        val written = CompletableDeferred<Unit>()
        var writes = 0
        val clock = { D3Timestamp(1L, 1_795_219_200_000L) }
        lateinit var previous: D3Client
        previous = D3Client({
            writes++
            if (writes == 1) previous.receive(D3Frame(true, 1, 1, 1, byteArrayOf(0)).encode())
            else written.complete(Unit)
            true
        }, clock, { 1L })
        previous.newSession()
        previous.request(D3Command.INFO)
        val request = async { previous.request(D3Command.SET_TIME) { d3TimePayload(it.unixMs / 1000L) } }
        written.await()
        request.cancelAndJoin()
        assertEquals(2, writes)
        lateinit var next: D3Client
        next = D3Client({
            next.receive(D3Frame(true, 1, 1, 1, byteArrayOf(82)).encode())
            next.receive(D3Frame(true, 1, 1, 2, byteArrayOf(0)).encode())
            true
        }, clock, { 2L })
        next.newSession()
        assertArrayEquals(byteArrayOf(0), next.request(D3Command.INFO).payload)
    }

    @Test
    fun calibrationRequiresFreshStatusAndBothTimingWindows() {
        val payload = checkNotNull(serialPayload("A5 5A 01 01 02 04 00 44 33 22 11 2B 00 00 F8 2A 00 00 02 01 DF 00 6B 02 00 7B 00 00 00 14 00 00 00 02 17 0F 14 00 00 00 00 00 00 00 00 FF FF FF FF 00 00 00 FF FF FF FF 67 D4 C9 84", hex = true)).copyOfRange(13, 56)
        val fresh = d3Status(payload)
        val utc = 1_795_219_201_000L
        val set = D3Reply(byteArrayOf(), D3Timestamp(0, utc), D3Timestamp(200, utc + 200), false)
        val read = D3Reply(byteArrayOf(), D3Timestamp(210, utc + 210), D3Timestamp(300, utc + 300), false)
        assertEquals(D3TimeCheck.VERIFIED, d3TimeVerification(set, read, fresh).outcome)
        assertEquals(D3TimeCheck.REVIEW_DELAY, d3TimeVerification(set.copy(retransmitted = true), read, fresh).outcome)
        assertEquals(D3TimeCheck.REVIEW_DELAY, d3TimeVerification(set, read.copy(received = D3Timestamp(1211, utc + 1211)), fresh).outcome)
        assertEquals(D3TimeCheck.REVIEW_DELAY, d3TimeVerification(set, read.copy(received = D3Timestamp(300, utc + 300, 1)), fresh).outcome)
        assertEquals(D3TimeCheck.DIFFERENT, d3TimeVerification(set, read, fresh.copy(time = fresh.time.copy(value = 1795219204L))).outcome)
        assertEquals(D3TimeCheck.UNKNOWN, d3TimeVerification(set, read,
            fresh.copy(time = D3Reading(D3DataState.MISSING, null, null))).outcome)
    }

    @Test
    fun fullOfflineExchangeUsesSpecificationSamples() = runBlocking { checkOfflineD3() }

    @Test
    fun liveSnapshotTracksVerifiedReadbacksAndClearsOnFailureOrDisconnect() = runBlocking {
        val wall = 1_795_219_200_000L
        var elapsed = 0L
        var clockValue = wall / 1_000L - 20L
        var failRead = false
        var allowWrite: CompletableDeferred<Unit>? = null
        val commands = mutableListOf<Int>()
        val reader = D3FrameReader()
        lateinit var client: D3Client
        client = D3Client(write = { bytes ->
            allowWrite?.await()
            reader.feed(bytes, elapsed) { request ->
                commands.add(request.command)
                val payload = when (request.command) {
                    D3Command.INFO.wire -> ByteArray(23).apply {
                        this[17] = 7
                        this[21] = 1
                        this[22] = 'X'.code.toByte()
                    }
                    D3Command.SET_TIME.wire -> {
                        val time = Math.floorDiv(wall + elapsed, 1_000L)
                        assertArrayEquals(d3TimePayload(time), request.payload)
                        clockValue = time
                        byteArrayOf(0) + request.payload
                    }
                    else -> if (failRead) byteArrayOf(5) else ByteArray(43).apply {
                        this[5] = D3DataState.VALID.ordinal.toByte()
                        d3TimePayload(clockValue).copyInto(this, 6)
                        this[10] = D3DataState.VALID.ordinal.toByte()
                        this[12] = 123
                        this[20] = D3DataState.VALID.ordinal.toByte()
                        this[21] = 0x17
                        this[22] = 0x0f
                    }
                }
                elapsed += 20L
                client.receive(request.copy(response = true, payload = payload).encode())
            }
            true
        }, clock = { D3Timestamp(elapsed, wall + elapsed) }, nextSessionId = { 1L })
        val watch = D3Watch(this, client, { true }, false, {})
        suspend fun awaitOperation() = withTimeout(1_000L) { while (watch.busy) yield() }

        watch.refresh()
        awaitOperation()
        val original = checkNotNull(watch.snapshot)
        assertEquals(wall + 40L, original.receivedAtMs)
        assertEquals(123L, original.status.steps.value)
        assertEquals(0, original.status.stepsScope)
        assertEquals(3863L, original.status.voltage.value)
        assertNull(original.status.percentage.value)

        val pendingWrite = CompletableDeferred<Unit>()
        allowWrite = pendingWrite
        watch.autoRefresh()
        yield()
        assertTrue(watch.busy)
        assertEquals(original, watch.snapshot)
        watch.autoRefresh()
        watch.calibrate()
        pendingWrite.complete(Unit)
        allowWrite = null
        awaitOperation()
        assertEquals(listOf(1, 2, 2), commands)
        assertEquals(wall + 60L, watch.snapshot?.receivedAtMs)

        watch.calibrate()
        awaitOperation()
        assertEquals(listOf(1, 2, 2, 3, 2), commands)
        val calibrated = checkNotNull(watch.snapshot)
        assertEquals(wall + 100L, calibrated.receivedAtMs)
        assertEquals(wall / 1_000L, calibrated.status.time.value)
        assertTrue(watch.timeResult.orEmpty().contains("已验证"))

        failRead = true
        watch.autoRefresh()
        awaitOperation()
        assertNull(watch.snapshot)
        failRead = false
        watch.refresh()
        awaitOperation()
        assertEquals(3863L, watch.snapshot?.status?.voltage?.value)
        watch.close()
        watch.autoRefresh()
        assertNull(watch.snapshot)
        assertFalse(watch.canSetTime)
    }

    @Test
    fun allD3FaultControlsExerciseTheExpectedPath() = runBlocking {
        val passed = mutableListOf<String>()
        checkOfflineD3Faults(passed::add)
        assertEquals(D3Fault.entries.size - 1, passed.size)
    }

    @Test
    fun freshSequenceStartsWithoutAnUnfinishedOlderReply() = runBlocking {
        lateinit var client: D3Client
        var requests = 0
        client = D3Client({
            requests++
            val reply = if (requests == 1) D3Frame(true, 1, 1, 7L, byteArrayOf(0))
                else D3Frame(true, 2, 2, 7L, byteArrayOf(0))
            client.receive(reply.encode())
            true
        }, { D3Timestamp(10, 1_795_219_200_000L) }, { 7L }, responseTimeoutMs = 30)
        client.newSession()
        client.request(D3Command.INFO)
        val oldPrefix = D3Frame(true, 1, 1, 7L, ByteArray(128)).encode().copyOf(13)
        client.receive(oldPrefix)
        assertArrayEquals(byteArrayOf(0), client.request(D3Command.STATUS).payload)
        assertEquals(2, requests)
    }

    @Test
    fun includesExactlyOneSecondAndTwoSecondsAtTheirBoundaries() {
        val utc = 1_795_219_200_000L
        val valid = D3Reading(D3DataState.VALID, 1_795_219_204L, null)
        val missing = D3Reading(D3DataState.MISSING, null, null)
        val status = D3Status(0, valid, missing, 0, missing, missing, missing, missing)
        fun time(ms: Long) = D3Timestamp(ms, utc + ms)
        fun reply(start: Long, end: Long) = D3Reply(byteArrayOf(), time(start), time(end), false)
        val set = reply(0, 1000)
        val read = reply(1000, 2000)
        assertEquals(D3TimeCheck.VERIFIED, d3TimeVerification(set, read, status).outcome)
        assertEquals(D3TimeCheck.REVIEW_DELAY, d3TimeVerification(reply(0, 1001), reply(1001, 2001), status).outcome)
        assertEquals(D3TimeCheck.REVIEW_DELAY, d3TimeVerification(set, reply(1000, 2001), status).outcome)
        val tooFar = status.copy(time = valid.copy(value = 1_795_219_205L))
        assertEquals(D3TimeCheck.DIFFERENT, d3TimeVerification(set, read, tooFar).outcome)
    }
}
