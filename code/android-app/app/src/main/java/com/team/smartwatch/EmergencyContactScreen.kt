package com.team.smartwatch

import androidx.activity.compose.BackHandler
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.dp

@Composable
internal fun EmergencyContactScreen(
    model: FallSosViewModel,
    onBack: () -> Unit,
    modifier: Modifier,
    demonstration: Boolean = true,
) {
    BackHandler(onBack = onBack)
    Column(
        modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(20.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        OutlinedButton(onClick = onBack) { Text(if (demonstration) "返回 SOS" else "返回首页") }
        Text("紧急联系人", style = MaterialTheme.typography.headlineMedium)
        Text("列表从上到下排列优先级。最多 ${SOS_CONTACT_LIMIT} 位，联系人保存在本机。")
        if (!demonstration) Text("手表 SOS 事件接口待固件支持。")
        model.draftContacts.forEachIndexed { index, contact ->
            Text("第 ${index + 1} 位", style = MaterialTheme.typography.titleMedium)
            OutlinedTextField(
                value = contact.name, onValueChange = { model.editContact(index, contact.copy(name = it)) },
                label = { Text("联系人称呼") }, singleLine = true, modifier = Modifier.fillMaxWidth(),
            )
            OutlinedTextField(
                value = contact.phone, onValueChange = { model.editContact(index, contact.copy(phone = it)) },
                label = { Text("手机号码") }, singleLine = true,
                keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Phone),
                modifier = Modifier.fillMaxWidth(),
            )
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                if (index > 0) OutlinedButton(onClick = { model.moveContactUp(index) }) { Text("上移第 ${index + 1} 位") }
                OutlinedButton(onClick = { model.removeContact(index) }) { Text("移除第 ${index + 1} 位") }
            }
        }
        OutlinedButton(onClick = model::addContact, enabled = model.draftContacts.size < SOS_CONTACT_LIMIT) {
            Text("添加联系人")
        }
        if (demonstration) {
            Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                Switch(checked = model.draftEnabled, onCheckedChange = { model.draftEnabled = it })
                Text("自动拨号与短信（模拟）")
            }
            Text("第一位 30 秒未接就向全部联系人发短信，同时继续拨下一位。")
        }
        Button(onClick = { model.saveContact(demonstration) }) { Text("保存联系人设置") }
        if (demonstration) OutlinedButton(onClick = model::fillDemoContact) { Text("填入 3 位演示联系人") }
        if (model.settingsMessage.isNotEmpty()) Text(model.settingsMessage)
    }
}
