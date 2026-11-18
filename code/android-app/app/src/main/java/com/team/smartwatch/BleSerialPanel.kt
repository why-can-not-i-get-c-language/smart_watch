package com.team.smartwatch

import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.text.selection.SelectionContainer
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.FilterChip
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.unit.dp

@Composable
internal fun BleSerialPanel(channel: BlunoSerialChannel) {
    var hex by remember { mutableStateOf(true) }
    var input by remember { mutableStateOf("") }
    var inputError by remember { mutableStateOf<String?>(null) }
    var showLegacy by remember { mutableStateOf(false) }
    var password by remember { mutableStateOf("") }
    var baud by remember { mutableStateOf("") }
    val context = LocalContext.current

    Card(Modifier.fillMaxWidth()) {
        Column(Modifier.padding(14.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            Text("串口收发", style = MaterialTheme.typography.titleMedium)
            Text(channel.status)
            Text("本次连接  发送 ${channel.sentBytes} B · 接收 ${channel.receivedBytes} B")
            if (channel.canInitialize) {
                OutlinedButton(onClick = { showLegacy = !showLegacy }) { Text("原厂初始化") }
                if (showLegacy) {
                    Text("如本地监听收不到回包，请填写模块当前的密码及串口波特率。")
                    OutlinedTextField(
                        value = password, onValueChange = { if (it.length <= 32) password = it },
                        label = { Text("模块当前密码") }, singleLine = true,
                        visualTransformation = PasswordVisualTransformation(),
                        modifier = Modifier.fillMaxWidth(),
                    )
                    OutlinedTextField(
                        value = baud, onValueChange = { if (it.length <= 7) baud = it },
                        label = { Text("当前波特率") }, singleLine = true,
                        keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
                        modifier = Modifier.fillMaxWidth(),
                    )
                    OutlinedButton(onClick = {
                        if (channel.initializeLegacy(password, baud)) {
                            password = ""
                            showLegacy = false
                        }
                    }) { Text("按当前参数初始化") }
                }
            }
            if (channel.ready) {
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    FilterChip(selected = hex, onClick = { hex = true; inputError = null }, label = { Text("HEX") })
                    FilterChip(selected = !hex, onClick = { hex = false; inputError = null }, label = { Text("文本") })
                }
                OutlinedTextField(
                    value = input,
                    onValueChange = { if (it.length <= 3072) { input = it; inputError = null } },
                    label = { Text(if (hex) "发送 HEX" else "发送文本（UTF-8）") },
                    placeholder = { Text(if (hex) "31 32 33 34" else "1234") },
                    modifier = Modifier.fillMaxWidth(),
                    minLines = 1, maxLines = 4, isError = inputError != null,
                    supportingText = { inputError?.let { Text(it) } },
                )
                Button(enabled = !channel.busy && input.isNotEmpty(), onClick = {
                    val bytes = serialPayload(input, hex)
                    if (bytes == null) {
                        inputError = if (hex) "输入成对的十六进制数字，最多 512 字节。"
                            else "输入 1 至 512 字节的 UTF-8 文本。"
                    } else {
                        channel.send(bytes)
                    }
                }) { Text("发送一次") }
            }
            if (channel.records.isNotEmpty()) {
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    OutlinedButton(onClick = channel::clearRecords) { Text("清空记录") }
                    OutlinedButton(onClick = {
                        val clipboard = context.getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager
                        clipboard.setPrimaryClip(ClipData.newPlainText("BLE 收发记录", channel.records.joinToString("\n\n")))
                    }) { Text("复制记录") }
                }
                SelectionContainer {
                    Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                        channel.records.asReversed().forEach {
                            Text(it, style = MaterialTheme.typography.bodySmall, fontFamily = FontFamily.Monospace)
                        }
                    }
                }
            }
        }
    }
}
