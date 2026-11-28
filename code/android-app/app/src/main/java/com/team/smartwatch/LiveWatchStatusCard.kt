package com.team.smartwatch

import android.content.ActivityNotFoundException
import android.os.SystemClock
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.repeatOnLifecycle
import java.io.IOException
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

private const val REFRESH_SECONDS = 15L

@OptIn(ExperimentalLayoutApi::class)
@Composable
internal fun LiveWatchStatusCard(
    watch: D3Watch?,
    linking: Boolean,
    connectionMessage: String?,
    nowElapsedMillis: Long,
    onDisconnect: () -> Unit,
    onRescan: () -> Unit,
    lifecycle: Lifecycle,
) {
    LaunchedEffect(watch, linking, lifecycle) {
        if (watch != null && linking) {
            lifecycle.repeatOnLifecycle(Lifecycle.State.RESUMED) {
                if (watch.snapshot?.isFresh(SystemClock.elapsedRealtime()) == false) watch.autoRefresh()
                while (isActive) {
                    delay(REFRESH_SECONDS * 1_000L)
                    watch.autoRefresh()
                }
            }
        }
    }
    val colors = MaterialTheme.colorScheme
    val latest = watch?.snapshot.takeIf { linking }
    val snapshot = latest?.takeIf { it.isFresh(maxOf(nowElapsedMillis, it.receivedElapsedMs)) }
    val status = when {
        !linking -> "未连接"
        latest != null && snapshot == null -> "数据已过期"
        else -> watch?.message ?: connectionMessage ?: "正在连接"
    }
    val statusColor = when {
        snapshot != null -> colors.primary
        linking -> colors.secondary
        else -> colors.onSurfaceVariant
    }

    Card(
        Modifier.fillMaxWidth(),
        colors = CardDefaults.cardColors(containerColor = colors.surfaceContainer),
    ) {
        Column(Modifier.padding(14.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
            Row(
                Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(12.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Text("真表数据", Modifier.weight(1f), style = MaterialTheme.typography.titleMedium,
                    fontWeight = FontWeight.SemiBold)
                Text(status, color = statusColor, style = MaterialTheme.typography.bodyMedium)
            }
            if (snapshot != null) {
                val data = snapshot.status
                LiveReading("手表时间 · 读回快照", renderReading(data.time, ::formatD3Time))
                LiveReading(d3StepsLabel(data.stepsScope), renderReading(data.steps) { "$it 步" })
                LiveReading("电池电压", renderReading(data.voltage) { "$it mV" })
                LiveReading("估算电量", renderReading(data.percentage) { "$it%" })
                LiveReading("心率", renderReading(data.heart) { formatD3HeartRate(it) })
                LiveReading("估算血氧", renderReading(data.oxygen) { formatD3Oxygen(it) })
                Text("最近接收 ${beijingTime(snapshot.receivedAtMs)}", style = MaterialTheme.typography.bodySmall,
                    color = colors.onSurfaceVariant)
            } else if (latest != null) {
                Text("上次接收 ${beijingTime(latest.receivedAtMs)}，请读取手表状态。")
            } else if (!linking && connectionMessage != null) {
                Text(connectionMessage, color = colors.onSurfaceVariant, style = MaterialTheme.typography.bodySmall)
                OutlinedButton(onClick = onRescan) { Text("重新扫描手表") }
            }
            if (linking) {
                FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    if (watch != null) {
                        OutlinedButton(onClick = watch::refresh, enabled = watch.canRefresh) { Text("读取手表状态") }
                        OutlinedButton(onClick = watch::calibrate, enabled = watch.canSetTime) {
                            Text("用手机当前时间校时")
                        }
                    }
                    OutlinedButton(onClick = onDisconnect) { Text("断开连接") }
                }
                if (watch?.identity?.supports(D3Command.STATUS) == true) {
                    Text("首页前台每 ${REFRESH_SECONDS} 秒尝试更新", style = MaterialTheme.typography.bodySmall,
                        color = colors.onSurfaceVariant)
                }
            }
            LiveSnapshotExport(snapshot)
            watch?.timeResult?.let { result ->
                Text(if (linking) result else "最近校时 · $result", style = MaterialTheme.typography.bodySmall)
            }
        }
    }
}

@Composable
private fun LiveSnapshotExport(snapshot: D3Snapshot?) {
    val resolver = LocalContext.current.contentResolver
    val scope = rememberCoroutineScope()
    var pending by rememberSaveable { mutableStateOf<String?>(null) }
    var result by rememberSaveable { mutableStateOf<String?>(null) }
    val export = rememberLauncherForActivityResult(ActivityResultContracts.CreateDocument("text/csv")) { uri ->
        val csv = pending
        if (uri == null || csv == null) {
            pending = null
            return@rememberLauncherForActivityResult
        }
        scope.launch {
            try {
                withContext(Dispatchers.IO) {
                    val output = resolver.openOutputStream(uri, "wt") ?: throw IOException()
                    output.bufferedWriter(Charsets.UTF_8).use { it.write(csv) }
                }
                result = "当前读回数据已导出"
            } catch (_: IOException) {
                result = "CSV 保存失败，请重试"
            } catch (_: SecurityException) {
                result = "无法写入所选位置"
            } finally {
                pending = null
            }
        }
    }
    if (snapshot != null) {
        OutlinedButton(enabled = pending == null, onClick = {
            if (!snapshot.isFresh(SystemClock.elapsedRealtime())) {
                result = "数据已过期，请重新读取手表状态"
                return@OutlinedButton
            }
            result = null
            pending = liveSnapshotCsv(snapshot)
            try {
                export.launch("smartwatch-status-${snapshot.receivedAtMs}.csv")
            } catch (_: ActivityNotFoundException) {
                pending = null
                result = "没有可用的文件保存应用"
            }
        }) { Text("导出当前读回数据 CSV") }
    }
    result?.let { Text(it, style = MaterialTheme.typography.bodySmall) }
}

@Composable
private fun LiveReading(label: String, value: String) {
    Text(label, color = MaterialTheme.colorScheme.onSurfaceVariant, style = MaterialTheme.typography.labelMedium)
    Text(value, style = MaterialTheme.typography.bodyLarge)
}
