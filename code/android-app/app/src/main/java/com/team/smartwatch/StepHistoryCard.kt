package com.team.smartwatch

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.selection.selectable
import androidx.compose.foundation.selection.selectableGroup
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Card
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.dp
import java.text.NumberFormat
import java.util.Locale

@Composable
internal fun StepHistoryCard(today: Reading<DailySteps>) {
    val days = SimulatedWatch.pastSteps + StepHistoryDay(
        "6",
        SimulatedWatch.DEMO_DATE,
        (today as? Reading.Valid)?.value?.count,
    )
    var selected by rememberSaveable { mutableIntStateOf(days.lastIndex) }
    val formatter = NumberFormat.getIntegerInstance(Locale.CHINA)
    val peak = maxOf(6000, days.maxOf { it.count ?: 0 })
    val colors = MaterialTheme.colorScheme
    val selectedDay = days[selected]
    val selectedCount = selectedDay.count?.let { "${formatter.format(it)} 步" } ?: "无记录"

    Card(Modifier.fillMaxWidth()) {
        Column(Modifier.padding(14.dp), verticalArrangement = Arrangement.spacedBy(5.dp)) {
            Text("近七日步数", style = MaterialTheme.typography.titleMedium)
            Text("10 月 31 日–11 月 6 日 · 模拟历史", style = MaterialTheme.typography.bodySmall)
            Text("${selectedDay.date} · $selectedCount", color = colors.primary)
            Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                Column(Modifier.height(76.dp), verticalArrangement = Arrangement.SpaceBetween) {
                    Text(formatter.format(peak), style = MaterialTheme.typography.labelSmall)
                    Text("0 步", style = MaterialTheme.typography.labelSmall)
                }
                Row(Modifier.weight(1f).selectableGroup()) {
                    days.forEachIndexed { index, day ->
                        val active = index == selected
                        val count = day.count
                        val description = count?.let { "${formatter.format(it)} 步" } ?: "无记录"
                        val color = if (active) colors.primary else Color(0xFFD7774B)
                        Column(
                            Modifier.weight(1f)
                                .selectable(selected = active, role = Role.RadioButton) { selected = index }
                                .semantics { contentDescription = "${day.date}，$description" },
                            horizontalAlignment = Alignment.CenterHorizontally,
                        ) {
                            Box(Modifier.height(76.dp), contentAlignment = Alignment.BottomCenter) {
                                when (count) {
                                    null -> Text("—", color = colors.onSurfaceVariant)
                                    0 -> Box(Modifier.size(6.dp).background(color, CircleShape))
                                    else -> Box(
                                        Modifier.fillMaxWidth(0.56f)
                                            .height((76f * count / peak).dp.coerceAtLeast(2.dp))
                                            .background(color, RoundedCornerShape(topStart = 4.dp, topEnd = 4.dp)),
                                    )
                                }
                            }
                            Text(day.day, color = if (active) colors.primary else colors.onSurfaceVariant)
                        }
                    }
                }
            }
            Text("短横线表示无记录，圆点表示 0 步", style = MaterialTheme.typography.bodySmall)
        }
    }
}
