package com.team.smartwatch

import android.Manifest
import android.annotation.SuppressLint
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCallback
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothGattDescriptor
import android.bluetooth.BluetoothManager
import android.bluetooth.BluetoothProfile
import android.bluetooth.le.BluetoothLeScanner
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanResult
import android.bluetooth.le.ScanSettings
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.content.pm.PackageManager
import android.location.LocationManager
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import android.provider.Settings
import androidx.activity.ComponentActivity
import androidx.activity.compose.BackHandler
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.key
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import androidx.core.content.ContextCompat
import androidx.core.location.LocationManagerCompat
import androidx.lifecycle.lifecycleScope
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import kotlin.coroutines.resume
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.launch
import kotlinx.coroutines.suspendCancellableCoroutine

@SuppressLint("MissingPermission") // Operations check runtime permissions and handle revocation.
class BleIntegrationActivity : ComponentActivity() {
    private data class Peripheral(val device: BluetoothDevice, val address: String, val entry: BleScanEntry)
    private data class Service(val uuid: String, val characteristics: List<String>)

    private val handler = Handler(Looper.getMainLooper())
    private val timeFormat = SimpleDateFormat("HH:mm:ss.SSS", Locale.getDefault())
    private val adapter get() = getSystemService(BluetoothManager::class.java)?.adapter
    private val askPermission = registerForActivityResult(ActivityResultContracts.RequestMultiplePermissions()) {
        startScan()
    }
    private val peripherals = mutableStateListOf<Peripheral>()
    private val peripheralIndices = mutableMapOf<String, Int>()
    private var services by mutableStateOf<List<Service>>(emptyList())
    private var scanStatus by mutableStateOf("请开启手机蓝牙，并让待联调模块广播。")
    private var connectionStatus by mutableStateOf<String?>(null)
    private var scanning by mutableStateOf(false)
    private var selectedAddress by mutableStateOf<String?>(null)
    private var showingDetails by mutableStateOf(false)
    private var connectingOrConnected by mutableStateOf(false)
    private val interfaceResults = mutableStateListOf<String>()
    private var checkingInterfaces by mutableStateOf(false)
    private var scanner: BluetoothLeScanner? = null
    private var scanCallback: ScanCallback? = null
    private var gatt: BluetoothGatt? = null
    private var serialChannel by mutableStateOf<BlunoSerialChannel?>(null)
    private var d3Watch by mutableStateOf<D3Watch?>(null)
    private val unconfirmedTime = mutableSetOf<String>()
    @Volatile private var phoneClockRevision = 0
    private var scanStartNanos = 0L
    private var scanStartMillis = 0L
    private val scanDeadline = Runnable {
        stopScan()
        scanStatus = scanUnavailableReason() ?: completedScan()
    }
    private val connectionDeadline = Runnable {
        closeConnection(connectionUnavailableReason() ?: "连接或服务发现超时，请重试。")
    }
    private val bluetoothOff = "手机蓝牙已关闭。开启后请重新连接。"
    private val bluetoothPermissionMissing = "请重新授予附近设备权限后连接。"
    private val systemStateReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context, intent: Intent) {
            when (intent.action) {
                BluetoothAdapter.ACTION_STATE_CHANGED -> refreshBluetoothState()
                Intent.ACTION_TIME_CHANGED -> phoneClockRevision++
            }
        }
    }

    private fun d3Clock() = D3Timestamp(SystemClock.elapsedRealtime(), System.currentTimeMillis(), phoneClockRevision)

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        ContextCompat.registerReceiver(
            this, systemStateReceiver, IntentFilter(BluetoothAdapter.ACTION_STATE_CHANGED).apply {
                addAction(Intent.ACTION_TIME_CHANGED)
            },
            ContextCompat.RECEIVER_EXPORTED,
        )
        setContent {
            MaterialTheme {
                val listState = rememberLazyListState()
                val selected = selectedAddress?.let { peripheralIndices[it] }?.let { peripherals[it] }
                BackHandler(enabled = showingDetails) { returnToResults() }
                Scaffold { padding ->
                    if (showingDetails && selected != null) {
                        Column(
                            Modifier.fillMaxSize().padding(padding).verticalScroll(rememberScrollState()).padding(20.dp),
                            verticalArrangement = Arrangement.spacedBy(12.dp),
                        ) {
                            OutlinedButton(onClick = ::returnToResults) { Text("返回扫描列表") }
                            Text("BLE 设备详情", style = MaterialTheme.typography.headlineSmall)
                            PeripheralSummary(selected)
                            val advertised = selected.entry.advertisedServices
                            Text(if (advertised.isEmpty()) "广播服务 UUID：未携带"
                                else "广播服务 UUID\n${advertised.joinToString("\n")}")
                            connectionStatus?.let { Text(it) }
                            if (connectingOrConnected) {
                                OutlinedButton(onClick = { closeConnection("已断开连接。") }) { Text("断开连接") }
                            } else {
                                Button(onClick = { connect(selected) }) { Text("连接并发现 GATT 服务") }
                            }
                            d3Watch?.let { D3WatchPanel(it) }
                            serialChannel?.let { channel ->
                                key(channel) { BleSerialPanel(channel, d3Watch) }
                            }
                            if (services.isNotEmpty()) Text("连接后发现的 GATT 服务", style = MaterialTheme.typography.titleMedium)
                            services.forEach { service ->
                                Text("服务 ${service.uuid}", style = MaterialTheme.typography.titleSmall)
                                service.characteristics.forEach { Text(it, style = MaterialTheme.typography.bodySmall) }
                            }
                        }
                    } else {
                        LazyColumn(
                            modifier = Modifier.fillMaxSize().padding(padding).padding(horizontal = 20.dp),
                            state = listState,
                            verticalArrangement = Arrangement.spacedBy(12.dp),
                        ) {
                            item {
                                Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                                    Text("真实蓝牙联调", style = MaterialTheme.typography.headlineSmall)
                                    Text(scanStatus)
                                    Text("本轮发现 ${peripherals.size} 台")
                                    if (scanning) {
                                        OutlinedButton(onClick = {
                                            stopScan()
                                            scanStatus = completedScan()
                                        }) { Text("停止扫描") }
                                    } else {
                                        Button(onClick = ::requestScan) {
                                            Text(if (scanStartNanos == 0L) "扫描附近 BLE 设备" else "重新扫描")
                                        }
                                        OutlinedButton(onClick = {
                                            startActivity(Intent(Settings.ACTION_BLUETOOTH_SETTINGS))
                                        }) { Text("打开蓝牙设置") }
                                        OutlinedButton(enabled = !checkingInterfaces, onClick = ::runOfflineCheck) {
                                            Text("运行离线接口验收")
                                        }
                                        interfaceResults.forEach { Text(it, style = MaterialTheme.typography.bodySmall) }
                                    }
                                }
                            }
                            items(peripherals, key = { it.address }) { item ->
                                Card(onClick = {
                                    selectedAddress = item.address
                                    connectionStatus = null
                                    showingDetails = true
                                }, modifier = Modifier.fillMaxWidth()) {
                                    Column(Modifier.padding(12.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
                                        PeripheralSummary(item)
                                        if (item.address == selectedAddress) Text("已选设备")
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    @Composable
    private fun PeripheralSummary(item: Peripheral) {
        val entry = item.entry
        val lastSeenMillis = scanStartMillis + (entry.timestampNanos - scanStartNanos) / 1_000_000L
        Text(entry.name, style = MaterialTheme.typography.titleMedium)
        Text(item.address)
        Text("RSSI ${entry.rssi?.let { "$it dBm" } ?: "未知"} · 最后发现 ${timeFormat.format(Date(lastSeenMillis))}",
            style = MaterialTheme.typography.bodySmall)
    }

    private fun returnToResults() {
        closeConnection(null)
        serialChannel = null
        d3Watch = null
        showingDetails = false
    }

    private fun runOfflineCheck() {
        checkingInterfaces = true
        interfaceResults.clear()
        lifecycleScope.launch {
            try {
                checkOfflineInterfaces { interfaceResults.add(it) }
                interfaceResults.add("离线接口验收完成")
            } catch (cancelled: CancellationException) {
                throw cancelled
            } catch (error: Exception) {
                interfaceResults.add("离线验收失败：${error.message ?: error.javaClass.simpleName}")
            } finally {
                checkingInterfaces = false
            }
        }
    }

    private fun hasPermission(permission: String) = checkSelfPermission(permission) == PackageManager.PERMISSION_GRANTED

    private fun hasNearbyPermissions() = Build.VERSION.SDK_INT < 31 ||
        (hasPermission(Manifest.permission.BLUETOOTH_SCAN) && hasPermission(Manifest.permission.BLUETOOTH_CONNECT))

    private fun connectionUnavailableReason(): String? = try {
        val available = adapter
        when {
            Build.VERSION.SDK_INT >= 31 && !hasPermission(Manifest.permission.BLUETOOTH_CONNECT) ->
                bluetoothPermissionMissing
            available == null -> "此手机没有可用的蓝牙适配器。"
            !available.isEnabled -> bluetoothOff
            else -> null
        }
    } catch (_: SecurityException) {
        bluetoothPermissionMissing
    }

    private fun refreshBluetoothState() {
        val problem = connectionUnavailableReason()
        if (problem != null) {
            if (scanning) {
                stopScan()
                scanStatus = scanUnavailableReason() ?: completedScan()
            } else if (problem == bluetoothOff) {
                scanStatus = "扫描失败：$bluetoothOff"
            }
            if (gatt != null || (showingDetails && problem == bluetoothOff)) closeConnection(problem)
        } else {
            if (scanStatus == "扫描失败：$bluetoothOff") {
                scanStatus = "手机蓝牙已开启，可开始扫描或从本轮列表重新连接。"
            }
            if (connectionStatus == bluetoothOff) connectionStatus = "手机蓝牙已开启，请重新连接设备。"
        }
    }

    private fun requestScan() {
        if (scanning || gatt != null) return
        val missing = mutableListOf<String>()
        if (!hasNearbyPermissions()) {
            missing.add(Manifest.permission.BLUETOOTH_SCAN)
            missing.add(Manifest.permission.BLUETOOTH_CONNECT)
        }
        if (!hasPermission(Manifest.permission.ACCESS_FINE_LOCATION)) {
            // Android 12+ 要求同时请求粗略和精确位置，由用户选择精度。
            missing.add(Manifest.permission.ACCESS_COARSE_LOCATION)
            missing.add(Manifest.permission.ACCESS_FINE_LOCATION)
        }
        if (missing.isEmpty()) startScan() else askPermission.launch(missing.toTypedArray())
    }

    private fun scanUnavailableReason(): String? = try {
        val available = adapter
        when {
            !hasNearbyPermissions() -> "扫描失败：未授权「附近设备」。请允许蓝牙扫描和连接。"
            available == null -> "扫描失败：此手机没有可用的蓝牙适配器。"
            !available.isEnabled -> "扫描失败：$bluetoothOff"
            !hasPermission(Manifest.permission.ACCESS_FINE_LOCATION) ->
                "扫描失败：完整扫描需要前台精确位置权限。请在系统权限中授权。"
            !LocationManagerCompat.isLocationEnabled(getSystemService(LocationManager::class.java)) ->
                "扫描失败：系统定位已关闭，请开启后重试。"
            else -> null
        }
    } catch (_: SecurityException) {
        "扫描失败：蓝牙权限已变化，请重新授权。"
    }

    private fun completedScan() = if (peripherals.isEmpty()) "扫描结束：未发现设备。"
        else "扫描结束：请从本轮结果选择设备。"

    private fun startScan() {
        if (scanning || gatt != null || showingDetails) return
        peripherals.clear()
        peripheralIndices.clear()
        selectedAddress = null
        scanUnavailableReason()?.let {
            scanStatus = it
            return
        }
        try {
            val activeScanner = adapter?.bluetoothLeScanner
            if (activeScanner == null) {
                scanStatus = scanUnavailableReason() ?: "扫描失败：系统未提供 BLE 扫描器。"
                return
            }
            val callback = object : ScanCallback() {
                override fun onScanResult(callbackType: Int, result: ScanResult) {
                    handler.post {
                        if (scanCallback === this) recordAdvertisement(result)
                    }
                }

                override fun onBatchScanResults(results: MutableList<ScanResult>) {
                    handler.post {
                        if (scanCallback === this) results.forEach(::recordAdvertisement)
                    }
                }

                override fun onScanFailed(errorCode: Int) {
                    handler.post {
                        if (scanCallback !== this) return@post
                        stopScan()
                        scanStatus = scanUnavailableReason() ?: scanFailureMessage(errorCode)
                    }
                }
            }
            val settings = ScanSettings.Builder()
                .setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY)
                .setCallbackType(ScanSettings.CALLBACK_TYPE_ALL_MATCHES)
                .apply {
                    if (Build.VERSION.SDK_INT >= 26 && adapter?.isLeExtendedAdvertisingSupported == true) {
                        setLegacy(false)
                    }
                }
                .build()
            scanStartNanos = SystemClock.elapsedRealtimeNanos()
            scanStartMillis = System.currentTimeMillis()
            scanner = activeScanner
            scanCallback = callback
            activeScanner.startScan(emptyList(), settings, callback)
            scanning = true
            scanStatus = "扫描中（12 秒），可选择已发现的设备。"
            handler.postDelayed(scanDeadline, 12_000L)
        } catch (_: SecurityException) {
            stopScan()
            scanStatus = scanUnavailableReason() ?: "扫描失败：系统拒绝蓝牙扫描权限。"
        } catch (_: IllegalStateException) {
            stopScan()
            scanStatus = scanUnavailableReason() ?: "扫描失败：蓝牙状态已变化，请重试。"
        }
    }

    private fun recordAdvertisement(result: ScanResult) {
        // 忽略系统延迟交付的上一轮结果。时间戳缺失时以收到回调的时间为准。
        val timestamp = result.timestampNanos.takeIf { it > 0L } ?: SystemClock.elapsedRealtimeNanos()
        if (timestamp < scanStartNanos) return
        val device = result.device
        val address = device.address.uppercase(Locale.ROOT)
        val record = result.scanRecord
        val advertisedName = record?.deviceName
        val cachedName = if (advertisedName.isNullOrBlank()) {
            try {
                device.name
            } catch (_: SecurityException) {
                null
            }
        } else null
        val observation = BleScanEntry.fromAdvertisement(
            advertisedName = advertisedName,
            cachedName = cachedName,
            timestampNanos = timestamp,
            rssi = result.rssi,
            advertisedServices = record?.serviceUuids.orEmpty().map { it.toString() }.toSet(),
            connectable = if (Build.VERSION.SDK_INT >= 26) result.isConnectable else null,
        )
        val index = peripheralIndices[address]
        if (index == null) {
            peripheralIndices[address] = peripherals.size
            peripherals.add(Peripheral(device, address, observation))
        } else {
            val previous = peripherals[index]
            val latestDevice = if (timestamp >= previous.entry.timestampNanos) device else previous.device
            peripherals[index] = Peripheral(latestDevice, address, previous.entry.update(observation))
        }
    }

    private fun scanFailureMessage(code: Int): String {
        val reason = when (code) {
            ScanCallback.SCAN_FAILED_ALREADY_STARTED -> "系统已有扫描在运行"
            ScanCallback.SCAN_FAILED_APPLICATION_REGISTRATION_FAILED -> "扫描客户端注册失败"
            ScanCallback.SCAN_FAILED_INTERNAL_ERROR -> "蓝牙堆栈内部错误"
            ScanCallback.SCAN_FAILED_FEATURE_UNSUPPORTED -> "设备不支持此 BLE 扫描模式"
            ScanCallback.SCAN_FAILED_OUT_OF_HARDWARE_RESOURCES -> "蓝牙控制器资源不足"
            ScanCallback.SCAN_FAILED_SCANNING_TOO_FREQUENTLY -> "扫描过于频繁，请稍后重试"
            else -> "系统报告未知错误"
        }
        return "扫描失败：$reason（$code）。"
    }

    private fun stopScan() {
        handler.removeCallbacks(scanDeadline)
        val callback = scanCallback
        scanCallback = null
        scanning = false
        if (callback != null) {
            try {
                scanner?.stopScan(callback)
            } catch (_: SecurityException) {
                // Permission can be revoked while the scanner is active.
            } catch (_: IllegalStateException) {
                // The adapter can turn off during the scan.
            }
        }
        scanner = null
    }

    private fun connect(item: Peripheral) {
        if (gatt != null) return
        serialChannel = null
        d3Watch = null
        if (scanning) {
            stopScan()
            scanStatus = completedScan()
        }
        connectionUnavailableReason()?.let {
            connectionStatus = it
            return
        }
        try {
            val connection = item.device.connectGatt(this, false, callback, BluetoothDevice.TRANSPORT_LE)
            if (connection == null) {
                connectionStatus = "无法发起连接，请重试。"
                return
            }
            gatt = connection
            connectingOrConnected = true
            connectionStatus = "正在连接…"
            handler.postDelayed(connectionDeadline, 15_000L)
        } catch (_: SecurityException) {
            connectionStatus = "蓝牙权限已变化，请重新授权。"
        }
    }

    private val callback = object : BluetoothGattCallback() {
        override fun onConnectionStateChange(connection: BluetoothGatt, result: Int, newState: Int) {
            handler.post {
                if (gatt !== connection) return@post
                when {
                    result != BluetoothGatt.GATT_SUCCESS || newState == BluetoothProfile.STATE_DISCONNECTED ->
                        closeConnection(connectionUnavailableReason() ?: if (result == BluetoothGatt.GATT_SUCCESS) {
                            "设备已断开，请重新连接。"
                        } else {
                            "连接失败或中断（$result），请重新连接。"
                        })
                    newState == BluetoothProfile.STATE_CONNECTED -> {
                        connectionStatus = "BLE 已连接，正在发现 GATT 服务…"
                        try {
                            if (!connection.discoverServices()) closeConnection("无法开始服务发现，请重试。")
                        } catch (_: SecurityException) {
                            closeConnection("蓝牙权限已变化，请重新授权。")
                        }
                    }
                }
            }
        }

        override fun onServicesDiscovered(connection: BluetoothGatt, result: Int) {
            handler.post {
                if (gatt !== connection) return@post
                handler.removeCallbacks(connectionDeadline)
                connectionUnavailableReason()?.let {
                    closeConnection(it)
                    return@post
                }
                if (result != BluetoothGatt.GATT_SUCCESS || connection.services.isEmpty()) {
                    closeConnection("未能发现 GATT 服务（$result），请重试。")
                    return@post
                }
                services = connection.services.map { service ->
                    Service(service.uuid.toString(), service.characteristics.map(::describe))
                }
                connectionStatus = "BLE 已连接，发现 ${services.size} 个 GATT 服务。"
                val channel = BlunoSerialChannel(connection, handler) { failure ->
                    if (gatt === connection) closeConnection(connectionUnavailableReason() ?: failure)
                }
                val address = checkNotNull(selectedAddress)
                val client = D3Client(
                    write = { bytes -> suspendCancellableCoroutine { result ->
                        channel.send(bytes) { if (result.isActive) result.resume(it) }
                    } },
                    clock = ::d3Clock,
                )
                val watch = D3Watch(lifecycleScope, client, { channel.ready && !channel.busy },
                    address in unconfirmedTime, { pending ->
                        if (pending) unconfirmedTime.add(address) else unconfirmedTime.remove(address)
                    })
                d3Watch = watch
                channel.onReady = watch::transportReady
                channel.onReceived = client::receive
                serialChannel = channel
                channel.prepare()
            }
        }

        override fun onDescriptorWrite(connection: BluetoothGatt, descriptor: BluetoothGattDescriptor, status: Int) {
            handler.post {
                if (gatt === connection) serialChannel?.descriptorWritten(descriptor, status)
            }
        }

        override fun onCharacteristicWrite(connection: BluetoothGatt, characteristic: BluetoothGattCharacteristic, status: Int) {
            handler.post {
                if (gatt === connection) serialChannel?.characteristicWritten(characteristic, status)
            }
        }

        override fun onCharacteristicRead(
            connection: BluetoothGatt, characteristic: BluetoothGattCharacteristic, value: ByteArray, status: Int,
        ) {
            val snapshot = value.copyOf()
            handler.post {
                if (gatt === connection) serialChannel?.characteristicRead(characteristic, snapshot, status)
            }
        }

        @Suppress("DEPRECATION", "OVERRIDE_DEPRECATION")
        override fun onCharacteristicRead(connection: BluetoothGatt, characteristic: BluetoothGattCharacteristic, status: Int) {
            if (Build.VERSION.SDK_INT < 33) {
                onCharacteristicRead(connection, characteristic, characteristic.value ?: byteArrayOf(), status)
            }
        }

        override fun onCharacteristicChanged(
            connection: BluetoothGatt, characteristic: BluetoothGattCharacteristic, value: ByteArray,
        ) {
            val snapshot = value.copyOf()
            val arrival = d3Clock()
            handler.post {
                if (gatt === connection) serialChannel?.received(characteristic, snapshot, arrival)
            }
        }

        @Suppress("DEPRECATION", "OVERRIDE_DEPRECATION")
        override fun onCharacteristicChanged(connection: BluetoothGatt, characteristic: BluetoothGattCharacteristic) {
            if (Build.VERSION.SDK_INT < 33) {
                onCharacteristicChanged(connection, characteristic, characteristic.value ?: byteArrayOf())
            }
        }
    }

    private fun describe(characteristic: BluetoothGattCharacteristic): String {
        val flags = listOf(
            BluetoothGattCharacteristic.PROPERTY_READ to "读",
            BluetoothGattCharacteristic.PROPERTY_WRITE to "写",
            BluetoothGattCharacteristic.PROPERTY_WRITE_NO_RESPONSE to "无应答写",
            BluetoothGattCharacteristic.PROPERTY_NOTIFY to "通知",
            BluetoothGattCharacteristic.PROPERTY_INDICATE to "指示",
        ).filter { (flag, _) -> characteristic.properties and flag != 0 }.joinToString(" / ") { it.second }
        return "${characteristic.uuid} · ${flags.ifEmpty { "其他属性" }}"
    }

    private fun closeConnection(message: String?) {
        handler.removeCallbacks(connectionDeadline)
        val connection = gatt
        gatt = null
        d3Watch?.close()
        serialChannel?.close()
        try {
            connection?.close()
        } catch (_: SecurityException) {
            // Android may revoke the permission before cleanup.
        }
        connectingOrConnected = false
        services = emptyList()
        connectionStatus = message
    }

    override fun onResume() {
        super.onResume()
        refreshBluetoothState()
    }

    override fun onStop() {
        super.onStop()
        if (scanning) {
            stopScan()
            scanStatus = completedScan()
        }
        // Home、锁屏和暂时打开系统设置时，联调页仍可恢复同一个 GATT 会话。
    }

    override fun onDestroy() {
        unregisterReceiver(systemStateReceiver)
        stopScan()
        closeConnection(null)
        super.onDestroy()
    }
}
