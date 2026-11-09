package com.team.smartwatch

import android.content.ActivityNotFoundException
import android.content.Context
import android.content.Intent
import org.json.JSONArray
import org.json.JSONObject

internal const val SOS_CONTACT_LIMIT = 5

internal data class SosContact(val name: String = "", val phone: String = "") {
    val maskedPhone: String get() = phone.takeLast(4).padStart(phone.length, '*')
}

internal fun List<SosContact>.encodeContacts(): JSONArray = JSONArray().apply {
    for (contact in this@encodeContacts) {
        put(JSONObject().put("name", contact.name).put("phone", contact.phone))
    }
}

internal fun JSONArray.decodeContacts(): List<SosContact> = List(length()) { i ->
    val contact = getJSONObject(i)
    SosContact(contact.getString("name"), contact.getString("phone"))
}

internal enum class SosCallStage(val label: String) {
    NONE("尚未拨号"), DIALING("等待接听"), ANSWERED("已接听"),
    EXHAUSTED("全部联系人未接听"), INTERRUPTED("拨号中断，等待人工核查"), CANCELLED("已停止拨号"),
}

internal enum class SmsDemoResult(val label: String) {
    SUCCESS("模拟发送成功"),
    FAILURE("模拟发送失败"),
    NO_PERMISSION("模拟短信权限被拒绝"),
    NO_SIM("模拟无可用 SIM 卡"),
    TIMEOUT("模拟发送结果未知"),
    WAIT_FOR_RESTART("模拟等待回执（重启检查）"),
}

internal enum class SmsStage { NOT_SENT, BLOCKED, SENDING, SENT, FAILED, UNKNOWN }

internal data class SosMessage(val contactName: String, val phone: String, val body: String)

// 只检查号码格式，不据此认定号码存在或可收到短信。
internal fun contactError(name: String, phone: String): String? = when {
    name.isBlank() || name.trim().length > 30 -> "联系人称呼需为 1–30 个字符"
    !Regex("^\\+?[0-9]{7,15}$").matches(phone.trim()) -> "号码需为 7–15 位数字，可用 + 开头"
    else -> null
}

// 辅助入口采用 Android 文本分享；微信安装、版本兼容和发送由手机用户确认。
internal fun openWeChatShare(context: Context, body: String): Boolean = try {
    context.startActivity(Intent(Intent.ACTION_SEND).apply {
        type = "text/plain"
        putExtra(Intent.EXTRA_TEXT, body)
        setPackage("com.tencent.mm")
    })
    true
} catch (_: ActivityNotFoundException) {
    false
} catch (_: SecurityException) {
    false
}
