package com.team.smartwatch

import android.Manifest
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Build
import androidx.core.app.NotificationCompat
import androidx.core.app.NotificationManagerCompat

internal object SosNotification {
    const val OPEN_SOS = "com.team.smartwatch.OPEN_SOS"
    private const val CHANNEL_ID = "sos-alerts"
    private const val NOTIFICATION_ID = 1

    fun isEnabled(context: Context): Boolean {
        if (Build.VERSION.SDK_INT >= 33 &&
            context.checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED
        ) return false
        if (!NotificationManagerCompat.from(context).areNotificationsEnabled()) return false
        if (Build.VERSION.SDK_INT < 26) return true
        val channel = context.getSystemService(NotificationManager::class.java).getNotificationChannel(CHANNEL_ID)
        return channel?.importance != NotificationManager.IMPORTANCE_NONE
    }

    fun show(context: Context, eventId: Long) {
        if (!isEnabled(context)) return
        val manager = context.getSystemService(NotificationManager::class.java)
        if (Build.VERSION.SDK_INT >= 26 && manager.getNotificationChannel(CHANNEL_ID) == null) {
            manager.createNotificationChannel(NotificationChannel(
                CHANNEL_ID, "SOS 求助提醒", NotificationManager.IMPORTANCE_HIGH,
            ).apply { enableVibration(true) })
        }
        val openSos = Intent(context, MainActivity::class.java).setAction(OPEN_SOS)
            .addFlags(Intent.FLAG_ACTIVITY_CLEAR_TOP or Intent.FLAG_ACTIVITY_SINGLE_TOP)
        val contentIntent = PendingIntent.getActivity(context, 0, openSos, PendingIntent.FLAG_IMMUTABLE)
        // 锁屏内容只显示提醒，不携带联系人、号码或短信正文。
        val notification = NotificationCompat.Builder(context, CHANNEL_ID)
            .setSmallIcon(android.R.drawable.stat_sys_warning)
            .setContentTitle("SOS 求助提醒（模拟）")
            .setContentText("手表已发起求助，点此查看处理状态")
            .setVisibility(NotificationCompat.VISIBILITY_PUBLIC)
            .setPriority(NotificationCompat.PRIORITY_HIGH)
            .setDefaults(NotificationCompat.DEFAULT_SOUND or NotificationCompat.DEFAULT_VIBRATE)
            .setOnlyAlertOnce(true)
            .setAutoCancel(true)
            .setContentIntent(contentIntent)
            .build()
        try {
            manager.notify("sos-$eventId", NOTIFICATION_ID, notification)
        } catch (_: SecurityException) {
            // 权限可能在检查后被撤销，仍保留求助记录及其他处理。
        }
    }
}
