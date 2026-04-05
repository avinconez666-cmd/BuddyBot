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
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.launch
import okhttp3.*
import java.util.concurrent.TimeUnit

class ArduinoComms(private val context: Context, private val scope: CoroutineScope) {
    private var usbSerial: UsbSerialDevice? = null
    private var webSocket: WebSocket? = null
    private val httpClient = OkHttpClient.Builder()
        .connectTimeout(30, TimeUnit.SECONDS)
        .build()

    val communicationMode = MutableStateFlow(CommunicationMode.DISCONNECTED)
    var onMessageReceived: ((String) -> Unit)? = null
    
    private val serialBuffer = StringBuilder()
    private val ACTION_USB_PERMISSION = "com.buddybot.USB_PERMISSION_ARDUINO"

    private val usbReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context, intent: Intent) {
            when (intent.action) {
                UsbManager.ACTION_USB_DEVICE_DETACHED -> {
                    val device: UsbDevice? = IntentCompat.getParcelableExtra(intent, UsbManager.EXTRA_DEVICE, UsbDevice::class.java)
                    device?.let {
                        if (isArduino(it)) {
                            log("Arduino detached.")
                            close()
                            communicationMode.value = CommunicationMode.DISCONNECTED
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

    init {
        val filter = IntentFilter().apply {
            addAction(ACTION_USB_PERMISSION)
            addAction(UsbManager.ACTION_USB_DEVICE_DETACHED)
        }
        // Use RECEIVER_NOT_EXPORTED for local targeted broadcasts
        ContextCompat.registerReceiver(context, usbReceiver, filter, ContextCompat.RECEIVER_NOT_EXPORTED)
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
        return (vid == 0x2341 && (pid == 0x0042 || pid == 0x0043 || pid == 0x0001 || pid == 0x0010)) || // Arduino
               (vid == 0x2A03) || // Arduino.org
               (vid == 0x1A86 && pid == 0x7523) || // CH340
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

    private fun connectToArduino(device: UsbDevice) {
        try {
            log("Connecting to Arduino serial...")
            val usbManager = context.getSystemService(Context.USB_SERVICE) as UsbManager
            val connection = usbManager.openDevice(device) ?: run {
                log("Failed to open USB device connection.")
                return
            }
            usbSerial = UsbSerialDevice.createUsbSerialDevice(device, connection)
            if (usbSerial?.open() == true) {
                usbSerial!!.apply {
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
                                onMessageReceived?.invoke(messages[i].trim())
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

    private fun initializeWebSocket(ip: String) {
        log("Connecting to WebSocket: ws://$ip")
        val request = Request.Builder().url("ws://$ip:${BuddyBotConfig.WEBSOCKET_PORT}").build()
        webSocket = httpClient.newWebSocket(request, object : WebSocketListener() {
            override fun onOpen(webSocket: WebSocket, response: Response) {
                log("WebSocket connected.")
                communicationMode.value = CommunicationMode.WEBSOCKET
            }
            override fun onMessage(webSocket: WebSocket, text: String) {
                onMessageReceived?.invoke(text.trim())
            }
            override fun onFailure(webSocket: WebSocket, t: Throwable, response: Response?) {
                log("WebSocket failure: ${t.message}")
                communicationMode.value = CommunicationMode.DISCONNECTED
                scope.launch { delay(5000); initializeWebSocket(ip) }
            }
        })
    }

    fun sendCommand(command: String) {
        val cmd = if (command.endsWith("\n")) command else "$command\n"
        when (communicationMode.value) {
            CommunicationMode.USB_SERIAL -> usbSerial?.write(cmd.toByteArray())
            CommunicationMode.WEBSOCKET -> webSocket?.send(cmd.trim())
            else -> {}
        }
    }

    fun unregister() {
        try {
            context.unregisterReceiver(usbReceiver)
        } catch (e: Exception) {
            // Already unregistered or other issue
        }
    }

    fun close() {
        log("Closing communications.")
        usbSerial?.close()
        usbSerial = null
        webSocket?.close(1000, "Closing")
        webSocket = null
    }
    
    private fun log(message: String) {
        Log.d("ArduinoComms", message)
    }
}
