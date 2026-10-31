package com.team.smartwatch

// 分页历史、断连续读和CSV导出；实际Android工程由C01建立。
class HistoryExporter {
    fun exportCsv(): Result<String> = Result.failure(UnsupportedOperationException("未实现历史导出"))
}
