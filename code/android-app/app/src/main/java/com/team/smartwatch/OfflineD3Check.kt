package com.team.smartwatch

import kotlinx.coroutines.coroutineScope
import kotlinx.coroutines.job
import kotlinx.coroutines.joinAll

// Fixed wire examples from the APP-03 D3 task specification. No GATT device is used here.
internal suspend fun checkOfflineD3(pendingCalibration: Boolean = false) = coroutineScope {
    val pairs = listOf(
        ("A5 5A 01 00 01 01 00 44 33 22 11 00 00 DD C6 CB A8" to
            "A5 5A 01 01 01 01 00 44 33 22 11 1E 00 00 00 01 02 03 04 05 06 07 08 09 0A 0B 01 00 00 00 1F 00 00 00 08 44 52 41 46 54 2D 30 31 F0 A5 DC 51"),
        ("A5 5A 01 00 02 02 00 44 33 22 11 00 00 FB FD C9 1F" to
            "A5 5A 01 01 02 02 00 44 33 22 11 2B 00 00 10 27 00 00 02 00 DF 00 6B 02 00 7B 00 00 00 14 00 00 00 02 17 0F 14 00 00 00 00 00 00 00 00 FF FF FF FF 00 00 00 FF FF FF FF D9 AA 62 FC"),
        ("A5 5A 01 00 03 03 00 44 33 22 11 04 00 00 DF 00 6B EA D2 18 DB" to
            "A5 5A 01 01 03 03 00 44 33 22 11 05 00 00 01 DF 00 6B 7E 9A BD 91"),
        ("A5 5A 01 00 02 04 00 44 33 22 11 00 00 7C F4 A6 D9" to
            "A5 5A 01 01 02 04 00 44 33 22 11 2B 00 00 F8 2A 00 00 02 01 DF 00 6B 02 00 7B 00 00 00 14 00 00 00 02 17 0F 14 00 00 00 00 00 00 00 00 FF FF FF FF 00 00 00 FF FF FF FF 67 D4 C9 84"),
    )
    var step = 0
    var elapsed = 0L
    var session = 0x11223344L
    var followupReply: (D3Frame) -> ByteArray? = { error("未安排后续 D3 应答") }

    fun decode(wire: ByteArray): D3Frame {
        var frame: D3Frame? = null
        D3FrameReader().feed(wire, elapsed) { frame = it }
        return checkNotNull(frame)
    }
    val now = 1_795_219_200_000L
    val expectedWrites = if (pendingCalibration) 2 else pairs.size
    lateinit var client: D3Client
    client = D3Client(write = { bytes ->
        val wire = if (step < expectedWrites) {
            val (request, response) = pairs[step++]
            check(bytes.serialHex() == request) { "D3 请求样例不匹配" }
            checkNotNull(serialPayload(response, hex = true))
        } else {
            val request = decode(bytes)
            followupReply(request)?.let { request.copy(response = true, payload = it).encode() }
        }
        if (wire != null) {
            for (offset in wire.indices step 16) {
                elapsed += 5
                client.receive(wire.copyOfRange(offset, minOf(offset + 16, wire.size)))
            }
        }
        true
    }, clock = { D3Timestamp(elapsed, now + elapsed) }, nextSessionId = { session++ }, responseTimeoutMs = 30L)
    val watch = D3Watch(this, client, { true }, pendingCalibration, {})
    watch.transportReady()
    coroutineContext.job.children.toList().joinAll()
    watch.transportReady()
    check(!watch.busy) { "同一连接不能重复启动自动校时" }

    val identity = checkNotNull(watch.identity)
    check(identity.uid == "000102030405060708090A0B" && identity.firmware == "DRAFT-01")
    val status = checkNotNull(watch.status)
    check(status.stepsScope == 0 && status.steps.value == 123L && status.voltage.value == 3863L)
    check(status.oxygen.state == D3DataState.UNSUPPORTED && status.oxygen.value == null)
    if (pendingCalibration) {
        check(watch.timeAcknowledged == null)
        check(watch.timeResult.orEmpty().contains("待确认"))
    } else {
        check(watch.timeAcknowledged == now / 1000L + 1)
        check(watch.timeResult.orEmpty().startsWith("自动校时 · 低延迟校时已验证"))
    }
    check(step == expectedWrites)
    if (pendingCalibration) return@coroutineScope

    val previous = checkNotNull(watch.snapshot)
    followupReply = { request ->
        check(request.command == D3Command.STATUS.wire && request.session == 0x11223344L)
        null
    }
    watch.autoRefresh()
    coroutineContext.job.children.toList().joinAll()
    check(watch.message.contains("超时") && watch.snapshot === previous)
    check(previous.isFresh(previous.receivedElapsedMs + 45_000L))
    elapsed = previous.receivedElapsedMs + 45_001L
    check(watch.snapshot?.isFresh(elapsed) == false)

    // An unready session can interrupt a refresh while the physical link stays open.
    followupReply = { byteArrayOf(4) }
    watch.autoRefresh()
    coroutineContext.job.children.toList().joinAll()
    check(watch.identity == null && watch.snapshot === previous)

    val infoPayload = decode(checkNotNull(serialPayload(pairs[0].second, hex = true))).payload
    val statusPayload = decode(checkNotNull(serialPayload(pairs[1].second, hex = true))).payload
    followupReply = { request ->
        check(request.session == 0x11223345L)
        when (request.command) {
            D3Command.INFO.wire -> infoPayload
            D3Command.STATUS.wire -> statusPayload
            else -> error("自动刷新不应执行校时")
        }
    }
    watch.autoRefresh()
    coroutineContext.job.children.toList().joinAll()
    check(watch.message == "已读取手表状态")
    check(watch.snapshot?.isFresh(elapsed) == true && watch.snapshot !== previous)

    followupReply = { null }
    watch.refresh()
    coroutineContext.job.children.toList().joinAll()
    check(watch.message.contains("超时") && watch.snapshot == null)
}
