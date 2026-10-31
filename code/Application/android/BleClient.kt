package com.team.smartwatch

// BLE扫描、权限、连接、分包收发和断开恢复；实际Android工程由C01建立。
class BleClient {
    suspend fun connect(deviceAddress: String): Result<Unit> = Result.failure(UnsupportedOperationException("未实现BLE连接：$deviceAddress"))
}
