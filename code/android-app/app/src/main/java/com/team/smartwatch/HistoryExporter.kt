package com.team.smartwatch

import java.io.Writer

internal fun exportHistoryCsv(records: Iterable<WatchRecord>, output: Writer) {
    output.write("id,kind,utc_seconds,value,unit,time_validity,value_validity\r\n")
    for (record in records) {
        val unit = when (record.kind) {
            HistoryKind.STEPS, HistoryKind.DAILY_PARTIAL_STEPS -> "steps"
            HistoryKind.HEART_RATE -> "bpm"
            HistoryKind.OXYGEN_ESTIMATE -> "percent"
            HistoryKind.SOS, HistoryKind.MANUAL_EVENT -> ""
        }
        output.write(listOf(
            record.id, record.kind.name,
            (record.utcSeconds as? Reading.Valid)?.value?.toString().orEmpty(),
            (record.value as? Reading.Valid)?.value?.toString().orEmpty(),
            unit, validity(record.utcSeconds), validity(record.value),
        ).joinToString(",") { value -> "\"${value.replace("\"", "\"\"")}\"" } + "\r\n")
    }
}

private fun validity(reading: Reading<*>) = when (reading) {
    Reading.Missing -> "missing"
    is Reading.Valid -> "valid"
    is Reading.Invalid -> "invalid"
    is Reading.Stale -> "stale"
}
