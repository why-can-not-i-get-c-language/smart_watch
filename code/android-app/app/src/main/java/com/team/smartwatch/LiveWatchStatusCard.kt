package com.team.smartwatch

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
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.repeatOnLifecycle
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive

private const val REFRESH_SECONDS = 15L

@OptIn(ExperimentalLayoutApi::class)
@Composable
internal fun LiveWatchStatusCard(
    watch: D3Watch?,
    linking: Boolean,
    connectionMessage: String?,
    onDisconnect: () -> Unit,
    onRescan: () -> Unit,
    lifecycle: Lifecycle,
) {
    LaunchedEffect(watch, linking, lifecycle) {
        if (watch != null && linking) {
            lifecycle.repeatOnLifecycle(Lifecycle.State.RESUMED) {
                while (isActive) {
                    delay(REFRESH_SECONDS * 1_000L)
                    watch.autoRefresh()
                }
            }
        }
    }
    val colors = MaterialTheme.colorScheme
    val snapshot = if (linking) watch?.snapshot else null
    val status = when {
        !linking -> "未连接"
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
                Text("最近接收 ${beijingTime(snapshot.receivedAtMs)}", style = MaterialTheme.typography.bodySmall,
                    color = colors.onSurfaceVariant)
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
            watch?.timeResult?.let { result ->
                Text(if (linking) result else "最近校时 · $result", style = MaterialTheme.typography.bodySmall)
            }
        }
    }
}

@Composable
private fun LiveReading(label: String, value: String) {
    Text(label, color = MaterialTheme.colorScheme.onSurfaceVariant, style = MaterialTheme.typography.labelMedium)
    Text(value, style = MaterialTheme.typography.bodyLarge)
}
