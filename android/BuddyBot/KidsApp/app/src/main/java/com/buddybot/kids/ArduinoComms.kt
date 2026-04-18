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
import kotlinx.coroutines.launch
import okhttp3.*
import java.io.IOException
import java.util.concurrent.TimeUnit

class ArduinoComms(private val context: Context, private val scope: CoroutineScope) {
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
    private val ACTION_USB_PERMISSION = "com.buddybot.USB_PERMISSION_ARDUINO"
    
    // WebSocket reconnection with exponential backoff
    private var webSocketRetryCount = 0
    private var webSocketRetryJob: Job? = null
    private val MAX_WEBSOCKET_RETRIES = 10
    private val INITIAL_RETRY_DELAY_MS = 1000L
    private val MAX_RETRY_DELAY_MS = 30000L

    private val usbReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context, intent: Intent) {
            when (intent.action) {
                UsbManager.ACTION_USB_DEVICE_DETACHED -> {
                    val device: UsbDevice? = IntentCompat.getParcelableExtra(intent, UsbManager.EXTRA_DEVICE, UsbDevice::class.java)
                    device?.let {
                        if (isArduino(it)) {
                            log("Arduino detached.")
                            communicationMode.value = CommunicationMode.DISCONNECTED
                            close()
                        }
                    }
                }
                // FIX #7: when the cable is replugged, automatically kick off
                // initializeUSBSerial() which handles permission + connection.
                UsbManager.ACTION_USB_DEVICE_ATTACHED -> {
                    val device: UsbDevice? = IntentCompat.getParcelableExtra(intent, UsbManager.EXTRA_DEVICE, UsbDevice::class.java)
                    device?.let {
                        if (isArduino(it) && communicationMode.value == CommunicationMode.DISCONNECTED) {
                            log("Arduino reattached — attempting auto-reconnect...")
                            initializeUSBSerial()
                        }
                    }
                }
                ACTION_USB_PERMISSION -> {
                    synchronized(this) {
                        val device: UsbDevice? = IntentCompat.getParcelableExtra(intent, UsbManager.EXTRA_DEVICE, UsbDevice::class.java)
                        val granted = intent.getBooleanExtra(UsbManager.EXTRA_PERMISSION_GRANTED, false)
                        log("USB Permission result: $granted for ${device?.deviceName}")
                        if (granted && device != null) {
                            connectToArduino(device)
                        }
                    }
                }
            }
        }
    }

    private var onUsbPermissionForWebcam: ((UsbDevice) -> Unit)? = null

    private val webcamReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context, intent: Intent) {
            if ("com.buddybot.USB_PERMISSION_WEBCAM" == intent.action) {
                synchronized(this) {
                    val device: UsbDevice? = IntentCompat.getParcelableExtra(intent, UsbManager.EXTRA_DEVICE, UsbDevice::class.java)
                    if (intent.getBooleanExtra(UsbManager.EXTRA_PERMISSION_GRANTED, false)) {
                        device?.let { onUsbPermissionForWebcam?.invoke(it) }
                    }
                }
            }
        }
    }

    init {
        // FIX #7: also register for ATTACHED so the receiver fires on replug
        val filter = IntentFilter().apply {
            addAction(ACTION_USB_PERMISSION)
            addAction(UsbManager.ACTION_USB_DEVICE_DETACHED)
            addAction(UsbManager.ACTION_USB_DEVICE_ATTACHED)
        }
        ContextCompat.registerReceiver(context, usbReceiver, filter, ContextCompat.RECEIVER_NOT_EXPORTED)

        val webcamFilter = IntentFilter("com.buddybot.USB_PERMISSION_WEBCAM")
        ContextCompat.registerReceiver(context, webcamReceiver, webcamFilter, ContextCompat.RECEIVER_EXPORTED)
    }

    fun setWebcamPermissionCallback(callback: (UsbDevice) -> Unit) {
        onUsbPermissionForWebcam = callback
    }

    fun requestWebcamPermission(device: UsbDevice) {
        val usbManager = context.getSystemService(Context.USB_SERVICE) as UsbManager
        val permissionIntent = PendingIntent.getBroadcast(
            context,
            0,
            Intent("com.buddybot.USB_PERMISSION_WEBCAM").setPackage(context.packageName),
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) PendingIntent.FLAG_MUTABLE else 0
        )
        usbManager.requestPermission(device, permissionIntent)
    }

    fun initialize(buddybotIP: String) {
        log("Initializing communication (IP: $buddybotIP)")
        initializeUSBSerial()
        scope.launch {
            delay(2000)
            if (communicationMode.value == CommunicationMode.DISCONNECTED && buddybotIP.isNotEmpty()) {
                initializeWebSocket(buddybotIP)
            }
        }
    }

    fun isArduino(device: UsbDevice): Boolean {
        val vid = device.vendorId
        val pid = device.productId
        // Common Arduino & USB-Serial VIDs/PIDs
        return (vid == 0x2341 && (pid == 0x0042 || pid == 0x0043 || pid == 0x0001 || pid == 0x0010)) || // Arduino (ATmega16U2)
               (vid == 0x2A03) || // Arduino.org
               (vid == 0x1A86 && (pid == 0x7523 || pid == 0x7522)) || // CH340 / CH340C (clone Megas)
               (vid == 0x10C4 && pid == 0xEA60) || // CP210x
               (vid == 0x0403 && pid == 0x6001) || // FTDI
               (vid == 0x067B && pid == 0x2303)    // Prolific
    }

    fun initializeUSBSerial() {
        try {
            val usbManager = context.getSystemService(Context.USB_SERVICE) as UsbManager
            val deviceList = usbManager.deviceList
            log("Searching for Arduino in ${deviceList.size} USB devices...")
            
            val arduinoDevice = deviceList.values.find { isArduino(it) }

            if (arduinoDevice == null) {
                log("No Arduino device found.")
                return
            }

            log("Found Arduino: ${arduinoDevice.deviceName} (0x${Integer.toHexString(arduinoDevice.vendorId)}:0x${Integer.toHexString(arduinoDevice.productId)})")

            if (!usbManager.hasPermission(arduinoDevice)) {
                log("Requesting USB permission for Arduino...")
                val permissionIntent = PendingIntent.getBroadcast(
                    context, 
                    0, 
                    Intent(ACTION_USB_PERMISSION).setPackage(context.packageName), 
                    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) PendingIntent.FLAG_MUTABLE else 0
                )
                usbManager.requestPermission(arduinoDevice, permissionIntent)
                return
            }
            connectToArduino(arduinoDevice)
        } catch (e: Exception) { 
            Log.e("ArduinoComms", "USB Init error", e)
        }
    }

    // FIX #6: connectToArduino() now runs entirely on a background coroutine so that
    // blocking USB I/O (openDevice, open, setBaudRate …) never touches the main thread.
    private fun connectToArduino(device: UsbDevice) {
        scope.launch(kotlinx.coroutines.Dispatchers.IO) {
            try {
                log("Connecting to Arduino serial (background thread)...")
                val usbManager = context.getSystemService(Context.USB_SERVICE) as UsbManager
                val connection = usbManager.openDevice(device) ?: run {
                    log("Failed to open USB device connection.")
                    return@launch
                }

                // FIX #2: log the driver that createUsbSerialDevice() selects so a null
                // result (unrecognised VID/PID) is immediately visible in logcat.
                val serial = UsbSerialDevice.createUsbSerialDevice(device, connection)
                if (serial == null) {
                    log("ERROR: UsbSerialDevice.createUsbSerialDevice() returned null — " +
                        "no driver matched VID=0x${Integer.toHexString(device.vendorId)} " +
                        "PID=0x${Integer.toHexString(device.productId)}. " +
                        "Check that the device is in the isArduino() filter.")
                    connection.close()
                    return@launch
                }
                usbSerial = serial

                if (serial.open()) {
                    serial.apply {
                        setBaudRate(BuddyBotConfig.SERIAL_BAUD_RATE)
                        setDataBits(UsbSerialInterface.DATA_BITS_8)
                        setStopBits(UsbSerialInterface.STOP_BITS_1)
                        setParity(UsbSerialInterface.PARITY_NONE)
                        setFlowControl(UsbSerialInterface.FLOW_CONTROL_OFF)
                        read { data ->
                            val str = String(data)
                            serialBuffer.append(str)
                            if (serialBuffer.contains("\n")) {
                                val messages = serialBuffer.toString().split("\n")
                                for (i in 0 until messages.size - 1) {
                                    val msg = messages[i].trim()
                                    log("[RECV] USB ← Mega: $msg (raw length: ${messages[i].length})")
                                    onMessageReceived?.invoke(msg)
                                }
                                val last = messages.last()
                                serialBuffer.setLength(0)
                                serialBuffer.append(last)
                            }
                        }
                    }
                    communicationMode.value = CommunicationMode.USB_SERIAL
                    log("Serial connected successfully.")
                    sendCommand("DIAG:RUN")
                } else {
                    log("Failed to open serial port.")
                    connection.close()
                }
            } catch (e: Exception) {
                Log.e("ArduinoComms", "USB Connect error", e)
            }
        }
    }

    fun initializeWebSocket(ip: String) {
        log("Connecting to WebSocket: ws://$ip:${BuddyBotConfig.WEBSOCKET_PORT}")
        
        // Cancel any existing retry job
        webSocketRetryJob?.cancel()
        webSocketRetryCount = 0
        
        try {
            val request = Request.Builder()
                .url("ws://$ip:${BuddyBotConfig.WEBSOCKET_PORT}")
                .build()
            
            webSocket = httpClient.newWebSocket(request, object : WebSocketListener() {
                override fun onOpen(webSocket: WebSocket, response: Response) {
                    log("✅ WebSocket CONNECTED successfully")
                    communicationMode.value = CommunicationMode.WEBSOCKET
                    webSocketRetryCount = 0  // Reset retry counter on success
                    checkAndLogCommunicationStatus()
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
                    log("⚠️ WebSocket CLOSED (code=$code, reason=$reason)")
                    communicationMode.value = CommunicationMode.DISCONNECTED
                    scheduleWebSocketReconnect(ip)
                }
            })
        } catch (e: Exception) {
            log("❌ WebSocket initialization error: ${e.message}")
            communicationMode.value = CommunicationMode.DISCONNECTED
            scheduleWebSocketReconnect(ip)
        }
    }
    
    private fun scheduleWebSocketReconnect(ip: String) {
        if (webSocketRetryCount >= MAX_WEBSOCKET_RETRIES) {
            log("❌ WebSocket: Max retries ($MAX_WEBSOCKET_RETRIES) reached. Giving up.")
            return
        }
        
        webSocketRetryCount++
        val delayMs = calculateExponentialBackoff(webSocketRetryCount)
        log("⏳ WebSocket: Retry #$webSocketRetryCount in ${delayMs}ms (max: $MAX_WEBSOCKET_RETRIES)")
        
        webSocketRetryJob?.cancel()
        webSocketRetryJob = scope.launch {
            delay(delayMs)
            if (communicationMode.value == CommunicationMode.DISCONNECTED) {
                initializeWebSocket(ip)
            }
        }
    }
    
    private fun calculateExponentialBackoff(retryCount: Int): Long {
        val exponentialDelay = INITIAL_RETRY_DELAY_MS * (1 shl (retryCount - 1))
        return exponentialDelay.coerceAtMost(MAX_RETRY_DELAY_MS)
    }
    
    private fun checkAndLogCommunicationStatus() {
        val mode = communicationMode.value
        if (mode == CommunicationMode.USB_SERIAL || mode == CommunicationMode.WEBSOCKET) {
            log("🚀 Serial + WebSocket LIVE (mode: $mode)")
        }
    }

    fun sendCommand(command: String) {
        // Strip any embedded \r or \n first, then append exactly one \n terminator.
        // The Mega's handleS9Communication() splits on '\n' and ignores '\r', so
        // every command MUST end with '\n' and must NOT contain an interior newline
        // that would split a single command into two fragments.
        val clean = command.trimEnd('\r', '\n')
        if (clean.isEmpty()) return
        val cmd = "$clean\n"
        when (communicationMode.value) {
            CommunicationMode.USB_SERIAL -> {
                log("[SEND] USB → Mega: $clean (${cmd.length} bytes)")
                if (usbSerial?.isOpen() != true) {
                    Log.w("ArduinoComms", "Serial not open, dropping command")
                    return
                }
                try {
                    usbSerial?.write(cmd.toByteArray(Charsets.UTF_8))
                } catch (e: IOException) {
                    Log.e("ArduinoComms", "Serial write failed: ${e.message}")
                } catch (e: NullPointerException) {
                    Log.e("ArduinoComms", "Serial null during write")
                }
            }
            // WebSocket: send without the trailing newline — the WS frame boundary
            // acts as the delimiter on the ESP32 side; the Mega never sees WS frames.
            CommunicationMode.WEBSOCKET -> {
                log("[SEND] WS → Mega: $clean")
                webSocket?.send(clean)
            }
            else -> log("[ERROR] sendCommand ignored (disconnected): $clean")
        }
    }

    fun unregister() {
        try {
            context.unregisterReceiver(usbReceiver)
            context.unregisterReceiver(webcamReceiver)
        } catch (e: Exception) {
            // Already unregistered or other issue
        }
    }

     fun close() {
         log("Closing communications.")
         communicationMode.value = CommunicationMode.DISCONNECTED
         usbSerial?.close()
         usbSerial = null
         webSocket?.close(1000, "Closing")
         webSocket = null
     }
    
    private fun log(message: String) {
        Log.d("ArduinoComms", message)
    }
}
