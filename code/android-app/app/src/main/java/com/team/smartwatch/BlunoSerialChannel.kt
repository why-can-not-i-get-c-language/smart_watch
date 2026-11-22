package com.team.smartwatch

import android.annotation.SuppressLint
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothGattDescriptor
import android.bluetooth.BluetoothStatusCodes
import android.os.Build
import android.os.Handler
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import java.util.UUID

private fun bluetoothUuid(shortId: String) = UUID.fromString("0000$shortId-0000-1000-8000-00805f9b34fb")

/** 所有入口由活动的主线程调用。每个会话仅允许一个待回调的 GATT 操作。 */
@SuppressLint("MissingPermission")
@Suppress("DEPRECATION")
internal class BlunoSerialChannel(
    private val gatt: BluetoothGatt,
    private val handler: Handler,
    private val onFailure: (String) -> Unit,
) {
    private enum class Step(val label: String) {
        SUBSCRIBE("启用 CCCD"), READ_MODEL("读取模块型号"), AUTHENTICATE("写入模块认证命令"),
        UART("初始化串口波特率"), SEND("写入串口数据"),
    }

    private data class Operation(val step: Step, val bytes: ByteArray = byteArrayOf(), var offset: Int = 0)
    private val profile = gatt.getService(bluetoothUuid("dfb0"))
    private val data = profile?.getCharacteristic(bluetoothUuid("dfb1"))
    private val command = profile?.getCharacteristic(bluetoothUuid("dfb2"))
    private val model = gatt.getService(bluetoothUuid("180a"))?.getCharacteristic(bluetoothUuid("2a24"))
    private val cccd = data?.getDescriptor(bluetoothUuid("2902"))
    private val queue = ArrayDeque<Operation>()
    private var pending: Operation? = null
    private var frameLength = 0
    private var listenerEnabled = false
    private var needsReconnect = false
    private var initialized = false
    private val timeFormat = SimpleDateFormat("HH:mm:ss.SSS", Locale.getDefault())
    private val entries = mutableStateListOf<String>()
    val records: List<String> get() = entries
    var status by mutableStateOf("正在初始化接收…")
        private set
    var ready by mutableStateOf(false)
        private set
    var busy by mutableStateOf(false)
        private set
    var sentBytes by mutableStateOf(0L)
        private set
    var receivedBytes by mutableStateOf(0L)
        private set
    val canInitialize: Boolean
        get() = ready && !busy && !initialized && cccd == null &&
            command?.writeTypeForSerial() != null && model?.supports(BluetoothGattCharacteristic.PROPERTY_READ) == true
    private val timeout = Runnable {
        val operation = pending
        if (operation != null) fail(operation.step, "等待回调超时")
    }

    fun prepare() {
        val characteristic = data
        if (characteristic == null) {
            status = "当前设备缺少 DFB0 / DFB1 串口服务。"
            return
        }
        val notify = characteristic.supports(BluetoothGattCharacteristic.PROPERTY_NOTIFY)
        val indicate = characteristic.supports(BluetoothGattCharacteristic.PROPERTY_INDICATE)
        if ((!notify && !indicate) || characteristic.writeTypeForSerial() == null) {
            status = "DFB1 未提供串口写入及通知能力。"
            return
        }
        if (!enableLocalListener()) return
        if (cccd == null) {
            // 已确认的 TEL0073 兼容路径。原厂客户端只在本地登记 DFB1 通知。
            ready = true
            status = "DFB1 本地监听已启用，可尝试收发。"
        } else {
            val value = if (notify) BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
                else BluetoothGattDescriptor.ENABLE_INDICATION_VALUE
            queue.addLast(Operation(Step.SUBSCRIBE, value))
            runNext()
        }
    }

    fun initializeLegacy(password: String, baud: String): Boolean {
        if (!canInitialize) return false
        if (password.isEmpty() || password.length > 32 || password.any { it !in '!'..'~' } ||
            baud.toIntOrNull()?.let { it in 1..1_000_000 } != true || !baud.all { it in '0'..'9' }
        ) {
            status = "请输入模块现有的 ASCII 密码和串口波特率。"
            return false
        }
        ready = false
        queue.addLast(Operation(Step.READ_MODEL))
        queue.addLast(Operation(Step.AUTHENTICATE, "AT+PASSWOR=$password\r\n".toByteArray(Charsets.US_ASCII)))
        queue.addLast(Operation(Step.UART, "AT+CURRUART=$baud\r\n".toByteArray(Charsets.US_ASCII)))
        runNext()
        return true
    }

    fun send(bytes: ByteArray) {
        if (!ready || busy || needsReconnect) return
        queue.addLast(Operation(Step.SEND, bytes.copyOf()))
        record("TX 提交", bytes)
        runNext()
    }

    private fun runNext() {
        val operation = pending ?: queue.removeFirstOrNull()?.also { pending = it } ?: return
        busy = true
        status = "正在${operation.step.label}…"
        val started = try {
            when (operation.step) {
                Step.SUBSCRIBE -> writeDescriptor(checkNotNull(cccd), operation.bytes)
                Step.READ_MODEL -> gatt.readCharacteristic(checkNotNull(model))
                Step.AUTHENTICATE, Step.UART -> writeChunk(checkNotNull(command), operation)
                Step.SEND -> writeChunk(checkNotNull(data), operation)
            }
        } catch (_: SecurityException) {
            fail(operation.step, "蓝牙权限已撤销")
            return
        } catch (_: IllegalStateException) {
            fail(operation.step, "蓝牙连接已不可用")
            return
        }
        if (started) handler.postDelayed(timeout, 8_000L)
        else fail(operation.step, "Android 未接受请求")
    }

    private fun writeDescriptor(descriptor: BluetoothGattDescriptor, bytes: ByteArray): Boolean =
        if (Build.VERSION.SDK_INT >= 33) {
            gatt.writeDescriptor(descriptor, bytes) == BluetoothStatusCodes.SUCCESS
        } else {
            descriptor.value = bytes
            gatt.writeDescriptor(descriptor)
        }

    private fun writeChunk(characteristic: BluetoothGattCharacteristic, operation: Operation): Boolean {
        val bytes = operation.bytes.serialChunkAt(operation.offset)
        frameLength = bytes.size
        val writeType = checkNotNull(characteristic.writeTypeForSerial())
        return if (Build.VERSION.SDK_INT >= 33) {
            gatt.writeCharacteristic(characteristic, bytes, writeType) == BluetoothStatusCodes.SUCCESS
        } else {
            characteristic.writeType = writeType
            characteristic.value = bytes
            gatt.writeCharacteristic(characteristic)
        }
    }

    fun descriptorWritten(descriptor: BluetoothGattDescriptor, result: Int) {
        if (pending?.step != Step.SUBSCRIBE || descriptor.uuid != cccd?.uuid ||
            !sameCharacteristic(descriptor.characteristic, data)) return
        if (result != BluetoothGatt.GATT_SUCCESS) {
            fail(Step.SUBSCRIBE, "GATT 状态 $result")
            return
        }
        completeStep()
        ready = true
        status = "DFB1 通知订阅已完成，可尝试收发。"
    }

    fun characteristicRead(characteristic: BluetoothGattCharacteristic, value: ByteArray, result: Int) {
        if (pending?.step != Step.READ_MODEL || !sameCharacteristic(characteristic, model)) return
        if (result != BluetoothGatt.GATT_SUCCESS || !value.toString(Charsets.US_ASCII).startsWith("DF BLUNO", true)) {
            fail(Step.READ_MODEL, "未确认原厂 DF BLUNO 型号（GATT 状态 $result）")
            return
        }
        completeStep()
        runNext()
    }

    fun characteristicWritten(characteristic: BluetoothGattCharacteristic, result: Int) {
        val operation = pending ?: return
        val target = when (operation.step) {
            Step.AUTHENTICATE, Step.UART -> command
            Step.SEND -> data
            else -> return
        }
        if (!sameCharacteristic(characteristic, target)) return
        handler.removeCallbacks(timeout)
        if (result != BluetoothGatt.GATT_SUCCESS) {
            fail(operation.step, "GATT 状态 $result")
            return
        }
        if (operation.step == Step.SEND) sentBytes += frameLength
        operation.offset += frameLength
        if (operation.offset < operation.bytes.size) {
            runNext()
            return
        }
        completeStep()
        when (operation.step) {
            Step.AUTHENTICATE -> runNext()
            Step.UART -> {
                initialized = true
                if (enableLocalListener()) {
                    ready = true
                    status = "原厂初始化写入已完成，DFB1 本地监听已启用。"
                }
            }
            Step.SEND -> {
                val sent = operation.bytes
                record(if (target?.writeTypeForSerial() == BluetoothGattCharacteristic.WRITE_TYPE_NO_RESPONSE)
                    "TX 无应答写入完成" else "TX GATT 写入完成", sent)
                status = "GATT 写入完成（${sent.size} 字节），回包见 RX。"
            }
            else -> Unit
        }
    }

    fun received(characteristic: BluetoothGattCharacteristic, value: ByteArray) {
        if (!listenerEnabled || needsReconnect || !sameCharacteristic(characteristic, data) || value.isEmpty()) return
        receivedBytes += value.size
        record("RX", value)
    }

    fun clearRecords() = entries.clear()

    fun close() {
        completeStep()
        queue.clear()
        ready = false
        listenerEnabled = false
        if (!needsReconnect) status = "本次串口会话已关闭。"
    }

    private fun enableLocalListener(): Boolean {
        listenerEnabled = try {
            gatt.setCharacteristicNotification(checkNotNull(data), true)
        } catch (_: SecurityException) {
            false
        }
        if (!listenerEnabled) status = "DFB1 本地通知登记失败，请检查权限并重连。"
        return listenerEnabled
    }

    private fun completeStep() {
        handler.removeCallbacks(timeout)
        pending = null
        busy = false
    }

    private fun fail(step: Step, reason: String) {
        needsReconnect = true
        close()
        status = "${step.label}失败（$reason）。请断开并重连。"
        onFailure(status)
    }

    private fun record(label: String, bytes: ByteArray) {
        if (entries.size >= 50) entries.removeAt(0)
        entries.add("${timeFormat.format(Date())}  $label  ${bytes.size} B\n${bytes.serialHex()}")
    }
}

private fun BluetoothGattCharacteristic.supports(flag: Int) = properties and flag != 0

private fun BluetoothGattCharacteristic.writeTypeForSerial(): Int? = when {
    supports(BluetoothGattCharacteristic.PROPERTY_WRITE) -> BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT
    supports(BluetoothGattCharacteristic.PROPERTY_WRITE_NO_RESPONSE) -> BluetoothGattCharacteristic.WRITE_TYPE_NO_RESPONSE
    else -> null
}

private fun sameCharacteristic(actual: BluetoothGattCharacteristic, expected: BluetoothGattCharacteristic?) =
    expected != null && actual.uuid == expected.uuid && actual.instanceId == expected.instanceId &&
        actual.service?.uuid == expected.service?.uuid
