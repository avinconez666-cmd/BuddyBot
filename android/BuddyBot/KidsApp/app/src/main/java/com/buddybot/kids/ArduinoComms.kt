package com.buddybot.kids

import android.app.PendingIntent
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.hardware.usb.UsbDevice
import android.hardware.usb.UsbManager
import android.os.Build
import android.util.Log
import androidx.core.content.ContextCompat
import androidx.core.content.IntentCompat
import com.hoho.android.usbserial.driver.CdcAcmSerialDriver
import com.hoho.android.usbserial.driver.Ch34xSerialDriver
import com.hoho.android.usbserial.driver.Cp21xxSerialDriver
import com.hoho.android.usbserial.driver.FtdiSerialDriver
import com.hoho.android.usbserial.driver.ProbeTable
import com.hoho.android.usbserial.driver.ProlificSerialDriver
import com.hoho.android.usbserial.driver.UsbSerialDriver
import com.hoho.android.usbserial.driver.UsbSerialPort
import com.hoho.android.usbserial.driver.UsbSerialProber
import com.hoho.android.usbserial.util.SerialInputOutputManager
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.channels.BufferOverflow
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import okhttp3.OkHttpClient
import okhttp3.Request
import org.json.JSONObject
import java.util.concurrent.Executors
import java.util.concurrent.Future
import java.util.concurrent.TimeUnit
class ArduinoComms(private val context: Context, private val scope: CoroutineScope) {

    companion object {
        private const val TAG = "ArduinoComms"
        private const val ACTION_USB_PERM = "com.buddybot.USB_PERMISSION_ARDUINO"
        private const val ACTION_CAM_PERM = "com.buddybot.USB_PERMISSION_WEBCAM"
        private const val VID_PICO = 0x2E8A
        private val PICO_PIDS = listOf(0x0009, 0x000A, 0x000B, 0x000C, 0x000F, 0xFEED)
        private const val BAUD = BuddyBotConfig.SERIAL_BAUD_RATE
        private const val READ_TIMEOUT  = 200
        private const val WRITE_TIMEOUT = 500
        private const val HEALTH_MS   = 10_000L
        private const val SILENCE_MS  = 15_000L
    }

    val communicationMode = MutableStateFlow(CommunicationMode.DISCONNECTED)
    var onMessageReceived: ((String) -> Unit)? = null
    var onUsbPermissionRequested: (() -> Unit)? = null
    var onUsbPermissionDenied: (() -> Unit)? = null

    @Volatile private var serialPort: UsbSerialPort? = null
    private var ioManager: SerialInputOutputManager? = null
    private val ioExecutor = Executors.newSingleThreadExecutor { r ->
        Thread(r, "usb-serial-io").also { it.isDaemon = true }
    }
    private var ioFuture: Future<*>? = null
    private val lineBuf = StringBuilder(256)

    private val writeQueue = Channel<String>(32, BufferOverflow.DROP_OLDEST)
    private var writeJob: Job? = null

    private var httpIp = ""
    private var httpRetry = 0
    private var httpRetryJob: Job? = null
    private var httpPollJob: Job? = null
    private val http = OkHttpClient.Builder()
        .connectTimeout(10, TimeUnit.SECONDS)
        .readTimeout(10, TimeUnit.SECONDS).build()

    @Volatile private var lastRxMs = System.currentTimeMillis()
    private var healthJob: Job? = null
    private var onCamPerm: ((UsbDevice) -> Unit)? = null

    // Custom prober: adds Pico W VID/PIDs that mik3y may not include by default
    private val prober: UsbSerialProber by lazy {
        val t = ProbeTable()
        PICO_PIDS.forEach { pid -> t.addProduct(VID_PICO, pid, CdcAcmSerialDriver::class.java) }
        t.addProduct(0x1A86, 0x7523, Ch34xSerialDriver::class.java)
        t.addProduct(0x1A86, 0x55D3, Ch34xSerialDriver::class.java)
        t.addProduct(0x10C4, 0xEA60, Cp21xxSerialDriver::class.java)
        t.addProduct(0x0403, 0x6001, FtdiSerialDriver::class.java)
        t.addProduct(0x067B, 0x2303, ProlificSerialDriver::class.java)
        t.addProduct(0x2341, 0x0042, CdcAcmSerialDriver::class.java)
        UsbSerialProber(t)
    }

    private val usbReceiver = object : BroadcastReceiver() {
        override fun onReceive(ctx: Context, intent: Intent) {
            when (intent.action) {
                ACTION_USB_PERM -> {
                    val granted = intent.getBooleanExtra(UsbManager.EXTRA_PERMISSION_GRANTED, false)
                    val dev: UsbDevice? = IntentCompat.getParcelableExtra(intent, UsbManager.EXTRA_DEVICE, UsbDevice::class.java)
                    log("USB permission: granted=$granted dev=${dev?.deviceName}")
                    if (granted && dev != null) openDevice(dev) else onUsbPermissionDenied?.invoke()
                }
                UsbManager.ACTION_USB_DEVICE_ATTACHED -> {
                    val dev: UsbDevice? = IntentCompat.getParcelableExtra(intent, UsbManager.EXTRA_DEVICE, UsbDevice::class.java)
                    if (dev != null && isSerialDevice(dev) && communicationMode.value == CommunicationMode.DISCONNECTED) initializeUSBSerial()
                }
                UsbManager.ACTION_USB_DEVICE_DETACHED -> {
                    val dev: UsbDevice? = IntentCompat.getParcelableExtra(intent, UsbManager.EXTRA_DEVICE, UsbDevice::class.java)
                    if (dev != null && isSerialDevice(dev)) { log("USB detached"); closeSerial() }
                }
            }
        }
    }

    private val camReceiver = object : BroadcastReceiver() {
        override fun onReceive(ctx: Context, intent: Intent) {
            if (intent.action == ACTION_CAM_PERM) {
                val dev: UsbDevice? = IntentCompat.getParcelableExtra(intent, UsbManager.EXTRA_DEVICE, UsbDevice::class.java)
                if (intent.getBooleanExtra(UsbManager.EXTRA_PERMISSION_GRANTED, false)) dev?.let { onCamPerm?.invoke(it) }
            }
        }
    }

    init {
        val f = IntentFilter().apply {
            addAction(ACTION_USB_PERM)
            addAction(UsbManager.ACTION_USB_DEVICE_ATTACHED)
            addAction(UsbManager.ACTION_USB_DEVICE_DETACHED)
        }
        ContextCompat.registerReceiver(context, usbReceiver, f, ContextCompat.RECEIVER_NOT_EXPORTED)
        ContextCompat.registerReceiver(context, camReceiver, IntentFilter(ACTION_CAM_PERM), ContextCompat.RECEIVER_EXPORTED)
        log("ArduinoComms init — mik3y USB serial")
    }

    fun isSerialDevice(device: UsbDevice): Boolean {
        if (isCamera(device)) return false
        val vid = device.vendorId; val pid = device.productId
        if (vid == VID_PICO) return true
        if (vid == 0x2341 || vid == 0x2A03) return true
        if (vid == 0x1A86) return true
        if (vid == 0x10C4 && pid == 0xEA60) return true
        if (vid == 0x0403 && pid == 0x6001) return true
        if (vid == 0x067B && pid == 0x2303) return true
        for (i in 0 until device.interfaceCount) {
            val iface = device.getInterface(i)
            if (iface.interfaceClass == 2 && iface.interfaceSubclass == 2) return true
        }
        return false
    }

    fun isArduino(device: UsbDevice): Boolean = isSerialDevice(device)

    private fun isCamera(d: UsbDevice): Boolean =
        d.deviceClass == 14 || (d.deviceClass == 239 && d.deviceSubclass == 2) || d.vendorId == 0x046D
    fun initialize(ip: String) {
        log("initialize(ip=$ip)")
        initializeUSBSerial()
        scope.launch {
            delay(3_000); if (communicationMode.value == CommunicationMode.DISCONNECTED) initializeUSBSerial()
            delay(3_000)
            if (communicationMode.value == CommunicationMode.DISCONNECTED) {
                initializeUSBSerial()
                delay(2_000)
                if (communicationMode.value == CommunicationMode.DISCONNECTED && ip.isNotEmpty()) {
                    log("USB not found — HTTP fallback to $ip"); initializeHttp(ip)
                }
            }
        }
    }

    fun initializeUSBSerial() {
        val mgr = context.getSystemService(Context.USB_SERVICE) as UsbManager
        val devList = mgr.deviceList
        log("USB scan: ${devList.size} device(s)")
        devList.values.forEach { d ->
            log("  VID=0x${d.vendorId.toString(16).uppercase()} PID=0x${d.productId.toString(16).uppercase()} class=${d.deviceClass} name=${d.deviceName}")
        }
        val drivers = mutableListOf<UsbSerialDriver>()
        devList.values.forEach { d ->
            if (isCamera(d)) return@forEach
            (prober.probeDevice(d) ?: UsbSerialProber.getDefaultProber().probeDevice(d))?.let { drivers.add(it) }
        }
        if (drivers.isEmpty()) { log("No serial device found"); return }
        val driver = drivers.maxByOrNull { when (it.device.vendorId) { VID_PICO -> 100; 0x2341, 0x2A03 -> 50; 0x1A86 -> 40; else -> 10 } }!!
        val device = driver.device
        log("Selected: VID=0x${device.vendorId.toString(16).uppercase()} PID=0x${device.productId.toString(16).uppercase()} ports=${driver.ports.size}")
        if (!mgr.hasPermission(device)) {
            log("Requesting USB permission")
            onUsbPermissionRequested?.invoke()
            val pi = PendingIntent.getBroadcast(context, 0, Intent(ACTION_USB_PERM).setPackage(context.packageName),
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) PendingIntent.FLAG_MUTABLE else 0)
            mgr.requestPermission(device, pi); return
        }
        openDevice(device)
    }

    private fun openDevice(device: UsbDevice) {
        scope.launch(Dispatchers.IO) {
            try {
                val usbMgr = context.getSystemService(Context.USB_SERVICE) as UsbManager
                val driver = prober.probeDevice(device) ?: UsbSerialProber.getDefaultProber().probeDevice(device)
                    ?: run { log("ERROR: no driver for VID=0x${device.vendorId.toString(16)}"); return@launch }
                val conn = usbMgr.openDevice(device) ?: run { log("ERROR: openDevice null"); return@launch }
                val port = driver.ports.firstOrNull() ?: run { log("ERROR: no ports"); conn.close(); return@launch }

                port.open(conn)
                port.setParameters(BAUD, 8, UsbSerialPort.STOPBITS_1, UsbSerialPort.PARITY_NONE)
                try { port.dtr = true } catch (e: Exception) { Log.w(TAG, "DTR: ${e.message}") }
                try { port.rts = true } catch (e: Exception) { Log.w(TAG, "RTS: ${e.message}") }

                serialPort = port

                val listener = object : SerialInputOutputManager.Listener {
                    override fun onNewData(data: ByteArray) {
                        lastRxMs = System.currentTimeMillis()
                        processIncoming(data)
                    }
                    override fun onRunError(e: Exception) {
                        log("IO error: ${e.message}")
                        if (communicationMode.value == CommunicationMode.USB_SERIAL) {
                            communicationMode.value = CommunicationMode.DISCONNECTED
                            scope.launch { delay(1_500); initializeUSBSerial() }
                        }
                    }
                }
                ioManager?.stop(); ioFuture?.cancel(true)
                val iom = SerialInputOutputManager(port, listener).also {
                    it.readTimeout = READ_TIMEOUT; it.writeTimeout = WRITE_TIMEOUT
                }
                ioManager = iom; ioFuture = ioExecutor.submit(iom)

                communicationMode.value = CommunicationMode.USB_SERIAL
                lastRxMs = System.currentTimeMillis()
                log("USB OPEN baud=$BAUD port=${port.portNumber}")
                startWriteJob(); startHealthCheck()
                delay(300); sendCommand("DIAG:RUN")
            } catch (e: Exception) {
                Log.e(TAG, "openDevice error", e)
                communicationMode.value = CommunicationMode.DISCONNECTED
            }
        }
    }

    private fun processIncoming(data: ByteArray) {
        lineBuf.append(String(data, Charsets.UTF_8))
        var nl: Int
        while (lineBuf.indexOf("\n").also { nl = it } >= 0) {
            val msg = normalise(lineBuf.substring(0, nl))
            lineBuf.delete(0, nl + 1)
            if (msg.isNotEmpty()) { log("[RECV] $msg"); onMessageReceived?.invoke(msg) }
        }
        if (lineBuf.length > 2048) { lineBuf.clear(); log("WARN: buf overflow") }
    }

    private fun normalise(raw: String): String {
        var s = raw.trim()
        val ci = s.indexOf("|CRC:"); if (ci > 0) s = s.substring(0, ci)
        return s.removeSuffix("|END").trim()
    }

    private fun startWriteJob() {
        writeJob?.cancel()
        writeJob = scope.launch(Dispatchers.IO) {
            for (payload in writeQueue) {
                val p = serialPort
                if (p == null || !p.isOpen || communicationMode.value != CommunicationMode.USB_SERIAL) {
                    log("WARN: write skipped: ${payload.trim()}"); continue
                }
                try { p.write(payload.toByteArray(Charsets.UTF_8), WRITE_TIMEOUT) } catch (e: Exception) { log("Write err: ${e.message}") }
                delay(20)
            }
        }
    }

    private fun startHealthCheck() {
        healthJob?.cancel()
        healthJob = scope.launch {
            while (isActive) {
                delay(HEALTH_MS)
                if (communicationMode.value == CommunicationMode.USB_SERIAL) {
                    val silence = System.currentTimeMillis() - lastRxMs
                    if (silence > SILENCE_MS) { log("Health: ${silence}ms — reconnect"); closeSerial(); delay(1_500); initializeUSBSerial() }
                    else log("Health OK (${silence}ms)")
                }
            }
        }
    }

    private fun closeSerial() {
        communicationMode.value = CommunicationMode.DISCONNECTED
        try { ioManager?.stop() } catch (_: Exception) {}
        ioManager = null; ioFuture?.cancel(true); ioFuture = null
        try { serialPort?.close() } catch (_: Exception) {}
        serialPort = null; writeJob?.cancel()
        log("Serial closed")
    }

    fun initializeHttp(ip: String) {
        if (ip.isBlank()) return
        log("HTTP: $ip"); httpRetry = 0; httpIp = ip; httpRetryJob?.cancel()
        scope.launch(Dispatchers.IO) {
            try {
                val resp = http.newCall(Request.Builder().url("http://$ip/health").build()).execute()
                if (resp.isSuccessful) { log("HTTP OK"); communicationMode.value = CommunicationMode.HTTP_PICO_W; startHttpPolling(ip) }
                else scheduleHttpRetry(ip)
            } catch (e: Exception) { log("HTTP init: ${e.message}"); scheduleHttpRetry(ip) }
        }
    }

    private fun startHttpPolling(ip: String) {
        httpPollJob?.cancel()
        httpPollJob = scope.launch(Dispatchers.IO) {
            while (isActive && communicationMode.value == CommunicationMode.HTTP_PICO_W) {
                try {
                    val resp = http.newCall(Request.Builder().url("http://$ip/status").build()).execute()
                    if (resp.isSuccessful) parseHttpStatus(resp.body?.string() ?: "")
                } catch (e: Exception) { log("HTTP poll: ${e.message}") }
                delay(1_500)
            }
        }
    }

    private fun parseHttpStatus(json: String) {
        if (json.isBlank()) return
        try {
            val o = JSONObject(json)
            o.optString("mode").takeIf { it.isNotBlank() }?.let { onMessageReceived?.invoke("MODE:$it") }
            val f = o.optInt("front",-1); val r = o.optInt("rear",-1); val l = o.optInt("left",-1); val ri = o.optInt("right",-1)
            if (f>=0||r>=0||l>=0||ri>=0) onMessageReceived?.invoke("US:$f,$r,$l,$ri")
            onMessageReceived?.invoke("TELE:${o.optString("battery","0.0")},${o.optString("pct","0")},0")
            if (o.optInt("flame",0)==1) onMessageReceived?.invoke("ALERT:FLAME_DETECTED")
        } catch (e: Exception) { log("HTTP parse: ${e.message}") }
    }

    private fun scheduleHttpRetry(ip: String) {
        if (httpRetry >= 10) return
        val ms = (1000L * (1L shl httpRetry++)).coerceAtMost(30_000L)
        httpRetryJob?.cancel()
        httpRetryJob = scope.launch { delay(ms); if (communicationMode.value == CommunicationMode.DISCONNECTED) initializeHttp(ip) }
    }

    fun sendCommand(command: String) {
        val clean = command.trimEnd('\r', '\n'); if (clean.isEmpty()) return
        when (communicationMode.value) {
            CommunicationMode.USB_SERIAL -> {
                if (writeQueue.trySend("$clean\n").isSuccess) log("[SEND] USB $clean") else log("WARN: queue full: $clean")
            }
            CommunicationMode.HTTP_PICO_W -> scope.launch(Dispatchers.IO) {
                try {
                    val enc = java.net.URLEncoder.encode(clean, "UTF-8")
                    http.newCall(Request.Builder().url("http://$httpIp/cmd?c=$enc").build()).execute()
                } catch (e: Exception) { Log.e(TAG, "HTTP send: ${e.message}") }
            }
            CommunicationMode.DISCONNECTED -> log("WARN: disconnected, dropped: $clean")
        }
    }

    fun setWebcamPermissionCallback(cb: (UsbDevice) -> Unit) { onCamPerm = cb }

    fun requestWebcamPermission(device: UsbDevice) {
        val pi = PendingIntent.getBroadcast(context, 0, Intent(ACTION_CAM_PERM).setPackage(context.packageName),
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) PendingIntent.FLAG_MUTABLE else 0)
        (context.getSystemService(Context.USB_SERVICE) as UsbManager).requestPermission(device, pi)
    }

    fun unregister() {
        try { context.unregisterReceiver(usbReceiver) } catch (_: Exception) {}
        try { context.unregisterReceiver(camReceiver)  } catch (_: Exception) {}
    }

    fun close() {
        closeSerial(); healthJob?.cancel(); httpRetryJob?.cancel(); httpPollJob?.cancel(); httpIp = ""
        log("ArduinoComms closed")
    }

    private fun log(msg: String) = Log.d(TAG, msg)
}