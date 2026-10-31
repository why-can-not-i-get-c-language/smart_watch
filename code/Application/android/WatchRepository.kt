package com.team.smartwatch

// 状态、校时和参数请求；实际Android工程由C01建立。
class WatchRepository {
    suspend fun synchronize(): Result<Unit> = Result.failure(UnsupportedOperationException("未实现同步"))
}
