package com.team.smartwatch

import android.content.ContentValues
import android.content.Context
import android.database.sqlite.SQLiteDatabase
import android.database.sqlite.SQLiteOpenHelper
import org.json.JSONObject

// 当前仅使用单个模拟手表的本地递增 ID。真实设备键/协议 ID 待双方冻结。
internal data class SavedSosEvent(
    val id: Long,
    val source: String,
    val time: String,
    val stage: FallEventStage,
    val body: String,
    val autoEnabled: Boolean,
    val name: String,
    val phone: String,
    val considered: Boolean,
    val smsStage: SmsStage,
    val attempts: Int,
    val smsMessage: String,
    val hasSmsSnapshot: Boolean,
    val phoneAction: String?,
    val contacts: List<SosContact>,
    val callStage: SosCallStage,
    val callIndex: Int,
) {
    fun encode(): String = JSONObject().apply {
        put("version", 2); put("id", id); put("source", source); put("time", time)
        put("stage", stage.name); put("body", body); put("autoEnabled", autoEnabled)
        put("name", name); put("phone", phone); put("considered", considered)
        put("smsStage", smsStage.name); put("attempts", attempts); put("smsMessage", smsMessage)
        put("hasSmsSnapshot", hasSmsSnapshot); put("phoneAction", phoneAction ?: JSONObject.NULL)
        put("contacts", contacts.encodeContacts())
        put("callStage", callStage.name)
        put("callIndex", callIndex)
    }.toString()

    companion object {
        fun decode(raw: String): SavedSosEvent = JSONObject(raw).let {
            val version = it.getInt("version")
            require(version in 1..2) { "Unsupported SOS record version" }
            val name = it.getString("name")
            val phone = it.getString("phone")
            val contacts = if (version == 2) it.getJSONArray("contacts").decodeContacts()
                else listOf(SosContact(name, phone)).filter { contactError(it.name, it.phone) == null }
            SavedSosEvent(
                it.getLong("id"), it.getString("source"), it.getString("time"),
                FallEventStage.valueOf(it.getString("stage")), it.getString("body"),
                it.getBoolean("autoEnabled") && (version == 2 || it.getBoolean("considered")), name, phone,
                it.getBoolean("considered"), SmsStage.valueOf(it.getString("smsStage")),
                it.getInt("attempts"), it.getString("smsMessage"), it.getBoolean("hasSmsSnapshot"),
                if (it.isNull("phoneAction")) null else it.getString("phoneAction"),
                contacts,
                if (version == 2) SosCallStage.valueOf(it.getString("callStage")) else SosCallStage.NONE,
                if (version == 2) it.getInt("callIndex") else -1,
            ).also { record ->
                require(record.id > 0 && record.attempts >= 0)
                require(record.callIndex in -1 until contacts.size)
                if (record.callStage == SosCallStage.DIALING) require(record.callIndex >= 0)
            }
        }
    }
}

// 单表、版本1；不删旧数据，不在升级/读取失败时静默重建。调用方负责在 IO 线程执行。
internal class SosEventStore(private val context: Context) {
    private fun helper() = object : SQLiteOpenHelper(context, "sos-events.db", null, 1) {
        override fun onCreate(db: SQLiteDatabase) {
            db.execSQL("CREATE TABLE events (id INTEGER PRIMARY KEY, payload TEXT NOT NULL)")
        }
        override fun onUpgrade(db: SQLiteDatabase, oldVersion: Int, newVersion: Int) {
            error("SOS database migration required: $oldVersion -> $newVersion")
        }
    }

    fun recent(): List<SavedSosEvent> = helper().use { helper ->
        helper.readableDatabase.rawQuery("SELECT id, payload FROM events ORDER BY id DESC LIMIT 20", null).use { cursor ->
            buildList {
                while (cursor.moveToNext()) {
                    val record = SavedSosEvent.decode(cursor.getString(1))
                    require(record.id == cursor.getLong(0))
                    add(record)
                }
            }
        }
    }

    fun save(event: SavedSosEvent) = helper().use { helper ->
        val values = ContentValues().apply { put("id", event.id); put("payload", event.encode()) }
        check(helper.writableDatabase.insertWithOnConflict("events", null, values, SQLiteDatabase.CONFLICT_REPLACE) != -1L)
    }
}
