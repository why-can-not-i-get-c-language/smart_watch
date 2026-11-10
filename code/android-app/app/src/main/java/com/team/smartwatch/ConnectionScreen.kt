package com.team.smartwatch

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
internal fun ConnectionScreen(
    connection: Connection,
    onChange: (Connection) -> Unit,
    onBack: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val (message, action, next) = when (connection) {
        Connection.DISCONNECTED -> Triple("选择演示手表后开始连接。", "开始模拟连接", Connection.CONNECTING)
        Connection.CONNECTING -> Triple("正在建立连接，可以取消或模拟链路建立。", "模拟链路建立", Connection.SYNCING)
        Connection.SYNCING -> Triple("链路已建立，等待手表状态同步。", "模拟同步完成", Connection.SYNCED)
        Connection.SYNCED -> Triple("演示手表状态已同步，可返回首页查看读数。", "断开连接", Connection.DISCONNECTED)
        Connection.BLUETOOTH_OFF -> Triple("请先开启蓝牙，再连接手表。", "模拟开启蓝牙", Connection.DISCONNECTED)
        Connection.PERMISSION_REQUIRED -> Triple("需要附近设备权限才能继续连接。", "模拟授予权限", Connection.DISCONNECTED)
        Connection.FAILED -> Triple("本次连接失败，请确认设备状态后重试。", "重试连接", Connection.CONNECTING)
    }
    Column(
        modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(20.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        OutlinedButton(onClick = onBack) { Text("返回首页") }
        Text("设备连接", style = MaterialTheme.typography.headlineMedium)
        Text("演示手表 · 模拟流程", color = MaterialTheme.colorScheme.onSurfaceVariant)
        if (BuildConfig.NEARBY_PERMISSION_PREVIEW) NearbyPermissionPreview()
        Card(Modifier.fillMaxWidth()) {
            Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
                Text(connection.label, style = MaterialTheme.typography.titleLarge)
                Text(message)
                OutlinedButton(onClick = { onChange(next) }) { Text(action) }
                if (connection == Connection.CONNECTING || connection == Connection.SYNCING) {
                    OutlinedButton(onClick = { onChange(Connection.DISCONNECTED) }) { Text("取消连接") }
                }
            }
        }
        Text("模拟异常", style = MaterialTheme.typography.titleMedium)
        OutlinedButton(onClick = { onChange(Connection.BLUETOOTH_OFF) }) { Text("模拟蓝牙关闭") }
        OutlinedButton(onClick = { onChange(Connection.PERMISSION_REQUIRED) }) { Text("模拟权限不足") }
        OutlinedButton(onClick = { onChange(Connection.FAILED) }) { Text("模拟连接失败") }
    }
}
