package com.team.smartwatch

// 事件去重、联系人、用户确认和系统短信/拨号交互；实际Android工程由C01建立。
class SosCoordinator {
    fun handleEvent(eventId: Long): Result<Unit> = Result.failure(UnsupportedOperationException("未实现求助事件：$eventId"))
}
