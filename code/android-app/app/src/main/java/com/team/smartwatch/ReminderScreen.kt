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
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp

@Composable
internal fun ReminderScreen(
    model: ReminderViewModel,
    connection: Connection,
    onBack: () -> Unit,
    onConnection: () -> Unit,
    modifier: Modifier = Modifier,
) {
    Column(
        modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(20.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        OutlinedButton(onClick = onBack) { Text("返回首页") }
        Text("提醒设置", style = MaterialTheme.typography.headlineMedium)
        Text("每日提醒 · 模拟数据 · 北京时间", color = MaterialTheme.colorScheme.onSurfaceVariant)
        Card(Modifier.fillMaxWidth()) {
            Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
                OutlinedTextField(
                    value = model.time,
                    onValueChange = model::editTime,
                    label = { Text("提醒时间（HH:mm）") },
                    singleLine = true,
                    enabled = !model.phase.busy,
                    isError = model.error != null,
                    modifier = Modifier.fillMaxWidth(),
                )
                model.error?.let { Text(it, color = MaterialTheme.colorScheme.error) }
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text("启用提醒", Modifier.weight(1f))
                    Switch(checked = model.enabled, onCheckedChange = model::setEnabled, enabled = !model.phase.busy)
                }
                Text("${connection.label} · ${model.message}")
                Button(
                    enabled = connection == Connection.SYNCED && !model.phase.busy,
                    onClick = model::save,
                ) { Text("保存提醒") }
                if (connection != Connection.SYNCED) {
                    Text("请先完成设备连接与状态同步。", style = MaterialTheme.typography.bodySmall)
                }
                OutlinedButton(onClick = onConnection) { Text("设备连接") }
            }
        }
        val confirmed = model.confirmed
        Text(
            if (confirmed == null) "模拟设备回读：尚未确认"
            else "模拟设备回读：每日 ${confirmed.time} · ${if (confirmed.enabled) "开启" else "关闭"}",
            style = MaterialTheme.typography.titleMedium,
        )
        Text("演示结果：${model.reply.label}")
        OutlinedButton(enabled = !model.phase.busy, onClick = model::nextReply) { Text("切换演示结果") }
        Text("确认值仅用于本次运行的模拟回读。真实提醒由手表触发，重复规则和参数按正式协议接入。",
            style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
    }
}
