package com.team.smartwatch

import androidx.activity.compose.BackHandler
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Card
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp

@Composable
internal fun SosHistoryScreen(model: FallSosViewModel, connection: Connection, onBack: () -> Unit, modifier: Modifier) {
    BackHandler(onBack = onBack)
    Column(modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(20.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp)) {
        OutlinedButton(onClick = onBack) { Text("返回 SOS") }
        Text("求助记录", style = MaterialTheme.typography.headlineMedium)
        Text("本机最近 20 条模拟事件 · 北京时间")
        if (model.loading) Text("正在读取记录…")
        model.storageError?.let { Text(it) }
        if (!model.loading && model.storageError == null && model.history.isEmpty()) Text("暂无已保存记录")
        for (record in model.history) {
            Card(Modifier.fillMaxWidth()) {
                Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                    Text("事件 #${record.id} · ${record.source}", style = MaterialTheme.typography.titleMedium)
                    Text(record.time)
                    Text(record.stage.label)
                    if (record.callStage != SosCallStage.NONE) {
                        Text("模拟拨号：${record.callStage.label} · 已尝试 ${record.callIndex + 1} 位")
                    }
                    Text(record.smsMessage)
                    Text("短信批次：${record.attempts} · 接收方送达未知")
                    if (record.hasSmsSnapshot) Text("短信收件人：${record.contacts.size} 位")
                    record.phoneAction?.let { Text("已模拟打开${it}页") }
                    OutlinedButton(onClick = { model.replayHistory(record.id) },
                        enabled = model.ready && connection == Connection.SYNCED) {
                        Text("模拟重传 #${record.id}")
                    }
                }
            }
        }
        Text(model.message)
        Text("记录保存在此应用中；卸载或清除应用数据会删除记录。")
    }
}
