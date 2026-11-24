package com.team.smartwatch

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue

internal enum class D3Fault(val label: String) {
    NONE("正常"),
    DISCONNECT_BEFORE_RESPONSE("完整写入后断开（屏蔽应答）"),
    DISCONNECT_AFTER_FIRST_CHUNK("发送首个分片后断开"),
    PARTIAL_RESPONSE("首条应答仅交付前 12 字节"),
    DROP_RESPONSES("屏蔽该次校时的全部应答"),
    DELAY_RESPONSE("应答延后到至少 1200 ms"),
    CLOCK_CHANGE("模拟手机系统时间跳变"),
}

/** Available only in the isolated development flavor. The next SetTime consumes the selection. */
internal class D3CalibrationDiagnostics {
    private val records = mutableStateListOf<String>()
    val entries: List<String> get() = records
    var selected by mutableStateOf(D3Fault.NONE)
        private set

    fun select(fault: D3Fault) {
        selected = fault
    }

    fun consume(): D3Fault {
        val next = selected
        selected = D3Fault.NONE
        return next
    }

    fun record(message: String) {
        if (records.size >= 18) records.removeAt(0)
        records.add(message)
    }
}
