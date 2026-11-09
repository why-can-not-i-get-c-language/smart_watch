package com.team.smartwatch

import android.app.TimePickerDialog

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
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp

@Composable
internal fun ReminderScreen(
    model: ReminderViewModel,
    connection: Connection,
    onBack: () -> Unit,
    onConnection: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val context = LocalContext.current
    var choosingTime by rememberSaveable { mutableStateOf(false) }
    if (choosingTime) {
        DisposableEffect(context) {
            val (hours, minutes) = model.time.split(":")
            val dialog = TimePickerDialog(context, { _, hour, minute ->
                model.editTime("${hour.toString().padStart(2, '0')}:${minute.toString().padStart(2, '0')}")
            }, hours.toInt(), minutes.toInt(), true)
            dialog.setTitle("选择闹钟时间")
            dialog.setOnDismissListener { choosingTime = false }
            dialog.show()
            onDispose {
                dialog.setOnDismissListener(null)
                dialog.dismiss()
            }
        }
    }
    Column(
        modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(20.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        OutlinedButton(onClick = onBack) { Text("返回首页") }
        Text("手表闹钟", style = MaterialTheme.typography.headlineMedium)
        Text("每日一次 · 北京时间 · 模拟设置", color = MaterialTheme.colorScheme.onSurfaceVariant)
        Card(Modifier.fillMaxWidth()) {
            Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
                OutlinedButton(enabled = !model.phase.busy, onClick = { choosingTime = true }) {
                    Text("闹钟时间：${model.time}")
                }
                model.error?.let { Text(it, color = MaterialTheme.colorScheme.error) }
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text("启用闹钟", Modifier.weight(1f))
                    Switch(checked = model.enabled, onCheckedChange = model::setEnabled, enabled = !model.phase.busy)
                }
                Text("${connection.label} · ${model.message}")
                Button(
                    enabled = connection == Connection.SYNCED && !model.phase.busy,
                    onClick = model::save,
                ) { Text("保存闹钟") }
                if (connection != Connection.SYNCED) {
                    Text("请先完成设备连接与状态同步。", style = MaterialTheme.typography.bodySmall)
                }
                OutlinedButton(onClick = onConnection) { Text("设备连接") }
            }
        }
        val confirmed = model.confirmed
        Text(
            if (confirmed == null) "模拟手表闹钟：尚未确认"
            else "模拟手表闹钟：每日 ${confirmed.time} · ${if (confirmed.enabled) "开启" else "关闭"}",
            style = MaterialTheme.typography.titleMedium,
        )
        Text("演示结果：${model.reply.label}")
        OutlinedButton(enabled = !model.phase.busy, onClick = model::nextReply) { Text("切换演示结果") }
        Text("当前只模拟保存和回读。实机闹钟由手表振动提醒，待固件和蓝牙联调。",
            style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
    }
}
