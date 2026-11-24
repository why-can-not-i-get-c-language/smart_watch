package com.team.smartwatch

import java.util.zip.CRC32

internal const val D3_MAX_PAYLOAD = 128
private const val HEADER_BYTES = 13
private const val CRC_BYTES = 4
private const val MAX_FRAME_BYTES = HEADER_BYTES + D3_MAX_PAYLOAD + CRC_BYTES
internal const val D3_MIN_UTC = 946684800L
internal const val D3_MAX_UTC = 4102444799L

internal enum class D3Command(val wire: Int) { INFO(1), STATUS(2), SET_TIME(3) }

internal data class D3Frame(
    val response: Boolean,
    val command: Int,
    val sequence: Int,
    val session: Long,
    val payload: ByteArray,
) {
    fun encode(): ByteArray {
        require(payload.size <= D3_MAX_PAYLOAD && sequence in 1..65535 && session in 1L..0xffffffffL)
        val end = HEADER_BYTES + payload.size
        return ByteArray(end + CRC_BYTES).apply {
            this[0] = 0xa5.toByte()
            this[1] = 0x5a
            this[2] = 1
            this[3] = if (response) 1 else 0
            this[4] = command.toByte()
            putLittleEndian(5, sequence.toLong(), 2)
            putLittleEndian(7, session, 4)
            putLittleEndian(11, payload.size.toLong(), 2)
            payload.copyInto(this, HEADER_BYTES)
            putLittleEndian(end, crc(this, end), CRC_BYTES)
        }
    }
}

private fun crc(bytes: ByteArray, length: Int) = CRC32().apply { update(bytes, 0, length) }.value

private fun ByteArray.octet(offset: Int) = this[offset].toInt() and 0xff
private fun ByteArray.littleEndian(offset: Int, width: Int): Long {
    var value = 0L
    repeat(width) { value = value or (octet(offset + it).toLong() shl (8 * it)) }
    return value
}

private fun ByteArray.putLittleEndian(offset: Int, value: Long, width: Int) {
    repeat(width) { this[offset + it] = (value ushr (8 * it)).toByte() }
}

internal fun d3TimePayload(utcSeconds: Long): ByteArray {
    require(utcSeconds in D3_MIN_UTC..D3_MAX_UTC)
    return ByteArray(4).apply { putLittleEndian(0, utcSeconds, 4) }
}

internal class D3FrameReader {
    private val bytes = ByteArray(MAX_FRAME_BYTES)
    private val arrivals = LongArray(MAX_FRAME_BYTES)
    private var count = 0

    fun clear() { count = 0 }

    // +1 enforces strict expiry after the allowed gap/age, including a single A5.
    fun remainingMillis(now: Long): Long {
        if (count == 0) return 0L
        val remaining = minOf(arrivals[count - 1] + 1_001L, arrivals[0] + 5_001L) - now
        if (remaining <= 0) clear()
        return remaining.coerceAtLeast(0L)
    }

    fun feed(chunk: ByteArray, now: Long, emit: (D3Frame) -> Unit) {
        remainingMillis(now)
        for (byte in chunk) {
            arrivals[count] = now
            bytes[count++] = byte
            while (count > 0) {
                if (bytes.octet(0) != 0xa5 || (count >= 2 && bytes.octet(1) != 0x5a)) {
                    discard(1)
                    continue
                }
                if (count < HEADER_BYTES) break
                val payloadLength = bytes.littleEndian(11, 2).toInt()
                if (bytes.octet(2) != 1 || bytes.octet(3) !in 0..1 || payloadLength > D3_MAX_PAYLOAD) {
                    discard(1)
                    continue
                }
                val end = HEADER_BYTES + payloadLength
                val size = end + CRC_BYTES
                if (count < size) break
                if (bytes.littleEndian(end, CRC_BYTES) != crc(bytes, end)) {
                    discard(1)
                    continue
                }
                val frame = D3Frame(bytes.octet(3) == 1, bytes.octet(4),
                    bytes.littleEndian(5, 2).toInt(), bytes.littleEndian(7, 4),
                    bytes.copyOfRange(HEADER_BYTES, end))
                discard(size)
                emit(frame)
            }
        }
    }

    private fun discard(length: Int) {
        bytes.copyInto(bytes, endIndex = count, startIndex = length)
        arrivals.copyInto(arrivals, endIndex = count, startIndex = length)
        count -= length
    }
}

internal class D3DeviceError(val code: Int) : IllegalStateException(
    when (code) {
        1 -> "手表拒绝载荷长度"
        2 -> "手表拒绝参数"
        3 -> "手表不支持该命令"
        4 -> "手表会话或时钟未就绪"
        5 -> "手表读写失败"
        6 -> "手表正忙"
        7 -> "请求序号冲突"
        8 -> "请求已过期"
        9 -> "手表内部错误"
        10 -> "手表操作超时"
        else -> "未知手表错误（$code）"
    },
)

private fun ByteArray.checkReply() {
    require(isNotEmpty()) { "应答缺少结果码" }
    val code = octet(0)
    if (code != 0) {
        require(size == 1) { "错误应答长度不符" }
        throw D3DeviceError(code)
    }
}

internal data class D3Identity(
    val uid: String, val firmware: String, val hardware: Int, val revision: Int, val capabilities: Long,
) {
    fun supports(command: D3Command) = capabilities and (1L shl (command.wire - 1)) != 0L
}

internal fun d3Identity(payload: ByteArray): D3Identity {
    payload.checkReply()
    require(payload.size >= 23) { "设备信息应答不完整" }
    val length = payload.octet(21)
    val firmware = payload.copyOfRange(22, payload.size)
    require(length in 1..31 && firmware.size == length && firmware.all { it in 0x20..0x7e }) {
        "固件版本字段无效"
    }
    val capabilities = payload.littleEndian(17, 4)
    require(capabilities and 0xfffffe00L == 0L) { "设备能力保留位无效" }
    return D3Identity(payload.copyOfRange(1, 13).serialHex().replace(" ", ""),
        firmware.toString(Charsets.US_ASCII), payload.littleEndian(13, 2).toInt(),
        payload.littleEndian(15, 2).toInt(), capabilities)
}

internal enum class D3DataState(val label: String) {
    UNSUPPORTED("不支持"), MISSING("暂无数据"), VALID("有效"), INVALID("无效"), STALE("已过期"),
}

internal data class D3Reading(val state: D3DataState, val value: Long?, val ageMs: Long?)
internal data class D3Status(
    val uptimeMs: Long, val time: D3Reading, val steps: D3Reading, val stepsScope: Int,
    val voltage: D3Reading, val percentage: D3Reading, val heart: D3Reading, val oxygen: D3Reading,
)

internal fun d3Status(payload: ByteArray): D3Status {
    payload.checkReply()
    require(payload.size == 43) { "状态应答长度不符" }
    fun reading(state: Int, start: Int, width: Int, max: Long = Long.MAX_VALUE, age: Int? = null): D3Reading {
        val kind = requireNotNull(D3DataState.entries.getOrNull(payload.octet(state))) { "未知数据状态" }
        val value = payload.littleEndian(start, width)
        require(if (kind == D3DataState.VALID) value <= max else value == 0L) { "状态与数值不符" }
        val elapsed = age?.let { payload.littleEndian(it, 4).takeUnless { value -> value == 0xffffffffL } }
        return D3Reading(kind, value.takeIf { kind == D3DataState.VALID }, elapsed)
    }
    val time = reading(5, 6, 4, D3_MAX_UTC)
    require(time.value == null || time.value >= D3_MIN_UTC) { "手表时间超出范围" }
    val scope = payload.octet(11)
    require(scope in 0..1) { "未知计步范围" }
    return D3Status(payload.littleEndian(1, 4), time, reading(10, 12, 4, age = 16), scope,
        reading(20, 21, 2, age = 23), reading(27, 28, 1, 100),
        reading(29, 30, 2, age = 32), reading(36, 37, 2, 10000, 39))
}

internal fun d3StoredTime(payload: ByteArray, requested: Long): Long {
    payload.checkReply()
    require(payload.size == 5) { "校时应答长度不符" }
    val stored = payload.littleEndian(1, 4)
    require(stored in D3_MIN_UTC..D3_MAX_UTC && stored in requested..requested + 1) { "校时应答的读回值超出允许范围" }
    return stored
}
