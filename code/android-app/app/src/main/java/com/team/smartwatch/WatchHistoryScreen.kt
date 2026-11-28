package com.team.smartwatch

import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.FilterChip
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import java.io.IOException
import java.io.StringWriter
import java.text.SimpleDateFormat
import java.util.Locale
import java.util.TimeZone
import java.util.concurrent.TimeUnit
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

private enum class HistoryCategory(val kind: HistoryKind, val label: String, val unit: String) {
    STEPS(HistoryKind.DAILY_PARTIAL_STEPS, "每日步数", " 步"),
    EVENT(HistoryKind.MANUAL_EVENT, "手动事件", ""),
    HEART(HistoryKind.HEART_RATE, "心率", " bpm"),
    OXYGEN(HistoryKind.OXYGEN_ESTIMATE, "血氧估算", "%"),
}

private const val BEIJING_OFFSET_SECONDS = 28_800L
private const val SECONDS_PER_DAY = 86_400L

internal fun beijingDay(utcSeconds: Long): Long =
    Math.floorDiv(utcSeconds + BEIJING_OFFSET_SECONDS, SECONDS_PER_DAY)

internal fun latestDailyStepRecords(records: List<WatchRecord>): List<WatchRecord> =
    records.sortedByDescending { it.id.toLong() }
        .distinctBy { beijingDay((it.utcSeconds as Reading.Valid).value) }
        .sortedByDescending { (it.utcSeconds as Reading.Valid).value }

internal fun demoHistory(today: Reading<DailySteps>): List<WatchRecord> {
    val formatter = SimpleDateFormat("yyyy-MM-dd HH:mm", Locale.ROOT).apply {
        timeZone = TimeZone.getTimeZone("Asia/Shanghai")
    }
    val last = checkNotNull(formatter.parse("2026-11-06 09:30")).time / 1000
    fun record(kind: HistoryKind, index: Int, time: Long, value: Int?) = WatchRecord(
        "demo-${kind.name.lowercase(Locale.ROOT)}-$index",
        kind,
        Reading.Valid(time, "模拟记录"),
        value?.let { Reading.Valid(it, "模拟记录") } ?: Reading.Missing,
    )
    val steps = SimulatedWatch.pastSteps.map { it.count } + (today as? Reading.Valid)?.value?.count
    val dayBoundary = beijingDay(last) * SECONDS_PER_DAY - BEIJING_OFFSET_SECONDS
    val dailySteps = steps.mapIndexed { index, count ->
        val day = dayBoundary - SECONDS_PER_DAY * (steps.lastIndex - index)
        record(HistoryKind.DAILY_PARTIAL_STEPS, index, day, count).copy(id = (100 + index).toString())
    }.asReversed()
    val earlierSameDay = dailySteps.first().copy(id = "99", value = Reading.Valid(10, "模拟记录"))
    // 历史接口分页的是原始记录，界面在已加载的各页中按北京日界取最新 ID。
    val manual = record(HistoryKind.MANUAL_EVENT, 1, last, 1)
    return dailySteps + earlierSameDay + manual + listOf(
        HistoryKind.HEART_RATE to HealthTrendMetric.HEART_RATE.samples,
        HistoryKind.OXYGEN_ESTIMATE to HealthTrendMetric.OXYGEN.samples,
    ).flatMap { (kind, readings) ->
        readings.mapIndexed { index, count ->
            record(kind, index, last - TimeUnit.HOURS.toSeconds(2L * (readings.lastIndex - index)), count)
        }.asReversed()
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
internal fun WatchHistoryScreen(today: Reading<DailySteps>, onBack: () -> Unit, modifier: Modifier = Modifier) {
    val repository = remember(today) { WatchRepository(OfflineWatchSession(demoHistory(today))) }
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    var category by rememberSaveable { mutableStateOf(HistoryCategory.STEPS) }
    var pageRequest by remember(repository, category) { mutableIntStateOf(0) }
    var records by remember(repository, category) { mutableStateOf(emptyList<WatchRecord>()) }
    var cursor by remember(repository, category) { mutableStateOf<String?>(null) }
    var loading by remember(repository, category) { mutableStateOf(true) }
    var readError by remember(repository, category) { mutableStateOf<String?>(null) }
    var pendingCsv by rememberSaveable { mutableStateOf<String?>(null) }
    var exportStatus by rememberSaveable { mutableStateOf<String?>(null) }
    var exporting by remember { mutableStateOf(false) }

    val export = rememberLauncherForActivityResult(ActivityResultContracts.CreateDocument("text/csv")) { uri ->
        val content = pendingCsv
        pendingCsv = null
        if (uri != null && content != null) {
            exporting = true
            scope.launch {
                exportStatus = try {
                    withContext(Dispatchers.IO) {
                        val output = context.contentResolver.openOutputStream(uri, "wt")
                            ?: throw IOException("无法打开文件")
                        output.bufferedWriter(Charsets.UTF_8).use { it.write(content) }
                    }
                    "CSV 已保存"
                } catch (_: IOException) {
                    "CSV 保存失败，请重新选择存放位置"
                } catch (_: SecurityException) {
                    "无法写入所选位置，请重新选择"
                } finally {
                    exporting = false
                }
            }
        }
    }

    LaunchedEffect(repository, category, pageRequest) {
        loading = true
        readError = null
        try {
            val page = repository.history(HistoryQuery(category.kind, limit = 5, cursor = cursor))
            records = (records + page.records).distinctBy { it.id }
            cursor = page.nextCursor
        } catch (error: WatchRequestException) {
            readError = error.message ?: "读取记录失败"
        } finally {
            loading = false
        }
    }

    val displayed = if (category == HistoryCategory.STEPS) latestDailyStepRecords(records) else records
    Column(
        modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(20.dp),
        verticalArrangement = Arrangement.spacedBy(10.dp),
    ) {
        OutlinedButton(onClick = onBack, enabled = !exporting) { Text("返回首页") }
        Text("历史与导出", style = MaterialTheme.typography.headlineSmall)
        Text("模拟数据 · 实际记录待手表分页接口", color = MaterialTheme.colorScheme.onSurfaceVariant)
        FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            HistoryCategory.entries.forEach { item ->
                FilterChip(selected = category == item, onClick = { category = item; exportStatus = null },
                    label = { Text(item.label) })
            }
        }
        if (category == HistoryCategory.STEPS) Text("部分汇总 · 北京时间 · 缺失日期不补零")
        Text("已显示 ${displayed.size} 条记录")
        displayed.forEach { record ->
            val time = (record.utcSeconds as? Reading.Valid)?.value?.let {
                if (category == HistoryCategory.STEPS) formatD3Time(it).substringBefore(' ') else formatD3Time(it)
            } ?: "时间未知"
            val value = (record.value as? Reading.Valid)?.let {
                if (category == HistoryCategory.EVENT) "事件 #${it.value}" else "${it.value}${category.unit}"
            } ?: "缺失"
            Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                Text(time, Modifier.weight(1f))
                Text(value)
            }
        }
        if (loading) Text("正在加载…")
        if (!loading && readError == null && records.isEmpty()) Text("暂无记录")
        readError?.let { Text(it, color = MaterialTheme.colorScheme.error) }
        if (readError != null || cursor != null) {
            OutlinedButton(enabled = !loading, onClick = { loading = true; pageRequest++ }) {
                Text(if (readError != null) "重试" else "加载更多")
            }
        }
        OutlinedButton(enabled = records.isNotEmpty() && pendingCsv == null && !exporting, onClick = {
            exportStatus = null
            pendingCsv = StringWriter().also { exportHistoryCsv(displayed, it) }.toString()
            try {
                export.launch("smartwatch-demo-${category.kind.name.lowercase(Locale.ROOT)}.csv")
            } catch (_: android.content.ActivityNotFoundException) {
                pendingCsv = null
                exportStatus = "没有可用的文件保存应用"
            }
        }) { Text("导出已加载记录 CSV") }
        exportStatus?.let { Text(it) }
    }
}
