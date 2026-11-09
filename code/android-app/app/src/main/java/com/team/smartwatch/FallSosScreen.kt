package com.team.smartwatch

import android.Manifest

import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Build
import android.net.Uri
import android.provider.Settings
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp

@Composable
internal fun FallSosScreen(
    model: FallSosViewModel,
    connection: Connection,
    onBack: () -> Unit,
    onConnection: () -> Unit,
    modifier: Modifier = Modifier,
) {
    var editingContact by rememberSaveable { mutableStateOf(false) }
    var showHistory by rememberSaveable { mutableStateOf(false) }
    val context = LocalContext.current
    if (editingContact) {
        EmergencyContactScreen(model, onBack = { editingContact = false }, modifier = modifier)
        return
    }
    if (showHistory) {
        SosHistoryScreen(model, connection, onBack = { showHistory = false }, modifier = modifier)
        return
    }
    var notificationEnabled by remember { mutableStateOf(SosNotification.isEnabled(context)) }
    var askedNotification by rememberSaveable { mutableStateOf(false) }
    val notificationPermission = rememberLauncherForActivityResult(ActivityResultContracts.RequestPermission()) {
        notificationEnabled = SosNotification.isEnabled(context)
    }
    val notificationSettings = rememberLauncherForActivityResult(ActivityResultContracts.StartActivityForResult()) {
        notificationEnabled = SosNotification.isEnabled(context)
    }
    Column(
        modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(20.dp),
        verticalArrangement = Arrangement.spacedBy(10.dp),
    ) {
        OutlinedButton(onClick = onBack) { Text("返回首页") }
        Text("跌倒 SOS", style = MaterialTheme.typography.headlineMedium)
        Text("模拟手表事件 · 演示倒计时 10 秒", color = MaterialTheme.colorScheme.onSurfaceVariant)
        Text("紧急联系人：${model.contacts.size} 位")
        Text("自动拨号与短信（模拟）：${if (model.autoEscalationEnabled) "已启用" else "未启用"}")
        OutlinedButton(onClick = { editingContact = true }) { Text("紧急联系人设置") }
        OutlinedButton(onClick = { showHistory = true }) { Text("查看求助记录") }
        Text(if (notificationEnabled) "SOS 系统通知已允许" else "SOS 系统通知未开启")
        OutlinedButton(onClick = {
            if (Build.VERSION.SDK_INT >= 33 && !askedNotification &&
                context.checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED
            ) {
                askedNotification = true
                notificationPermission.launch(Manifest.permission.POST_NOTIFICATIONS)
            } else {
                val settings = if (Build.VERSION.SDK_INT >= 26) {
                    Intent(Settings.ACTION_APP_NOTIFICATION_SETTINGS).putExtra(Settings.EXTRA_APP_PACKAGE, context.packageName)
                } else {
                    Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS, Uri.fromParts("package", context.packageName, null))
                }
                notificationSettings.launch(settings)
            }
        }) { Text(if (notificationEnabled) "系统通知设置" else "开启 SOS 通知") }
        if (model.loading) Text("正在恢复求助记录…")
        else if (!model.ready && model.storageError == null) Text("正在保存求助记录…")
        model.storageError?.let { Text(it) }
        Card(
            Modifier.fillMaxWidth(),
            colors = CardDefaults.cardColors(containerColor = Color(0xFFFFE9E5)),
        ) {
            Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
                Text(
                    if (model.awaitingResync) "手表事件状态待同步" else model.stage?.label ?: "暂无求助事件",
                    style = MaterialTheme.typography.titleLarge,
                )
                model.eventId?.let { Text("模拟事件 #$it") }
                if (model.stage == FallEventStage.COUNTDOWN && !model.awaitingResync) {
                    Text("${model.seconds} 秒", style = MaterialTheme.typography.headlineMedium)
                    OutlinedButton(
                        onClick = model::simulateWatchCancel,
                        enabled = model.ready && connection == Connection.SYNCED,
                    ) { Text("模拟手表取消") }
                }
                Text(model.message)
                model.phoneAction?.let {
                    Text("手机操作：模拟打开${it}页")
                    Text("接收方送达：未知")
                }
                Text("手动页面操作次数：${model.operationCount}")
            }
        }
        if (model.callStage != SosCallStage.NONE) {
            Card(Modifier.fillMaxWidth()) {
                Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                    Text("按优先级拨号 · 模拟", style = MaterialTheme.typography.titleMedium)
                    val id = model.eventId
                    val index = model.callIndex
                    val person = model.eventContacts.getOrNull(index)
                    if (person != null) Text("第 ${index + 1} 位 / 共 ${model.eventContacts.size} 位：${person.name}")
                    Text(model.callStage.label)
                    if (id != null && model.callStage == SosCallStage.DIALING) {
                        Text("剩余 ${model.callSeconds} 秒")
                        OutlinedButton(enabled = model.canSimulateCallResult,
                            onClick = { model.simulateCallResult(id, index, answered = true) }) { Text("模拟对方接听") }
                        OutlinedButton(enabled = model.canSimulateCallResult,
                            onClick = { model.simulateCallResult(id, index, answered = false) }) { Text("跳到 30 秒未接（模拟）") }
                    }
                }
            }
        }
        if (model.stage == FallEventStage.REQUESTED || model.smsAttempts > 0) {
            Card(
                Modifier.fillMaxWidth(),
                colors = CardDefaults.cardColors(containerColor = Color(0xFFE2F2ED)),
            ) {
                Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                    Text("SOS 短信 · 模拟", style = MaterialTheme.typography.titleMedium)
                    Text(model.smsMessage)
                    Text("本事件短信批次：${model.smsAttempts}")
                    Text("接收方送达：未知")
                    model.smsSnapshot?.let {
                        Text("本批收件人：${model.smsRecipients.size} 位")
                        model.smsRecipients.forEach { recipient -> Text("${recipient.name} · ${recipient.maskedPhone}") }
                        Text(it.body, style = MaterialTheme.typography.bodySmall)
                    }
                    if (model.smsStage == SmsStage.FAILED) {
                        OutlinedButton(onClick = model::retrySms, enabled = model.canRetrySms) { Text("手动重试短信（模拟）") }
                    }
                    if (model.smsStage == SmsStage.UNKNOWN) Text("发送结果未知，不自动补发；需人工核查。")
                }
            }
        }
        if (model.stage == FallEventStage.REQUESTED && !model.awaitingResync) {
            if (!model.automaticEscalationForEvent) {
                Button(onClick = { model.confirmAndOpen("拨号") }, enabled = model.canConfirm) {
                    Text("确认并模拟打开拨号页")
                }
                OutlinedButton(onClick = { model.confirmAndOpen("短信") }, enabled = model.canConfirm) {
                    Text("确认并模拟打开短信页")
                }
            }
        }
        if (model.awaitingResync) {
            OutlinedButton(
                onClick = { model.resynchronize(cancelled = false) },
                enabled = model.ready && connection == Connection.SYNCED,
            ) { Text("模拟同步已发起阶段") }
            OutlinedButton(
                onClick = { model.resynchronize(cancelled = true) },
                enabled = model.ready && connection == Connection.SYNCED,
            ) { Text("模拟同步已取消阶段") }
        }
        OutlinedButton(onClick = model::simulateFall, enabled = model.canStart) { Text("模拟疑似跌倒") }
        OutlinedButton(onClick = model::simulateManualSos, enabled = model.canStart) { Text("模拟手表 SOS") }
        OutlinedButton(onClick = model::replayEvent, enabled = model.ready && model.eventId != null && connection == Connection.SYNCED) {
            Text("模拟重复事件")
        }
        OutlinedButton(onClick = onConnection) { Text("设备连接") }
        Text("短信演示结果：${model.smsDemoResult.label}")
        OutlinedButton(onClick = model::nextSmsResult, enabled = model.smsStage != SmsStage.SENDING) {
            Text("切换短信演示结果")
        }
        if (model.stage == FallEventStage.REQUESTED && !model.awaitingResync) {
            Text("微信辅助：手动选择联系人发送", style = MaterialTheme.typography.titleMedium)
            OutlinedButton(onClick = {
                val clipboard = context.getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager
                clipboard.setPrimaryClip(ClipData.newPlainText("求助内容", model.shareBody))
                model.copied()
            }) { Text("复制求助内容") }
            OutlinedButton(onClick = { model.shareResult(openWeChatShare(context, model.shareBody)) }) {
                Text("打开微信分享")
            }
            if (model.shareMessage.isNotEmpty()) Text(model.shareMessage)
        }
        if (connection != Connection.SYNCED) Text("请先完成模拟设备连接与状态同步。")
        Text(
            "当前拨号和短信均为模拟。真实跌倒识别、振动和取消由手表完成，正式跌倒倒计时待确认。",
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
    }
}
