package com.team.smartwatch

import java.util.Locale

// 原厂 TEL0073 Android 示例以 17 字节分片，台架验证后再调整。
internal const val BLE_SERIAL_WRITE_BYTES = 17
internal const val BLE_SERIAL_MESSAGE_BYTES = 512

internal fun serialPayload(input: String, hex: Boolean): ByteArray? {
    if (!hex) return input.toByteArray(Charsets.UTF_8).takeIf { it.size in 1..BLE_SERIAL_MESSAGE_BYTES }
    val digits = input.filterNot(Char::isWhitespace)
    if (digits.length !in 2..BLE_SERIAL_MESSAGE_BYTES * 2 || digits.length % 2 != 0) return null
    if (!digits.all { it in '0'..'9' || it in 'a'..'f' || it in 'A'..'F' }) return null
    return ByteArray(digits.length / 2) { index ->
        digits.substring(index * 2, index * 2 + 2).toInt(16).toByte()
    }
}

internal fun ByteArray.serialHex(): String =
    joinToString(" ") { String.format(Locale.ROOT, "%02X", it.toInt() and 0xFF) }

internal fun ByteArray.serialChunkAt(offset: Int): ByteArray =
    copyOfRange(offset, minOf(offset + BLE_SERIAL_WRITE_BYTES, size))
