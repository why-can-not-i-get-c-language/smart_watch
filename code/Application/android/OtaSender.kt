package com.team.smartwatch

// 固件分包、偏移应答、重传和恢复；实际Android工程由C01建立。
class OtaSender {
    suspend fun send(image: ByteArray): Result<Unit> = Result.failure(UnsupportedOperationException("未实现升级，镜像字节数：${image.size}"))
}
