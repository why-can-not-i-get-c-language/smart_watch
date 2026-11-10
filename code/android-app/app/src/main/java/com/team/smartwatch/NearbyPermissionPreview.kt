package com.team.smartwatch

import android.Manifest
import android.content.Intent
import android.content.pm.PackageManager
import android.net.Uri
import android.os.Build
import android.provider.Settings
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Card
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp

@Composable
internal fun NearbyPermissionPreview() {
    val context = LocalContext.current
    val supported = Build.VERSION.SDK_INT >= Build.VERSION_CODES.S
    val required = arrayOf(Manifest.permission.BLUETOOTH_SCAN, Manifest.permission.BLUETOOTH_CONNECT)
    fun hasPermission(): Boolean = supported && required.all {
        context.checkSelfPermission(it) == PackageManager.PERMISSION_GRANTED
    }
    var allowed by remember { mutableStateOf(hasPermission()) }
    val request = rememberLauncherForActivityResult(ActivityResultContracts.RequestMultiplePermissions()) {
        allowed = hasPermission()
    }
    val settings = rememberLauncherForActivityResult(ActivityResultContracts.StartActivityForResult()) {
        allowed = hasPermission()
    }

    Card(Modifier.fillMaxWidth()) {
        Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            Text("蓝牙权限演示", style = MaterialTheme.typography.titleMedium)
            Text("只申请系统权限。授权后仍不会扫描、连接或配对设备。")
            if (supported) {
                Text(if (allowed) "附近设备权限：已允许" else "附近设备权限：未允许")
                OutlinedButton(enabled = !allowed, onClick = { request.launch(required) }) {
                    Text("申请附近设备权限")
                }
                OutlinedButton(onClick = {
                    settings.launch(Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS,
                        Uri.fromParts("package", context.packageName, null)))
                }) { Text("打开应用权限设置") }
            } else {
                Text("附近设备授权弹窗适用于 Android 12 及以上。")
            }
        }
    }
}
