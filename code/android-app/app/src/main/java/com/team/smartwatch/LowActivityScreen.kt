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
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.FilterChip
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.dp

@Composable
internal fun LowActivityScreen(
    model: LowActivityViewModel,
    connection: Connection,
    onBack: () -> Unit,
    onConnection: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val confirmed = model.confirmed.takeIf { connection == Connection.SYNCED }
    Column(
        modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(20.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        OutlinedButton(onClick = onBack) { Text("返回首页") }
        Text("低运动提醒", style = MaterialTheme.typography.headlineMedium)
        Text("连续活动较少达到设定时长后提醒。")
        Card(Modifier.fillMaxWidth()) {
            Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text("启用低运动提醒", Modifier.weight(1f))
                    Switch(
                        checked = model.draft.enabled,
                        onCheckedChange = model::setEnabled,
                        enabled = !model.saving,
                        modifier = Modifier.semantics { contentDescription = "启用低运动提醒" },
                    )
                }
                Text("连续低运动时长（演示）")
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    for (minutes in lowActivityDemoDurations) {
                        FilterChip(
                            selected = model.draft.minutes == minutes,
                            onClick = { model.selectMinutes(minutes) },
                            enabled = !model.saving,
                            label = { Text("$minutes 分钟") },
                        )
                    }
                }
                Text("${connection.label} · ${model.message}")
                Button(enabled = connection == Connection.SYNCED && !model.saving, onClick = model::save) {
                    Text("保存低运动设置")
                }
                if (connection != Connection.SYNCED) {
                    OutlinedButton(onClick = onConnection) { Text("设备连接") }
                }
            }
        }
        Text(
            if (confirmed == null) "模拟手表回读：尚未确认"
            else "模拟手表回读：${if (confirmed.enabled) "开启" else "关闭"} · ${confirmed.minutes} 分钟",
        )
        if (confirmed?.enabled == true) {
            Text("低运动累计：${model.accumulatedMinutes} / ${confirmed.minutes} 分钟")
            if (model.reminding) LowActivityAlert()
            OutlinedButton(
                enabled = model.canSimulate && !model.reminding,
                onClick = model::simulateLowActivity,
            ) { Text("模拟低运动 +15 分钟") }
            OutlinedButton(
                enabled = model.canSimulate && model.accumulatedMinutes > 0,
                onClick = model::simulateActivityResumed,
            ) { Text("模拟恢复活动") }
        }
        Text("此页使用模拟活动。实机由手表检测并振动，待蓝牙联调。", style = MaterialTheme.typography.bodySmall)
    }
}

@Composable
internal fun LowActivityAlert() {
    Card(
        modifier = Modifier.fillMaxWidth(),
        colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.tertiaryContainer),
    ) {
        Text("低运动提醒（模拟）：起来活动一下", Modifier.padding(16.dp))
    }
}
