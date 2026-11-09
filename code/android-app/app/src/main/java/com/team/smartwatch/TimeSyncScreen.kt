package com.team.smartwatch

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp

@Composable
internal fun TimeSyncScreen(
    model: TimeSyncViewModel,
    connection: Connection,
    watchTime: Reading<String>,
    phoneTime: String,
    onBack: () -> Unit,
    onConnection: () -> Unit,
    modifier: Modifier = Modifier,
) {
    Column(
        modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(20.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        OutlinedButton(onClick = onBack) { Text("返回首页") }
        Text("时间校准", style = MaterialTheme.typography.headlineMedium)
        Text("模拟流程 · 尚未连接真实手表", color = MaterialTheme.colorScheme.onSurfaceVariant)
        Card(Modifier.fillMaxWidth()) {
            Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                Text("手机当前时间 · 北京时间 UTC+8", style = MaterialTheme.typography.labelLarge)
                Text(phoneTime, style = MaterialTheme.typography.titleLarge)
                Text("模拟手表时间", style = MaterialTheme.typography.labelLarge)
                Text(when (watchTime) {
                    is Reading.Valid -> watchTime.value
                    is Reading.Invalid -> watchTime.reason
                    is Reading.Stale -> "时间已过期"
                    Reading.Missing -> "尚未获取"
                }, style = MaterialTheme.typography.titleMedium)
            }
        }
        Card(Modifier.fillMaxWidth()) {
            Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Column(Modifier.weight(1f)) {
                        Text("自动校时", style = MaterialTheme.typography.titleMedium)
                        Text("连接就绪后同步手机时间", style = MaterialTheme.typography.bodySmall)
                    }
                    Switch(checked = model.automatic, onCheckedChange = model::setAutomatic)
                }
                Text("${connection.label} · ${model.message}")
                Button(onClick = { model.synchronize() }, enabled = connection == Connection.SYNCED && !model.phase.busy) {
                    Text("立即校时")
                }
                if (connection != Connection.SYNCED) {
                    Text("请先完成设备连接与状态同步，再进行校时。", style = MaterialTheme.typography.bodySmall)
                }
                OutlinedButton(onClick = onConnection) { Text("设备连接") }
            }
        }
        Text("演示结果：${model.reply.label}", style = MaterialTheme.typography.bodyMedium)
        OutlinedButton(onClick = model::nextReply, enabled = !model.phase.busy) { Text("切换演示结果") }
        Text("手表固定显示北京时间。仅模拟设备应答和回读完成后才显示成功；当前不会修改真实手表时间。",
            style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
    }
}
