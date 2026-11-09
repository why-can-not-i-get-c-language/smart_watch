package com.team.smartwatch

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp

private enum class WeatherScenario(val status: String) {
    TODAY("演示日期 11 月 6 日 · 09:30 更新"),
    CACHED("刷新失败 · 显示当日缓存"),
    EXPIRED("天气已过期 · 请刷新"),
}

@Composable
internal fun WeatherCard(connection: Connection, connectionRevision: Int) {
    var index by rememberSaveable { mutableIntStateOf(0) }
    val scenario = WeatherScenario.entries[index]
    var acknowledgedFor by rememberSaveable { mutableStateOf<String?>(null) }
    val requestKey = "$connectionRevision-${scenario.name}"
    val acknowledged = acknowledgedFor == requestKey
    val canSync = connection == Connection.SYNCED && scenario != WeatherScenario.EXPIRED
    val colors = MaterialTheme.colorScheme

    Card(
        Modifier.fillMaxWidth(),
        colors = CardDefaults.cardColors(containerColor = Color(0xFFFFF4D9)),
    ) {
        Column(Modifier.padding(14.dp), verticalArrangement = Arrangement.spacedBy(5.dp)) {
            Text("当日天气 · 模拟数据", style = MaterialTheme.typography.labelLarge)
            if (scenario != WeatherScenario.EXPIRED) {
                Text("杭州 · 多云 · 16–23°C", style = MaterialTheme.typography.titleLarge)
            }
            Text(
                scenario.status,
                color = if (scenario == WeatherScenario.TODAY) colors.onSurfaceVariant else colors.tertiary,
            )
            Text(
                when {
                    scenario == WeatherScenario.EXPIRED -> "过期天气无法同步"
                    connection != Connection.SYNCED -> "等待手表连接"
                    acknowledged -> "模拟同步成功"
                    else -> "天气待同步"
                },
            )
            OutlinedButton(enabled = canSync && !acknowledged, onClick = { acknowledgedFor = requestKey }) {
                Text("模拟同步天气")
            }
            OutlinedButton(onClick = {
                index = (index + 1) % WeatherScenario.entries.size
                acknowledgedFor = null
            }) {
                Text("切换天气场景")
            }
        }
    }
}
