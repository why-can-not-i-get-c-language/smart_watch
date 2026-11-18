package com.team.smartwatch

import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class BleSerialPayloadTest {
    @Test
    fun rawHexKeepsNulHighByteAndLineEndings() {
        val input = "00 ff 0D 0A\n31 32 33 34"
        val payload = requireNotNull(serialPayload(input, hex = true))
        assertArrayEquals(byteArrayOf(0, -1, 13, 10, 49, 50, 51, 52), payload)
        assertEquals("00 FF 0D 0A 31 32 33 34", payload.serialHex())
    }

    @Test
    fun twentyBytePayloadUsesTwoOrderedWrites() {
        val text = "0123456789ABCDEFGHIJ"
        val message = requireNotNull(serialPayload(text, hex = false))
        val first = message.serialChunkAt(0)
        val second = message.serialChunkAt(first.size)
        assertEquals(17, first.size)
        assertEquals(3, second.size)
        assertEquals(text, (first + second).toString(Charsets.UTF_8))
    }

    @Test
    fun rejectsMalformedOrOversizeInput() {
        assertNull(serialPayload("01 F", hex = true))
        assertNull(serialPayload("01 ZZ", hex = true))
        assertNull(serialPayload("00".repeat(513), hex = true))
        assertNull(serialPayload("测".repeat(171), hex = false))
    }
}
