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
    private var webSocket: WebSocket? = null

    private val httpClient = OkHttpClient.Builder()
        .connectTimeout(30, TimeUnit.SECONDS)
        .readTimeout(30, TimeUnit.SECONDS)
        .writeTimeout(30, TimeUnit.SECONDS)
        .build()

    val communicationMode = MutableStateFlow(CommunicationMode.DISCONNECTED)
    var onMessageReceived: ((String) -> Unit)? = null

    private val serialBuffer = StringBuilder()

    // WebSocket backoff
    private var webSocketRetryCount = 0
    private var webSocketRetryJob: Job? = null
    private val MAX_WEBSOCKET_RETRIES = 10
    private val INITIAL_RETRY_DELAY_MS = 1000L
    private val MAX_RETRY_DELAY_MS = 30_000L

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
                // Try WebSocket fallback if IP is configured
                delay(2000)
                if (communicationMode.value == CommunicationMode.DISCONNECTED &&
                    buddybotIP.isNotEmpty()) {
                    log("Starting WebSocket fallback to $buddybotIP")
                    initializeWebSocket(buddybotIP)
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
    //  WEBSOCKET
    // ────────────────────────────────────────────────────────────────────────
    fun initializeWebSocket(ip: String) {
        if (ip.isBlank()) {
            log("WebSocket: IP not configured — skipping")
            return
        }
        log("Connecting WebSocket: ws://$ip:${BuddyBotConfig.WEBSOCKET_PORT}")

        webSocketRetryJob?.cancel()
        webSocketRetryCount = 0

        try {
            val request = Request.Builder()
                .url("ws://$ip:${BuddyBotConfig.WEBSOCKET_PORT}")
                .build()

            webSocket = httpClient.newWebSocket(request, object : WebSocketListener() {
                override fun onOpen(webSocket: WebSocket, response: Response) {
                    log("✅ WebSocket CONNECTED to $ip")
                    communicationMode.value = CommunicationMode.WEBSOCKET
                    webSocketRetryCount = 0
                }

                override fun onMessage(webSocket: WebSocket, text: String) {
                    log("[RECV] WS ← Mega: $text")
                    onMessageReceived?.invoke(text.trim())
                }

                override fun onFailure(webSocket: WebSocket, t: Throwable, response: Response?) {
                    log("❌ WebSocket FAILURE: ${t.message}")
                    communicationMode.value = CommunicationMode.DISCONNECTED
                    scheduleWebSocketReconnect(ip)
                }

                override fun onClosed(webSocket: WebSocket, code: Int, reason: String) {
                    log("WebSocket CLOSED ($code: $reason)")
                    communicationMode.value = CommunicationMode.DISCONNECTED
                    scheduleWebSocketReconnect(ip)
                }
            })
        } catch (e: Exception) {
            log("WebSocket init error: ${e.message}")
            communicationMode.value = CommunicationMode.DISCONNECTED
            scheduleWebSocketReconnect(ip)
        }
    }

    private fun scheduleWebSocketReconnect(ip: String) {
        if (webSocketRetryCount >= MAX_WEBSOCKET_RETRIES) {
            log("WebSocket: max retries reached — giving up")
            return
        }
        webSocketRetryCount++
        val delayMs = calculateBackoff(webSocketRetryCount)
        log("WebSocket retry #$webSocketRetryCount in ${delayMs}ms")

        webSocketRetryJob?.cancel()
        webSocketRetryJob = scope.launch {
            delay(delayMs)
            if (communicationMode.value == CommunicationMode.DISCONNECTED) {
                initializeWebSocket(ip)
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
        val cmd = "$clean\n"
        when (communicationMode.value) {
            CommunicationMode.USB_SERIAL -> {
                if (usbSerial?.isOpen() != true) {
                    Log.w(TAG, "Serial not open — dropping command: $clean")
                    return
                }
                try {
                    usbSerial?.write(cmd.toByteArray(Charsets.UTF_8))
                    log("[SEND] USB → Mega: $clean")
                } catch (e: IOException) {
                    Log.e(TAG, "Serial write failed: ${e.message}")
                } catch (e: NullPointerException) {
                    Log.e(TAG, "Serial null during write")
                }
            }
            CommunicationMode.WEBSOCKET -> {
                webSocket?.send(clean)
                log("[SEND] WS → Mega: $clean")
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
        webSocketRetryJob?.cancel()
        communicationMode.value = CommunicationMode.DISCONNECTED
        try { usbSerial?.close() } catch (e: Exception) { }
        usbSerial = null
        try { webSocket?.close(1000, "Closing") } catch (e: Exception) { }
        webSocket = null
        log("Communications closed")
    }

    private fun log(message: String) = Log.d(TAG, message)
}
