package com.team.smartwatch

import java.io.Writer
import java.util.Locale

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

internal fun liveSnapshotCsv(snapshot: D3Snapshot): String = buildString {
    append("received_utc_ms,metric,value,unit,state,age_ms\r\n")
    fun field(metric: String, reading: D3Reading, unit: String) {
        val value = reading.value.takeIf { reading.state == D3DataState.VALID }
        append(listOf(
            snapshot.receivedAtMs, metric, value ?: "", unit,
            reading.state.name.lowercase(Locale.ROOT), reading.ageMs ?: "",
        ).joinToString(","))
        append("\r\n")
    }
    val status = snapshot.status
    field("watch_time", status.time, "utc_seconds")
    field(if (status.stepsScope == 0) "session_steps" else "daily_steps", status.steps, "steps")
    field("battery_voltage", status.voltage, "mV")
    field("battery_estimate", status.percentage, "percent")
    field("heart_rate", status.heart, "0.1 bpm")
    field("oxygen_estimate", status.oxygen, "0.01 percent")
}

private fun validity(reading: Reading<*>) = when (reading) {
    Reading.Missing -> "missing"
    is Reading.Valid -> "valid"
    is Reading.Invalid -> "invalid"
    is Reading.Stale -> "stale"
}
