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
import com.felhr.usbserial.UsbSerialDevice
import com.felhr.usbserial.UsbSerialInterface
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import okhttp3.*
import java.io.IOException
import java.util.concurrent.TimeUnit

class ArduinoComms(private val context: Context, private val scope: CoroutineScope) {

    companion object {
        private const val TAG = "ArduinoComms"
        private const val ACTION_USB_PERMISSION_ARDUINO = "com.buddybot.USB_PERMISSION_ARDUINO"
        private const val ACTION_USB_PERMISSION_WEBCAM  = "com.buddybot.USB_PERMISSION_WEBCAM"
        // Serial health: no data in 15 s = reconnect
        private const val SERIAL_SILENCE_THRESHOLD_MS = 15_000L
        private const val SERIAL_HEALTH_CHECK_INTERVAL_MS = 10_000L
    }

    private var usbSerial: UsbSerialDevice? = null
    private var httpIp: String = ""

    private val httpClient = OkHttpClient.Builder()
        .connectTimeout(30, TimeUnit.SECONDS)
        .readTimeout(30, TimeUnit.SECONDS)
        .writeTimeout(30, TimeUnit.SECONDS)
        .build()

    val communicationMode = MutableStateFlow(CommunicationMode.DISCONNECTED)
    var onMessageReceived: ((String) -> Unit)? = null

    private val serialBuffer = StringBuilder()

    // HTTP retry backoff
    private var httpRetryCount = 0
    private var httpRetryJob: Job? = null
    private val MAX_HTTP_RETRIES = 10
    private val INITIAL_RETRY_DELAY_MS = 1000L
    private val MAX_RETRY_DELAY_MS = 30_000L

    private var httpPollJob: Job? = null

    // Serial health
    @Volatile private var lastSerialRxMs = System.currentTimeMillis()
    private var serialHealthJob: Job? = null

    // Webcam callback
    private var onUsbPermissionForWebcam: ((UsbDevice) -> Unit)? = null

    // ────────────────────────────────────────────────────────────────────────
    //  BROADCAST RECEIVERS
    // ────────────────────────────────────────────────────────────────────────
    private val usbReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context, intent: Intent) {
            when (intent.action) {
                UsbManager.ACTION_USB_DEVICE_DETACHED -> {
                    val device: UsbDevice? = IntentCompat.getParcelableExtra(
                        intent, UsbManager.EXTRA_DEVICE, UsbDevice::class.java)
                    device?.let {
                        if (isArduino(it)) {
                            log("Arduino detached")
                            communicationMode.value = CommunicationMode.DISCONNECTED
                            serialHealthJob?.cancel()
                            close()
                        }
                    }
                }
                UsbManager.ACTION_USB_DEVICE_ATTACHED -> {
                    val device: UsbDevice? = IntentCompat.getParcelableExtra(
                        intent, UsbManager.EXTRA_DEVICE, UsbDevice::class.java)
                    device?.let {
                        if (isArduino(it) &&
                            communicationMode.value == CommunicationMode.DISCONNECTED) {
                            log("Arduino reattached — auto-reconnecting…")
                            initializeUSBSerial()
                        }
                    }
                }
                ACTION_USB_PERMISSION_ARDUINO -> {
                    synchronized(this) {
                        val device: UsbDevice? = IntentCompat.getParcelableExtra(
                            intent, UsbManager.EXTRA_DEVICE, UsbDevice::class.java)
                        val granted = intent.getBooleanExtra(
                            UsbManager.EXTRA_PERMISSION_GRANTED, false)
                        log("Arduino USB permission result: $granted for ${device?.deviceName}")
                        if (granted && device != null) connectToArduino(device)
                    }
                }
            }
        }
    }

    private val webcamReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context, intent: Intent) {
            if (ACTION_USB_PERMISSION_WEBCAM == intent.action) {
                synchronized(this) {
                    val device: UsbDevice? = IntentCompat.getParcelableExtra(
                        intent, UsbManager.EXTRA_DEVICE, UsbDevice::class.java)
                    if (intent.getBooleanExtra(
                            UsbManager.EXTRA_PERMISSION_GRANTED, false)) {
                        device?.let { onUsbPermissionForWebcam?.invoke(it) }
                    }
                }
            }
        }
    }

    init {
        val filter = IntentFilter().apply {
            addAction(ACTION_USB_PERMISSION_ARDUINO)
            addAction(UsbManager.ACTION_USB_DEVICE_DETACHED)
            addAction(UsbManager.ACTION_USB_DEVICE_ATTACHED)
        }
        ContextCompat.registerReceiver(
            context, usbReceiver, filter, ContextCompat.RECEIVER_NOT_EXPORTED)

        val webcamFilter = IntentFilter(ACTION_USB_PERMISSION_WEBCAM)
        ContextCompat.registerReceiver(
            context, webcamReceiver, webcamFilter, ContextCompat.RECEIVER_EXPORTED)
    }

    // ────────────────────────────────────────────────────────────────────────
    //  DEVICE DETECTION — supports all CH340 variants (Keyestudio Mega)
    // ────────────────────────────────────────────────────────────────────────
    fun isArduino(device: UsbDevice): Boolean {
        val vid = device.vendorId
        val pid = device.productId
        return when {
            vid == 0x2341 -> true                           // All genuine Arduino
            vid == 0x2A03 -> true                           // Arduino.org
            vid == 0x1A86 -> true                           // ALL CH340 variants (Keyestudio, etc.)
            vid == 0x10C4 && pid == 0xEA60 -> true          // CP210x
            vid == 0x0403 && pid == 0x6001 -> true          // FTDI
            vid == 0x067B && pid == 0x2303 -> true          // Prolific
            else -> false
        }
    }

    // ────────────────────────────────────────────────────────────────────────
    //  INITIALIZATION
    // ────────────────────────────────────────────────────────────────────────
    fun initialize(buddybotIP: String) {
        log("Initializing communications (IP=$buddybotIP)")
        initializeUSBSerial()

        scope.launch {
            // First retry at 3 s if USB not found immediately
            delay(3000)
            if (communicationMode.value == CommunicationMode.DISCONNECTED) {
                log("USB serial not found after 3s — retry #1")
                initializeUSBSerial()
            }
            // Second retry at 6 s
            delay(3000)
            if (communicationMode.value == CommunicationMode.DISCONNECTED) {
                log("USB serial not found after 6s — retry #2")
                initializeUSBSerial()
                // Try HTTP fallback if IP is configured
                delay(2000)
                if (communicationMode.value == CommunicationMode.DISCONNECTED &&
                    buddybotIP.isNotEmpty()) {
                    log("Starting HTTP fallback to $buddybotIP")
                    initializeHttp(buddybotIP)
                }
            }
        }
    }

    // ────────────────────────────────────────────────────────────────────────
    //  USB SERIAL
    // ────────────────────────────────────────────────────────────────────────
    fun initializeUSBSerial() {
        try {
            val usbManager = context.getSystemService(Context.USB_SERVICE) as UsbManager
            val deviceList = usbManager.deviceList
            log("Scanning ${deviceList.size} USB device(s) for Arduino…")

            deviceList.values.forEach { d ->
                log("  USB: VID=0x${d.vendorId.toString(16)} PID=0x${d.productId.toString(16)} " +
                    "class=${d.deviceClass} name=${d.deviceName}")
            }

            val arduinoDevice = deviceList.values.find { isArduino(it) }

            if (arduinoDevice == null) {
                log("No Arduino device found (will retry)")
                return
            }

            log("Found Arduino: ${arduinoDevice.deviceName} " +
                "(0x${arduinoDevice.vendorId.toString(16)}:" +
                "0x${arduinoDevice.productId.toString(16)})")

            if (!usbManager.hasPermission(arduinoDevice)) {
                log("Requesting USB permission for Arduino…")
                val permIntent = PendingIntent.getBroadcast(
                    context, 0,
                    Intent(ACTION_USB_PERMISSION_ARDUINO).setPackage(context.packageName),
                    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S)
                        PendingIntent.FLAG_MUTABLE else 0)
                usbManager.requestPermission(arduinoDevice, permIntent)
                return
            }
            connectToArduino(arduinoDevice)
        } catch (e: Exception) {
            Log.e(TAG, "USB init error", e)
        }
    }

    private fun connectToArduino(device: UsbDevice) {
        scope.launch(kotlinx.coroutines.Dispatchers.IO) {
            try {
                log("Connecting to Arduino on background thread…")
                val usbManager = context.getSystemService(Context.USB_SERVICE) as UsbManager
                val connection = usbManager.openDevice(device) ?: run {
                    log("ERROR: Could not open USB device connection")
                    return@launch
                }

                val serial = UsbSerialDevice.createUsbSerialDevice(device, connection)
                if (serial == null) {
                    log("ERROR: No serial driver for " +
                        "VID=0x${device.vendorId.toString(16)} " +
                        "PID=0x${device.productId.toString(16)}")
                    connection.close()
                    return@launch
                }
                usbSerial = serial

                if (serial.open()) {
                    serial.setBaudRate(BuddyBotConfig.SERIAL_BAUD_RATE)
                    serial.setDataBits(UsbSerialInterface.DATA_BITS_8)
                    serial.setStopBits(UsbSerialInterface.STOP_BITS_1)
                    serial.setParity(UsbSerialInterface.PARITY_NONE)
                    serial.setFlowControl(UsbSerialInterface.FLOW_CONTROL_OFF)
                    serial.read { data ->
                        lastSerialRxMs = System.currentTimeMillis()
                        val str = String(data)
                        serialBuffer.append(str)
                        if (serialBuffer.contains("\n")) {
                            val messages = serialBuffer.toString().split("\n")
                            for (i in 0 until messages.size - 1) {
                                val msg = messages[i].trim()
                                if (msg.isNotEmpty()) {
                                    log("[RECV] USB ← Mega: $msg")
                                    onMessageReceived?.invoke(msg)
                                }
                            }
                            val last = messages.last()
                            serialBuffer.setLength(0)
                            serialBuffer.append(last)
                        }
                    }
                    communicationMode.value = CommunicationMode.USB_SERIAL
                    lastSerialRxMs = System.currentTimeMillis()
                    log("✅ Serial connected successfully at ${BuddyBotConfig.SERIAL_BAUD_RATE} baud")
                    startSerialHealthCheck()
                    delay(500)
                    sendCommand("DIAG:RUN")
                } else {
                    log("ERROR: Could not open serial port")
                    connection.close()
                }
            } catch (e: Exception) {
                Log.e(TAG, "USB connect error", e)
            }
        }
    }

    // ────────────────────────────────────────────────────────────────────────
    //  SERIAL HEALTH CHECK — reconnects if Mega goes silent for 15 s
    // ────────────────────────────────────────────────────────────────────────
    private fun startSerialHealthCheck() {
        serialHealthJob?.cancel()
        serialHealthJob = scope.launch {
            while (isActive) {
                delay(SERIAL_HEALTH_CHECK_INTERVAL_MS)
                if (communicationMode.value == CommunicationMode.USB_SERIAL) {
                    val silenceMs = System.currentTimeMillis() - lastSerialRxMs
                    if (silenceMs > SERIAL_SILENCE_THRESHOLD_MS) {
                        log("⚠️ Serial health check: no data in ${silenceMs}ms — reconnecting…")
                        communicationMode.value = CommunicationMode.DISCONNECTED
                        try { usbSerial?.close() } catch (e: Exception) { }
                        usbSerial = null
                        delay(1500)
                        initializeUSBSerial()
                    } else {
                        log("Serial health OK (last rx ${silenceMs}ms ago)")
                    }
                }
            }
        }
    }

    // ────────────────────────────────────────────────────────────────────────
    //  HTTP
    // ────────────────────────────────────────────────────────────────────────
    fun initializeHttp(ip: String) {
        if (ip.isBlank()) {
            log("HTTP: IP not configured — skipping")
            return
        }
        log("Connecting HTTP: http://$ip")

        httpRetryJob?.cancel()
        httpRetryCount = 0
        httpIp = ip

        scope.launch(Dispatchers.IO) {
            try {
                val request = Request.Builder()
                    .url("http://$ip/health")
                    .build()
                val response = httpClient.newCall(request).execute()
                if (response.isSuccessful) {
                    log("✅ HTTP CONNECTED to $ip")
                    communicationMode.value = CommunicationMode.WEBSOCKET
                    httpRetryCount = 0
                    startHttpPolling(ip)
                } else {
                    log("❌ HTTP health check failed: ${response.code}")
                    communicationMode.value = CommunicationMode.DISCONNECTED
                    scheduleHttpReconnect(ip)
                }
            } catch (e: Exception) {
                log("HTTP init error: ${e.message}")
                communicationMode.value = CommunicationMode.DISCONNECTED
                scheduleHttpReconnect(ip)
            }
        }
    }

    private fun startHttpPolling(ip: String) {
        httpPollJob?.cancel()
        httpPollJob = scope.launch(Dispatchers.IO) {
            while (isActive && communicationMode.value == CommunicationMode.WEBSOCKET) {
                try {
                    val request = Request.Builder()
                        .url("http://$ip/status")
                        .build()
                    val response = httpClient.newCall(request).execute()
                    if (response.isSuccessful) {
                        val body = response.body?.string() ?: ""
                        parseHttpStatus(body)
                    }
                } catch (e: Exception) {
                    log("HTTP poll error: ${e.message}")
                }
                delay(1500)
            }
        }
    }

    private fun parseHttpStatus(json: String) {
        try {
            val obj = org.json.JSONObject(json)
            // Mode update
            if (obj.has("mode")) {
                onMessageReceived?.invoke("MODE:${obj.getString("mode")}")
            }
            // Ultrasonic distances
            val front = if (obj.has("front")) obj.getInt("front") else -1
            val rear  = if (obj.has("rear"))  obj.getInt("rear")  else -1
            val left  = if (obj.has("left"))  obj.getInt("left")  else -1
            val right = if (obj.has("right")) obj.getInt("right") else -1
            if (front != -1 || rear != -1 || left != -1 || right != -1) {
                onMessageReceived?.invoke("US:$front,$rear,$left,$right")
            }
            // Battery telemetry
            val voltage = if (obj.has("voltage")) obj.getString("voltage") else
                          if (obj.has("battery")) obj.getString("battery") else "0.0"
            val pct = if (obj.has("batteryPercent")) obj.getString("batteryPercent") else
                      if (obj.has("pct")) obj.getString("pct") else "0"
            onMessageReceived?.invoke("TELE:$voltage,$pct,0")
            // Gas / flame alerts
            val gas = if (obj.has("gas")) obj.getString("gas") else "0"
            val flame = if (obj.has("flame")) obj.getString("flame") else "0"
            if (gas != "0") onMessageReceived?.invoke("ALERT:GAS_ALERT")
            if (flame == "1") onMessageReceived?.invoke("ALERT:FLAME_DETECTED")
        } catch (e: Exception) {
            log("HTTP status parse error: ${e.message}")
        }
    }

    private fun scheduleHttpReconnect(ip: String) {
        if (httpRetryCount >= MAX_HTTP_RETRIES) {
            log("HTTP: max retries reached — giving up")
            return
        }
        httpRetryCount++
        val delayMs = calculateBackoff(httpRetryCount)
        log("HTTP retry #$httpRetryCount in ${delayMs}ms")

        httpRetryJob?.cancel()
        httpRetryJob = scope.launch {
            delay(delayMs)
            if (communicationMode.value == CommunicationMode.DISCONNECTED) {
                initializeHttp(ip)
            }
        }
    }

    private fun calculateBackoff(retry: Int): Long {
        return (INITIAL_RETRY_DELAY_MS * (1L shl (retry - 1)))
            .coerceAtMost(MAX_RETRY_DELAY_MS)
    }

    // ────────────────────────────────────────────────────────────────────────
    //  WEBCAM HELPERS
    // ────────────────────────────────────────────────────────────────────────
    fun setWebcamPermissionCallback(callback: (UsbDevice) -> Unit) {
        onUsbPermissionForWebcam = callback
    }

    fun requestWebcamPermission(device: UsbDevice) {
        val usbManager = context.getSystemService(Context.USB_SERVICE) as UsbManager
        val permIntent = PendingIntent.getBroadcast(
            context, 0,
            Intent(ACTION_USB_PERMISSION_WEBCAM).setPackage(context.packageName),
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S)
                PendingIntent.FLAG_MUTABLE else 0)
        usbManager.requestPermission(device, permIntent)
    }

    // ────────────────────────────────────────────────────────────────────────
    //  SEND COMMAND
    // ────────────────────────────────────────────────────────────────────────
    fun sendCommand(command: String) {
        val clean = command.trimEnd('\r', '\n')
        if (clean.isEmpty()) return
        val payload = "CMD:$clean\n"
        when (communicationMode.value) {
            CommunicationMode.USB_SERIAL -> {
                if (usbSerial?.isOpen() != true) {
                    Log.w(TAG, "Serial not open — dropping command: $clean")
                    return
                }
                try {
                    usbSerial?.write(payload.toByteArray(Charsets.UTF_8))
                    log("[SEND] USB → Mega: $clean")
                } catch (e: IOException) {
                    Log.e(TAG, "Serial write failed: ${e.message}")
                } catch (e: NullPointerException) {
                    Log.e(TAG, "Serial null during write")
                }
            }
            CommunicationMode.WEBSOCKET -> {
                scope.launch(Dispatchers.IO) {
                    try {
                        val cmdParam = if (payload.startsWith("CMD:")) payload.substring(4).trim() else payload.trim()
                        val url = "http://$httpIp/cmd?c=${java.net.URLEncoder.encode(cmdParam, "UTF-8")}"
                        val request = Request.Builder().url(url).build()
                        val response = httpClient.newCall(request).execute()
                        if (response.isSuccessful) {
                            log("[SEND] HTTP → ESP32: $clean")
                        } else {
                            log("[SEND] HTTP error: ${response.code}")
                        }
                    } catch (e: Exception) {
                        Log.e(TAG, "HTTP send failed: ${e.message}")
                    }
                }
            }
            CommunicationMode.DISCONNECTED ->
                log("[WARN] sendCommand ignored (disconnected): $clean")
        }
    }

    // ────────────────────────────────────────────────────────────────────────
    //  CLEANUP
    // ────────────────────────────────────────────────────────────────────────
    fun unregister() {
        try { context.unregisterReceiver(usbReceiver) } catch (e: Exception) { }
        try { context.unregisterReceiver(webcamReceiver) } catch (e: Exception) { }
    }

    fun close() {
        serialHealthJob?.cancel()
        httpRetryJob?.cancel()
        httpPollJob?.cancel()
        communicationMode.value = CommunicationMode.DISCONNECTED
        try { usbSerial?.close() } catch (e: Exception) { }
        usbSerial = null
        httpIp = ""
        log("Communications closed")
    }

    private fun log(message: String) = Log.d(TAG, message)
}
