package com.team.smartwatch

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Card
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp

@OptIn(ExperimentalLayoutApi::class)
@Composable
internal fun LiveHomeScreen(
    onConnection: () -> Unit,
    onFavorites: () -> Unit,
    onWeather: () -> Unit,
    onEmergencyContacts: (() -> Unit)?,
    onDemo: (() -> Unit)?,
    modifier: Modifier = Modifier,
    liveWatchStatus: @Composable () -> Unit,
) {
    MaterialTheme(colorScheme = WatchOverviewColors) {
        val colors = MaterialTheme.colorScheme
        Surface(modifier.fillMaxSize(), color = colors.background, contentColor = colors.onBackground) {
            Column(
                Modifier.verticalScroll(rememberScrollState()).padding(16.dp),
                verticalArrangement = Arrangement.spacedBy(12.dp),
            ) {
                Text("智能手表", style = MaterialTheme.typography.headlineMedium, fontWeight = FontWeight.Bold)
                FlowRow(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                    OutlinedButton(onClick = onConnection) { Text("设备连接") }
                    OutlinedButton(onClick = onFavorites) { Text("蓝牙收藏") }
                    OutlinedButton(onClick = onWeather) { Text("手机天气") }
                    if (onEmergencyContacts != null) {
                        OutlinedButton(onClick = onEmergencyContacts) { Text("紧急联系人") }
                    }
                }
                liveWatchStatus()
                Card(Modifier.fillMaxWidth()) {
                    Column(Modifier.padding(14.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                        Text("待固件支持", style = MaterialTheme.typography.titleMedium)
                        Text("手表提醒设置、历史读取与导出、天气同步、SOS、无线升级",
                            color = colors.onSurfaceVariant, style = MaterialTheme.typography.bodyMedium)
                    }
                }
                if (onDemo != null) OutlinedButton(onClick = onDemo) { Text("进入功能演示") }
            }
        }
    }
}
