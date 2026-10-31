package com.team.smartwatch

// 与STM32共用的帧编解码、序号和CRC；实际Android工程由C01建立。
class PacketCodec {
    fun decode(bytes: ByteArray): Result<ByteArray> = Result.failure(UnsupportedOperationException("未实现解码，输入字节数：${bytes.size}"))
}
