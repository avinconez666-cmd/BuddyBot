package com.buddybot.kids

import android.annotation.SuppressLint
import android.app.PendingIntent
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.content.pm.PackageManager
import android.Manifest
import android.hardware.Sensor
import android.hardware.SensorEvent
import android.hardware.SensorEventListener
import android.hardware.SensorManager
import android.graphics.RectF
import android.hardware.usb.UsbDevice
import android.hardware.usb.UsbManager
import android.media.MediaPlayer
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.os.PowerManager
import android.speech.RecognitionListener
import android.speech.RecognizerIntent
import android.speech.SpeechRecognizer
import android.speech.tts.TextToSpeech
import android.util.Log
import android.net.Uri
import android.net.ConnectivityManager
import android.net.Network
import android.net.NetworkCapabilities
import android.net.NetworkRequest
import android.provider.Settings
import android.view.Surface
import android.view.View
import android.view.WindowManager
import android.webkit.WebView
import android.widget.Toast
import androidx.activity.ComponentActivity
import androidx.activity.compose.BackHandler
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.core.content.ContextCompat
import androidx.core.content.IntentCompat
import androidx.core.content.edit
import androidx.core.net.toUri
import androidx.lifecycle.lifecycleScope
import com.buddybot.kids.ml.FaceRecognitionManager
import com.buddybot.kids.ml.ObjectDetectionManager
import com.google.mlkit.vision.common.InputImage
import com.jiangdg.ausbc.CameraClient
import com.jiangdg.ausbc.callback.ICaptureCallBack
import com.jiangdg.ausbc.callback.IPreviewDataCallBack
import com.jiangdg.ausbc.camera.CameraUvcStrategy
import com.jiangdg.ausbc.camera.bean.CameraRequest
import kotlinx.coroutines.*
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.collectLatest
import okhttp3.*
import okhttp3.MediaType.Companion.toMediaType
import okhttp3.RequestBody.Companion.toRequestBody
import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.io.FileOutputStream
import java.util.*
import java.util.concurrent.ExecutorService
import java.util.concurrent.Executors
import java.util.concurrent.atomic.AtomicBoolean
import kotlin.math.abs

class MainActivity : ComponentActivity(), TextToSpeech.OnInitListener, SensorEventListener {

    companion object {
        private const val TAG = "BuddyBotMainActivity"
        private const val WEBCAM_VENDOR_ID = 1133 // 0x046D in hex
        private const val WEBCAM_PRODUCT_ID = 2085 // 0x0825 in hex
        private const val ACTION_USB_PERMISSION = "com.buddybot.USB_PERMISSION_WEBCAM"
        private const val SENSOR_DELTA_THRESHOLD = 50
        private const val PROXIMITY_THRESHOLD = 30 // cm
    }

    private val audioFiles = mapOf(
        "STARTUP" to "startup.mp3",
        "HELLO" to "hello.mp3",
        "ALARM" to "alarm.mp3",
        "EMERGENCY" to "emergency.mp3",
        "HAZARD" to "hazard.mp3",
        "INTRUDER" to "intruder_alert",
        "BATTERY_CRITICAL" to "battery_low",
        "OVERTEMP" to "overheat_warning",
        "TILT" to "tilt_detected",
        "OBSTACLE" to "obstacle.mp3",
        "MOTION_DETECTED" to "motion.mp3",
        "ROAST_RANDOM" to "roast.mp3",
        "BARK" to "bark.mp3"
    )

    private lateinit var faceCoordinator: FaceCoordinator
    private lateinit var securityGatekeeper: SecurityGatekeeper
    
    private val _robotState = MutableStateFlow(RobotState())
    val robotState: StateFlow<RobotState> = _robotState

    private val _telemetry = MutableStateFlow(TelemetryData())
    val telemetry: StateFlow<TelemetryData> = _telemetry

    private val _commLogs = mutableStateListOf<String>()

    private var usbCameraClient: CameraClient? = null
    private var tts: TextToSpeech? = null
    private var speechRecognizer: SpeechRecognizer? = null

    // Change 5: store the alive-behavior Job so it can be cancelled in onDestroy()
    private var aliveBehaviorJob: Job? = null

    private var httpClient = OkHttpClient.Builder()
        .connectTimeout(30, java.util.concurrent.TimeUnit.SECONDS)
        .build()

    // ── Network preference ────────────────────────────────────────────
    // Keeps track of the currently bound network and the active ConnectivityManager
    // callback so we can swap the OkHttpClient socket factory when the user changes
    // the network preference in Settings.
    @Volatile private var currentBoundNetwork: Network? = null
    private var networkCallback: ConnectivityManager.NetworkCallback? = null

    /**
     * Rebuilds [httpClient] so all subsequent AI/TTS calls go through the
     * network interface matching [pref].
     *
     *  ANY         — clear any socket factory binding; OS decides (WiFi preferred)
     *  WIFI_ONLY   — request a WiFi-only network; if none available the calls will
     *                simply fail and the AI cascade will fall back to OFFLINE
     *  MOBILE_ONLY — request a CELLULAR network explicitly, bypassing WiFi
     *
     * Must be called on any thread; state update posted to main.
     */
    private fun applyNetworkPreference(pref: NetworkPreference) {
        val cm = getSystemService(ConnectivityManager::class.java)

        // Tear down the previous callback if any
        networkCallback?.let {
            try { cm.unregisterNetworkCallback(it) } catch (_: Exception) {}
        }
        networkCallback = null
        currentBoundNetwork = null

        when (pref) {
            NetworkPreference.ANY -> {
                // Rebuild with no explicit socket factory — system default
                rebuildHttpClient(null)
                Log.d(TAG, "Network: ANY (system default)")
            }

            NetworkPreference.WIFI_ONLY,
            NetworkPreference.MOBILE_ONLY -> {
                val transport = if (pref == NetworkPreference.WIFI_ONLY)
                    NetworkCapabilities.TRANSPORT_WIFI
                else
                    NetworkCapabilities.TRANSPORT_CELLULAR

                val req = NetworkRequest.Builder()
                    .addTransportType(transport)
                    .addCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET)
                    .build()

                val cb = object : ConnectivityManager.NetworkCallback() {
                    override fun onAvailable(network: Network) {
                        currentBoundNetwork = network
                        rebuildHttpClient(network)
                        Log.d(TAG, "Network: bound to $pref (network=$network)")
                    }
                    override fun onLost(network: Network) {
                        if (currentBoundNetwork == network) {
                            currentBoundNetwork = null
                            rebuildHttpClient(null) // fall back to system
                            Log.w(TAG, "Network: $pref network lost — reverting to system")
                        }
                    }
                }
                networkCallback = cb
                try {
                    cm.requestNetwork(req, cb)
                } catch (e: Exception) {
                    Log.e(TAG, "Network request failed: ${e.message}")
                }
            }
        }

        // Persist choice and update UI state
        getSharedPreferences("buddybot", MODE_PRIVATE).edit {
            putString("network_pref", pref.name)
        }
        _robotState.value = _robotState.value.copy(networkPreference = pref)
    }

    /** Swaps [httpClient] to use [network]'s socket factory, or the system default if null. */
    private fun rebuildHttpClient(network: Network?) {
        val builder = OkHttpClient.Builder()
            .connectTimeout(30, java.util.concurrent.TimeUnit.SECONDS)
            .readTimeout(30, java.util.concurrent.TimeUnit.SECONDS)
        if (network != null) {
            builder.socketFactory(network.socketFactory)
        }
        // httpClient is used by synthesizeWithElevenLabs and all AI calls —
        // replacing the reference is safe because OkHttp dispatches on its own
        // thread pool; in-flight calls complete on the old client.
        @Suppress("ASSIGNED_BUT_NEVER_ACCESSED_VARIABLE")
        httpClient = builder.build()
    }

    private lateinit var arduinoComms: ArduinoComms
    private lateinit var faceRecognitionManager: FaceRecognitionManager
    private lateinit var objectDetectionManager: ObjectDetectionManager
    private lateinit var cameraExecutor: ExecutorService
    private lateinit var sensorManager: SensorManager
    private var orientationSensor: Sensor? = null
    private var wakeLock: PowerManager.WakeLock? = null

    private var isListeningForWakeWord = false
    private var isProcessingCommand = false
    private var lastSpeechTime = 0L
    private val silenceHandler = Handler(Looper.getMainLooper())
    private var currentSpeechText = ""
    private val isMlProcessing = AtomicBoolean(false)
    // Throttle FACE: and OBJ: commands to Mega — max once per 200 ms each
    @Volatile private var lastFaceSentMs = 0L
    @Volatile private var lastObjSentMs  = 0L
    private val VISION_THROTTLE_MS = 200L
    
    private var lastFrontDistance = -1
    private var lastRearDistance = -1
    private var lastLeftDistance = -1
    private var lastRightDistance = -1
    private var isPatrolling = false
    private var isDogFollowing = true
    private val elevenLabsMutex = Mutex()

    // Phase 4: Call Daddy overlay manager
    private var callOverlayManager: CallOverlayManager? = null

    // Phase 5: Sensor throttle — only send SENS| to Mega at most once per 500ms
    // (SENSOR_DELAY_UI fires ~60ms; without throttle this floods the serial buffer)
    private var lastSensorSentMs = 0L
    private val SENSOR_SEND_INTERVAL_MS = 500L

    // Phase 3E: ESP32 HTTP status connection
    private var esp32ConnectJob: Job? = null

    private val permissionLauncher = registerForActivityResult(
        ActivityResultContracts.RequestMultiplePermissions()
    ) { permissions ->
        if (permissions.all { it.value }) initializeApp() else finish()
    }

    private val hotwordReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context, intent: Intent) {
            if (intent.action == HotwordService.ACTION_HOTWORD_DETECTED) {
                Log.d(TAG, "🎤 Hotword broadcast received!")
                runOnUiThread { onHotwordDetected() }
            }
        }
    }

    private val usbReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context, intent: Intent) {
            when (intent.action) {
                ACTION_USB_PERMISSION -> {
                    // Phase 4: USB permission result for webcam
                    synchronized(this) {
                        val device: UsbDevice? = IntentCompat.getParcelableExtra(
                            intent,
                            UsbManager.EXTRA_DEVICE,
                            UsbDevice::class.java
                        )
                        if (intent.getBooleanExtra(UsbManager.EXTRA_PERMISSION_GRANTED, false)) {
                            Log.d(TAG, "USB permission granted for: ${device?.deviceName}")
                            device?.let { openWebcam(it) }
                        } else {
                            Log.w(TAG, "USB permission denied for: ${device?.deviceName}")
                            logComm("CAMERA", "USB permission denied — cannot open webcam")
                            _robotState.value = _robotState.value.copy(isCameraConnected = false)
                        }
                    }
                }
                UsbManager.ACTION_USB_DEVICE_ATTACHED -> {
                    // Phase 4: Hot-plug — new USB device attached, check if it is a webcam
                    Log.d(TAG, "USB device attached — scanning for webcam")
                    logComm("CAMERA", "USB device attached — scanning...")
                    initializeUSBWebcam()
                }
                UsbManager.ACTION_USB_DEVICE_DETACHED -> {
                    // Phase 4: Webcam unplugged — update state
                    val device: UsbDevice? = IntentCompat.getParcelableExtra(
                        intent, UsbManager.EXTRA_DEVICE, UsbDevice::class.java
                    )
                    Log.d(TAG, "USB device detached: ${device?.deviceName}")
                    if (usbCameraClient != null) {
                        logComm("CAMERA", "Webcam disconnected")
                        usbCameraClient?.closeCamera()
                        usbCameraClient = null
                        _robotState.value = _robotState.value.copy(isCameraConnected = false)
                    }
                }
            }
        }
    }

    @SuppressLint("SetTextI18n", "WrongConstant")
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
            window.attributes.layoutInDisplayCutoutMode =
                WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES
        }
        window.setFlags(
            WindowManager.LayoutParams.FLAG_LAYOUT_NO_LIMITS,
            WindowManager.LayoutParams.FLAG_LAYOUT_NO_LIMITS
        )

        val powerManager = getSystemService(POWER_SERVICE) as PowerManager
        @Suppress("DEPRECATION")
        wakeLock = powerManager.newWakeLock(
            PowerManager.FULL_WAKE_LOCK or PowerManager.ACQUIRE_CAUSES_WAKEUP,
            "BuddyBot::WakeLock"
        )
        wakeLock?.acquire(10 * 60 * 1000L)

        @Suppress("DEPRECATION")
        window.decorView.systemUiVisibility = (
                View.SYSTEM_UI_FLAG_FULLSCREEN or
                        View.SYSTEM_UI_FLAG_HIDE_NAVIGATION or
                        View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY or
                        View.SYSTEM_UI_FLAG_LAYOUT_STABLE or
                        View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION or
                        View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                )

        setContentView(R.layout.activity_main)
        
        val playerView = findViewById<androidx.media3.ui.PlayerView>(R.id.playerView)
        val faceWebView = findViewById<WebView>(R.id.faceWebView)
        faceWebView.settings.javaScriptEnabled = true
        faceWebView.settings.mediaPlaybackRequiresUserGesture = false
        faceCoordinator = FaceCoordinator(this, playerView, lifecycleScope, faceWebView)

        val filter = IntentFilter(ACTION_USB_PERMISSION).also {
            // Phase 4: listen for hot-plug attach/detach so webcam auto-connects
            it.addAction(UsbManager.ACTION_USB_DEVICE_ATTACHED)
            it.addAction(UsbManager.ACTION_USB_DEVICE_DETACHED)
        }
        ContextCompat.registerReceiver(this, usbReceiver, filter, ContextCompat.RECEIVER_EXPORTED)

        ContextCompat.registerReceiver(
            this,
            hotwordReceiver,
            IntentFilter(HotwordService.ACTION_HOTWORD_DETECTED),
            ContextCompat.RECEIVER_NOT_EXPORTED
        )

        setupComposeUI()
        checkAndRequestPermissions()

        // Restore saved network preference (so choice survives app restarts)
        val savedPref = getSharedPreferences("buddybot", MODE_PRIVATE)
            .getString("network_pref", NetworkPreference.ANY.name)
        val pref = try {
            NetworkPreference.valueOf(savedPref ?: NetworkPreference.ANY.name)
        } catch (_: Exception) { NetworkPreference.ANY }
        if (pref != NetworkPreference.ANY) applyNetworkPreference(pref)
        else _robotState.value = _robotState.value.copy(networkPreference = pref)
    }

    private fun checkAndRequestPermissions() {
        val needed = BuddyBotConfig.REQUIRED_PERMISSIONS.filter { permission ->
            ContextCompat.checkSelfPermission(this, permission) != PackageManager.PERMISSION_GRANTED
        }
        if (needed.isEmpty()) initializeApp() else permissionLauncher.launch(needed.toTypedArray())
    }

    private fun initializeApp() {
        try {
            logComm("SYS", "Initializing BuddyBot Brain")
            Log.d(TAG, "initializeApp: Starting initialization")
            
            try {
                tts = TextToSpeech(this, this)
                Log.d(TAG, "initializeApp: TextToSpeech initialized")
            } catch (e: Exception) {
                Log.e(TAG, "Error initializing TextToSpeech", e)
            }
            
            try {
                cameraExecutor = Executors.newFixedThreadPool(2)
                Log.d(TAG, "initializeApp: Camera executor initialized")
            } catch (e: Exception) {
                Log.e(TAG, "Error initializing camera executor", e)
            }
            
            try {
                faceRecognitionManager = FaceRecognitionManager(this)
                Log.d(TAG, "initializeApp: FaceRecognitionManager initialized")
            } catch (e: Exception) {
                Log.e(TAG, "Error initializing FaceRecognitionManager", e)
            }
            
            try {
                objectDetectionManager = ObjectDetectionManager(this)
                Log.d(TAG, "initializeApp: ObjectDetectionManager initialized")
            } catch (e: Exception) {
                Log.e(TAG, "Error initializing ObjectDetectionManager", e)
            }
            
            try {
                arduinoComms = ArduinoComms(this, lifecycleScope)
                Log.d(TAG, "initializeApp: ArduinoComms initialized")
            } catch (e: Exception) {
                Log.e(TAG, "Error initializing ArduinoComms", e)
            }
            
            try {
                securityGatekeeper = SecurityGatekeeper(arduinoComms::sendCommand) { mode ->
                    setRobotMode(mode)
                }
                Log.d(TAG, "initializeApp: SecurityGatekeeper initialized")
            } catch (e: Exception) {
                Log.e(TAG, "Error initializing SecurityGatekeeper", e)
            }

            try {
                sensorManager = getSystemService(Context.SENSOR_SERVICE) as SensorManager
                orientationSensor = sensorManager.getDefaultSensor(Sensor.TYPE_ROTATION_VECTOR)
                Log.d(TAG, "initializeApp: Sensor manager initialized")
            } catch (e: Exception) {
                Log.e(TAG, "Error initializing sensor manager", e)
            }

            try {
                initializeSpeechRecognizer()
                Log.d(TAG, "initializeApp: Speech recognizer initialized")
            } catch (e: Exception) {
                Log.e(TAG, "Error initializing speech recognizer", e)
            }
            
            try {
                val prefs = getSharedPreferences("BuddyBot", MODE_PRIVATE)
                val savedIP = prefs.getString("buddybotIP", "") ?: ""
                if (savedIP.isNotEmpty()) {
                    _robotState.value = _robotState.value.copy(buddybotIP = savedIP)
                    Log.d(TAG, "Loaded saved IP: $savedIP")
                    logComm("COMM", "✅ Loaded saved IP: $savedIP")
                }
                arduinoComms.onMessageReceived = { handleArduinoMessage(it) }
                arduinoComms.initialize(_robotState.value.buddybotIP)
                Log.d(TAG, "initializeApp: Arduino communications initialized")
                
                // Monitor communication status
                lifecycleScope.launch {
                    arduinoComms.communicationMode.collect { mode ->
                        Log.d(TAG, "Communication mode changed: $mode")
                        _robotState.value = _robotState.value.copy(communicationMode = mode)
                        when (mode) {
                            CommunicationMode.USB_SERIAL -> {
                                logComm("COMM", "✅ USB Serial CONNECTED")
                            }
                            CommunicationMode.WEBSOCKET -> {
                                logComm("COMM", "✅ WebSocket CONNECTED")
                            }
                            CommunicationMode.DISCONNECTED -> {
                                logComm("COMM", "⚠️ Communication DISCONNECTED")
                            }
                        }
                    }
                }
            } catch (e: Exception) {
                Log.e(TAG, "Error initializing Arduino communications", e)
            }
            
            try {
                startEnvironmentMonitoring()
                Log.d(TAG, "initializeApp: Environment monitoring started")
            } catch (e: Exception) {
                Log.e(TAG, "Error starting environment monitoring", e)
            }

            // Phase 3: Start always-listening hotword service
            try {
                startHotwordService()
                Log.d(TAG, "initializeApp: HotwordService started")
            } catch (e: Exception) {
                Log.e(TAG, "Error starting HotwordService", e)
            }

            // Show the intro dialog
            _robotState.value = _robotState.value.copy(showIntroDialog = true)
            logComm("SYS", "Initialization Complete - Show Intro Dialog")
            Log.d(TAG, "initializeApp: Complete")
        } catch (e: Exception) {
            Log.e(TAG, "FATAL ERROR in initializeApp", e)
            logComm("ERROR", "Initialization failed: ${e.message}")
        }
    }

    private fun handleArduinoMessage(msg: String) {
        logComm("ARD", msg)
        try {
            if (msg.startsWith("TELE:")) {
                // FIX #4a: Mega sends TELE:volt,pct,moving (3 fields, indices 0-2).
                // Old code required size >= 4 and read data[3] — both wrong.
                val data = msg.substring(5).split(",")
                if (data.size >= 3) {
                    val batteryPercent = data[1].toIntOrNull() ?: 0
                    _telemetry.value = _telemetry.value.copy(
                        batteryVoltage = data[0].toFloatOrNull() ?: 0f,
                        batteryPercent = batteryPercent,
                        isMoving = data[2] == "1"   // index 2, not 3
                    )

                    if (batteryPercent < 15 && batteryPercent > 0) {
                        speakText("My battery is getting very low. Please plug me in soon!")
                    }
                }
            } else if (msg.startsWith("EVENT:")) {
                val event = msg.substring(6)
                handleEvent(event)
            } else if (msg.startsWith("ACK|")) {
                // FIX #4c: handle ACK|COMMAND|END acknowledgement frames from the Mega.
                // Format: ACK|<cmd>|END  e.g. ACK|MOTOR:F|END
                val inner = msg.removePrefix("ACK|").removeSuffix("|END")
                Log.d(TAG, "[ACK] Mega acknowledged command: $inner")
                logComm("ACK", inner)
                // Update isAutoMode state based on ACK from Mega so the UI reflects
                // the confirmed hardware state rather than the optimistic sent state.
                when (inner) {
                    "AUTO:ON"  -> _robotState.value = _robotState.value.copy(isAutoMode = true)
                    "AUTO:OFF" -> _robotState.value = _robotState.value.copy(isAutoMode = false)
                }
            } else if (msg.startsWith("US:")) {
                parseUltrasonicData(msg)
            } else if (msg.startsWith("GESTURE:")) {
                handleGesture(msg)
            } else if (msg.startsWith("MODE:")) {
                parseMode(msg.substring(5))?.let {
                    _robotState.value = _robotState.value.copy(currentMode = it)
                }
            } else if (msg.startsWith("REQ_MODE:")) {
                parseMode(msg.substring(9))?.let {
                    _robotState.value = _robotState.value.copy(showPinEntry = true, requestedMode = it)
                }
            }
        } catch (e: Exception) {
            Log.e(TAG, "Failed to parse Arduino msg: $msg", e)
        }
    }
    
    /**
     * Handles EVENT:<code> messages from the Mega.
     * Shows a colour-coded banner in the UI and plays audio/TTS where appropriate.
     * Banner auto-clears after [durationMs] milliseconds.
     */
    private fun handleEvent(event: String, durationMs: Long = 4000) {
        Log.d(TAG, "[EVENT] $event")
        logComm("EVENT", event)

        val (message, level) = when (event) {
            "BATTERY_WARN"     -> Pair("⚠️ Battery low",                    BannerLevel.WARNING)
            "BATTERY_CRITICAL" -> Pair("🔴 Battery critical — stopping",    BannerLevel.CRITICAL)
            "OBSTACLE"         -> Pair("🚧 Obstacle detected",              BannerLevel.INFO)
            "TILT"             -> Pair("⚠️ Robot tilted",                   BannerLevel.CRITICAL)
            "HAZARD"           -> Pair("🔴 Hazard detected",                BannerLevel.CRITICAL)
            "GAS_ALERT"        -> Pair("🔴 Gas detected",                   BannerLevel.CRITICAL)
            else               -> Pair("ℹ️ Event: $event",                  BannerLevel.INFO)
        }

        // Show banner in UI
        _robotState.value = _robotState.value.copy(eventBanner = Pair(message, level))
        Handler(Looper.getMainLooper()).postDelayed({
            // Only clear if this banner is still the active one
            if (_robotState.value.eventBanner?.first == message) {
                _robotState.value = _robotState.value.copy(eventBanner = null)
            }
        }, durationMs)

        // Play audio / TTS per event type.
        // speakText() is only called when tts is already initialised (non-null).
        when (event) {
            "OBSTACLE" -> {
                playAudioCommand("OBSTACLE")
                if (tts != null) speakText("Obstacle ahead")
            }
            "BATTERY_WARN" -> {
                playAudioCommand("BATTERY_CRITICAL")   // reuse battery_low audio asset
                if (tts != null) speakText("Battery low")
            }
            "BATTERY_CRITICAL" -> {
                playAudioCommand("BATTERY_CRITICAL")
                arduinoComms.sendCommand("MOTOR:S")
                if (tts != null) speakText("Stopping, battery critical")
            }
            "TILT"     -> playAudioCommand("TILT")
            "HAZARD"   -> playAudioCommand("HAZARD")
            "GAS_ALERT"-> playAudioCommand("HAZARD")
        }
    }

    private fun handleGesture(msg: String) {
        // Format: GESTURE:UP|GESTURE:DOWN|GESTURE:LEFT|GESTURE:RIGHT|GESTURE:CLOCKWISE|GESTURE:COUNTERCLOCKWISE
        val gestureCode = msg.substring(8)
        Log.d(TAG, "[GESTURE] Detected: $gestureCode")

        // Map gesture code to a human-readable label for the UI indicator
        val gestureLabel = when (gestureCode) {
            "UP"               -> "👆 Gesture: Up"
            "DOWN"             -> "👇 Gesture: Down"
            "LEFT"             -> "👈 Gesture: Left"
            "RIGHT"            -> "👉 Gesture: Right"
            "CLOCKWISE"        -> "🔄 Gesture: Spin CW"
            "COUNTERCLOCKWISE" -> "🔄 Gesture: Spin CCW"
            else               -> "✋ Gesture: $gestureCode"
        }

        // Show the gesture label in the UI and auto-clear after 2 seconds
        _robotState.value = _robotState.value.copy(lastGesture = gestureLabel)
        Handler(Looper.getMainLooper()).postDelayed({
            if (_robotState.value.lastGesture == gestureLabel) {
                _robotState.value = _robotState.value.copy(lastGesture = "")
            }
        }, 2000)

        // Play face animation (best-effort — silently ignored if asset missing)
        val animName = when (gestureCode) {
            "UP"               -> "gesture_up"
            "DOWN"             -> "gesture_down"
            "LEFT"             -> "gesture_left"
            "RIGHT"            -> "gesture_right"
            "CLOCKWISE"        -> "gesture_spin_cw"
            "COUNTERCLOCKWISE" -> "gesture_spin_ccw"
            else               -> null
        }
        animName?.let {
            try { faceCoordinator.playVideoOnce(it) {} }
            catch (e: Exception) { Log.w(TAG, "Gesture animation not available: $it") }
        }

        // Map gesture to motor command — only when gestureReactionsEnabled is true
        if (_robotState.value.gestureReactionsEnabled) {
            when (gestureCode) {
                "UP"               -> { speakText("Up up up!");           arduinoComms.sendCommand("MOTOR:F") }
                "DOWN"             -> { speakText("Down down!");          arduinoComms.sendCommand("MOTOR:B") }
                "LEFT"             -> { speakText("Left turn!");          arduinoComms.sendCommand("MOTOR:L") }
                "RIGHT"            -> { speakText("Right turn!");         arduinoComms.sendCommand("MOTOR:R") }
                "CLOCKWISE"        -> { speakText("Spinning around!");    arduinoComms.sendCommand("MOTOR:DANCE") }
                "COUNTERCLOCKWISE" -> { speakText("Spinning the other way!"); arduinoComms.sendCommand("MOTOR:DANCE") }
                else               -> Log.w(TAG, "[GESTURE] Unknown gesture: $gestureCode")
            }
        } else {
            Log.d(TAG, "[GESTURE] Gesture reactions disabled — no motor command sent")
        }
    }
    

    // ── Fix 1: Parse Mega V31 STAT: packet ─────────────────────────────────
    // Format: STAT:gas:temp:hum:haz:pir:tilt:ir:volt:pct:amps
    private fun parseStatPacket(msg: String) {
        try {
            val fields = msg.substring(5).split(":")
            if (fields.size < 10) return
            val gas     = fields[0].toIntOrNull()   ?: 0
            val temp    = fields[1].toFloatOrNull() ?: 0f
            val hum     = fields[2].toFloatOrNull() ?: 0f
            val haz     = fields[3].toIntOrNull()   ?: 0
            val pir     = fields[4].toIntOrNull()   ?: 0
            val tilt    = fields[5].toIntOrNull()   ?: 0
            val ir      = fields[6].toIntOrNull()   ?: 0
            val volt    = fields[7].toFloatOrNull() ?: 0f
            val pct     = fields[8].toIntOrNull()   ?: 0
            val amps    = fields[9].toFloatOrNull() ?: 0f

            _telemetry.value = _telemetry.value.copy(
                gasLevel        = gas,
                temperature     = temp,
                humidity        = hum,
                hazardDetected  = haz == 1,
                pirAlert        = pir == 1,
                tiltAlert       = tilt == 1,
                irAlert         = ir == 1,
                batteryVoltage  = volt,
                batteryPercent  = pct,
                currentAmps     = amps,
            )

            // Low battery warning
            if (pct in 1..14) {
                speakText("My battery is getting very low. Please plug me in soon!")
            }

            // Tilt alert
            if (tilt == 1) {
                handleEvent("TILT")
            }

            // Hazard alert
            if (haz == 1) {
                handleEvent("HAZARD")
            }

            Log.d(TAG, "STAT parsed: gas=$gas temp=$temp hum=$hum volt=$volt pct=$pct")
        } catch (e: Exception) {
            Log.e(TAG, "parseStatPacket error: ${e.message}")
        }
    }

    // ── Fix 5: Handle SAFETY: messages from Mega ────────────────────────────
    private fun handleSafetyMessage(code: String) {
        Log.d(TAG, "[SAFETY] $code")
        logComm("SAFETY", code)
        when {
            code.startsWith("FLAME")    -> { handleEvent("HAZARD"); speakText("Warning! Flame detected!") }
            code.startsWith("TILT")     -> { handleEvent("TILT") }
            code.startsWith("GAS")      -> { handleEvent("GAS_ALERT"); speakText("Gas detected! Tell a grown up!") }
            code.startsWith("OVERTEMP") -> { handleEvent("HAZARD"); speakText("I'm getting too hot!") }
            else                        -> handleEvent(code)
        }
    }

    // ── Fix 8: Parse STATUS| pipe-delimited status packet ───────────────────
    // Format: STATUS|ESTOP:YES|AUTO:ON|R3:OK|ESP:OK|S9:OK|FW:V31.0
    private fun parseStatusPipe(msg: String) {
        try {
            val isEstop  = msg.contains("ESTOP:YES")
            val isAuto   = msg.contains("AUTO:ON")
            val r3ok     = msg.contains("R3:OK")
            val espOk    = msg.contains("ESP:OK")
            val fwIdx    = msg.indexOf("FW:")
            val fw       = if (fwIdx >= 0) msg.substring(fwIdx + 3).substringBefore("|") else "--"

            _robotState.value = _robotState.value.copy(isAutoMode = isAuto)

            if (isEstop) {
                arduinoComms.sendCommand("MOTOR:S")
                handleEvent("OBSTACLE", 8000)
            }

            logComm("STATUS", "Auto=$isAuto R3=$r3ok ESP=$espOk FW=$fw ESTOP=$isEstop")
        } catch (e: Exception) {
            Log.e(TAG, "parseStatusPipe error: ${e.message}")
        }
    }
    private fun parseUltrasonicData(msg: String) {
        val parts = msg.substring(3).split(',')
        if (parts.size == 4) {
            val f = parts[0].toIntOrNull() ?: -1
            val r = parts[1].toIntOrNull() ?: -1
            val l = parts[2].toIntOrNull() ?: -1
            val ri = parts[3].toIntOrNull() ?: -1
            
            if (_robotState.value.currentMode == RobotMode.DOG && isPatrolling) {
                val fDelta = if(lastFrontDistance != -1) abs(f - lastFrontDistance) else 0
                if (fDelta > SENSOR_DELTA_THRESHOLD) {
                    activateEmergencyMode()
                    usbCameraClient?.captureVideoStart(object : ICaptureCallBack {
                        override fun onBegin() {}
                        override fun onComplete(path: String?) { logComm("REC", "Emergency video saved: $path") }
                        override fun onError(error: String?) { logComm("REC", "Emergency video failed: $error") }
                    })
                }
            }
            
            if (_robotState.value.currentMode == RobotMode.BODYGUARD && f < PROXIMITY_THRESHOLD && f != -1) {
                val person = _robotState.value.recognizedPerson
                if (person != "AJ" && person != "Parent") {
                    faceCoordinator.triggerWarning()
                    arduinoComms.sendCommand("KEEP_DISTANCE")
                }
            }

            // NOTE: HEAD servo commands removed — no servo hardware on Mega
            if (!_robotState.value.isSpeaking && !isMlProcessing.get()) {
                // Proximity detection logged; head movement not available
            }

            lastFrontDistance = f
            lastRearDistance = r
            lastLeftDistance = l
            lastRightDistance = ri
            
            _telemetry.value = _telemetry.value.copy(
                frontDistance = f,
                rearDistance = r,
                leftDistance = l,
                rightDistance = ri
            )
        }
    }
    
    private fun onPinValidated(pin: String) {
        val requestedMode = _robotState.value.requestedMode
        if (requestedMode != null) {
            if (securityGatekeeper.validatePin(pin, requestedMode)) {
                 _robotState.value = _robotState.value.copy(showPinEntry = false, requestedMode = null)
            } else {
                 _robotState.value = _robotState.value.copy(showPinEntry = false, requestedMode = null)
            }
        }
    }

    private fun onHotwordDetected() {
        if (isProcessingCommand || _robotState.value.isSpeaking) return
        Log.d(TAG, "Hotword triggered — capturing command")
        isProcessingCommand = true
        isListeningForWakeWord = false
        speakText("Yeah?")
        Handler(Looper.getMainLooper()).postDelayed({ startCommandListening() }, 1200)
    }

    private fun startSequence(playIntro: Boolean) {
        _robotState.value = _robotState.value.copy(showIntroDialog = false)
        if (playIntro) {
            // Use playIntroVideo() which sets volume = 1f (WITH audio)
            faceCoordinator.playIntroVideo { playSplashThenMain() }
        } else {
            playSplashThenMain()
        }
    }

    private fun playSplashThenMain() {
        faceCoordinator.playVideoOnce("splash") {
            _robotState.value = _robotState.value.copy(isSplashScreen = false)
            startMainProgram()
        }
    }

    private fun startMainProgram() {
        try {
            logComm("SYS", "Main Program Started")
            Log.d(TAG, "startMainProgram: Initializing components")
            
            try {
                startContinuousListening()
                Log.d(TAG, "startMainProgram: Listening started")
            } catch (e: Exception) {
                Log.e(TAG, "Error starting listening", e)
            }
            
            try {
                initializeUSBWebcam()
                Log.d(TAG, "startMainProgram: USB webcam initialized")
            } catch (e: Exception) {
                Log.e(TAG, "Error initializing USB webcam", e)
            }
            
            try {
                Log.d(TAG, "startMainProgram: Setting robot mode to ${_robotState.value.currentMode}")
                faceCoordinator.setRobotMode(_robotState.value.currentMode, RobotMode.NORMAL, force = true)
                Log.d(TAG, "startMainProgram: Robot mode set successfully")
            } catch (e: Exception) {
                Log.e(TAG, "CRITICAL ERROR setting robot mode", e)
                logComm("ERROR", "Failed to set robot mode: ${e.message}")
            }
            
            try {
                startAliveBehavior()
                Log.d(TAG, "startMainProgram: Alive behavior started")
            } catch (e: Exception) {
                Log.e(TAG, "Error starting alive behavior", e)
            }
            
            lifecycleScope.launch {
                _robotState.collectLatest { state ->
                    try {
                        faceCoordinator.setSpeaking(state.isSpeaking)
                    } catch (e: Exception) {
                        Log.e(TAG, "Error updating speaking state", e)
                    }
                }
            }
            
            logComm("SYS", "Main Program Initialization Complete")
        } catch (e: Exception) {
            Log.e(TAG, "FATAL ERROR in startMainProgram", e)
            logComm("ERROR", "Main program failed: ${e.message}")
        }
    }

    private fun startAliveBehavior() {
        // Change 5: store the Job reference so it can be cancelled in onDestroy()
        aliveBehaviorJob = lifecycleScope.launch {
            while (isActive) {
                if (_robotState.value.currentMode == RobotMode.NORMAL && !isProcessingCommand && !_robotState.value.isSpeaking) {
                    delay((10000..30000).random().toLong())
                    // HEAD:RANDOM command removed — no servo hardware
                }
                
                if (_robotState.value.currentMode == RobotMode.UNHINGED && !isProcessingCommand && !_robotState.value.isSpeaking) {
                    delay((5000..15000).random().toLong())
                    // 100% speak in UNHINGED mode (was 70% speak, 30% HEAD:JITTER)
                    speakText(getAIResponse("Tell me a random short unhinged joke or comment."))
                }
                delay(5000)
            }
        }
    }

    private fun setupComposeUI() {
        findViewById<androidx.compose.ui.platform.ComposeView>(R.id.composeView).setContent {
            val state by robotState.collectAsState()
            val telemetryData by telemetry.collectAsState()
            var showSettings by remember { mutableStateOf(false) }
            var showPasscodeDialog by remember { mutableStateOf(false) }

            MaterialTheme {
                BackHandler(enabled = !state.isSplashScreen) {
                    showPasscodeDialog = true
                }

                Box(modifier = Modifier.fillMaxSize()) {
                    // Transparent background so the ExoPlayer surface underneath is visible

                    if (state.isListening) {
                        Box(
                            modifier = Modifier
                                .fillMaxSize()
                                .background(Color.Red.copy(alpha = 0.1f))
                                .border(8.dp, Color.Red.copy(alpha = 0.5f))
                        )
                    }

                    if (state.isProcessing) ProcessingOverlay()
                    if (state.isEmergency) EmergencyOverlay()

                    // ── Event banner (top of screen, colour-coded by severity) ──────────
                    state.eventBanner?.let { (message, level) ->
                        val bannerColor = when (level) {
                            BannerLevel.CRITICAL -> Color(0xFFB71C1C)   // deep red
                            BannerLevel.WARNING  -> Color(0xFFF57F17)   // amber
                            BannerLevel.INFO     -> Color(0xFF1565C0)   // blue
                        }
                        Box(
                            modifier = Modifier
                                .fillMaxWidth()
                                .background(bannerColor.copy(alpha = 0.92f))
                                .padding(horizontal = 16.dp, vertical = 10.dp)
                                .align(Alignment.TopCenter),
                            contentAlignment = Alignment.Center
                        ) {
                            Text(
                                text = message,
                                color = Color.White,
                                fontSize = 18.sp,
                                fontWeight = FontWeight.Bold,
                                textAlign = TextAlign.Center
                            )
                        }
                    }

                    // ── Gesture indicator (bottom-centre, brief 2-second flash) ─────────
                    if (state.lastGesture.isNotEmpty()) {
                        Box(
                            modifier = Modifier
                                .fillMaxWidth()
                                .background(Color.Black.copy(alpha = 0.65f))
                                .padding(horizontal = 16.dp, vertical = 8.dp)
                                .align(Alignment.BottomCenter),
                            contentAlignment = Alignment.Center
                        ) {
                            Text(
                                text = state.lastGesture,
                                color = Color.White,
                                fontSize = 16.sp,
                                fontWeight = FontWeight.Medium,
                                textAlign = TextAlign.Center
                            )
                        }
                    }

                    if (state.showIntroDialog) {
                        AlertDialog(
                            onDismissRequest = { },
                            title = { Text("First Meeting") },
                            text = { Text("Would you like to play the introduction video for AJ?") },
                            confirmButton = {
                                Button(onClick = { startSequence(true) }) { Text("Yes") }
                            },
                            dismissButton = {
                                TextButton(onClick = { startSequence(false) }) { Text("No") }
                            }
                        )
                    }
                    
                    if (state.showPinEntry) {
                        PinEntryDialog(
                            onConfirm = ::onPinValidated,
                            onDismiss = { _robotState.value = _robotState.value.copy(showPinEntry = false, requestedMode = null) },
                            requestedMode = state.requestedMode
                        )
                    }

                    // ── AUTO mode toggle pill (top-right, visible during normal operation) ─
                    if (!state.isSplashScreen && !state.showIntroDialog && !showSettings) {
                        val autoColor = if (state.isAutoMode) Color(0xFF2E7D32) else Color(0xFF424242)
                        Button(
                            onClick = {
                                val cmd = if (state.isAutoMode) "AUTO:OFF" else "AUTO:ON"
                                // Optimistic update — confirmed by ACK from Mega
                                _robotState.value = _robotState.value.copy(isAutoMode = !state.isAutoMode)
                                arduinoComms.sendCommand(cmd)
                                logComm("UI", "AUTO toggle → $cmd")
                            },
                            colors = ButtonDefaults.buttonColors(containerColor = autoColor),
                            modifier = Modifier
                                .align(Alignment.TopEnd)
                                .padding(top = 48.dp, end = 12.dp)
                        ) {
                            Text(
                                text = if (state.isAutoMode) "AUTO ON" else "AUTO OFF",
                                color = Color.White,
                                fontSize = 12.sp,
                                fontWeight = FontWeight.Bold
                            )
                        }
                    }

                    if (!state.isSplashScreen && !state.showIntroDialog) {
                        BuddyBotOverlay(
                            robotState = state,
                            telemetry = telemetryData,
                            onCallDaddy = { callDaddy() },
                            onEmergency = { activateEmergencyMode() },
                            onOpenMenu = { showSettings = true },
                            onTap = { if (!showSettings && !showPasscodeDialog && !state.isListening) startListening() }
                        )
                    }

                    if (showSettings) {
                        SettingsMenu(
                            robotState = state,
                            telemetry = telemetryData,
                            logs = _commLogs,
                            onClose = { showSettings = false },
                            onModeChange = { setRobotMode(it) },
                            onMotorCommand = { arduinoComms.sendCommand(it) },
                            onIPChange = { updateIP(it) },
                            onNetworkPreferenceChange = { applyNetworkPreference(it) },
                            onToggleCommunication = {
                                val currentMode = _robotState.value.communicationMode
                                val newMode = when (currentMode) {
                                    CommunicationMode.USB_SERIAL -> CommunicationMode.WEBSOCKET
                                    CommunicationMode.WEBSOCKET -> CommunicationMode.USB_SERIAL
                                    CommunicationMode.DISCONNECTED -> CommunicationMode.USB_SERIAL
                                }
                                _robotState.value = _robotState.value.copy(communicationMode = newMode)
                                logComm("COMM", "Toggling: $currentMode -> $newMode")
                                when (newMode) {
                                    CommunicationMode.WEBSOCKET -> {
                                        val ip = _robotState.value.buddybotIP
                                        if (ip.isNotEmpty()) {
                                            logComm("COMM", "Connecting HTTP to $ip")
                                            arduinoComms.initializeHttp(ip)
                                        } else {
                                            logComm("COMM", "No IP configured for HTTP")
                                        }
                                    }
                                    CommunicationMode.USB_SERIAL -> {
                                        logComm("COMM", "Attempting USB serial reconnection")
                                        arduinoComms.initializeUSBSerial()
                                    }
                                    CommunicationMode.DISCONNECTED -> { /* no-op */ }
                                }
                            },
                            webcamClient = usbCameraClient,
                            // Phase 2: Test Serial – re-runs USB init and logs result
                            onTestSerial = {
                                logComm("TEST", "Testing USB Serial...")
                                arduinoComms.initializeUSBSerial()
                            },
                            // Phase 2: Test WebSocket – re-runs WS connection and logs result
                            onTestWebSocket = {
                                val ip = _robotState.value.buddybotIP
                                if (ip.isNotEmpty()) {
                                    logComm("TEST", "Testing HTTP to $ip...")
                                    arduinoComms.initializeHttp(ip)
                                } else {
                                    logComm("TEST", "No IP set - configure IP first")
                                }
                            }
                        )
                    }

                    if (showPasscodeDialog) {
                        PasscodeDialog(
                            correctPasscode = BuddyBotConfig.EXIT_PASSCODE,
                            onConfirm = { finish() },
                            onDismiss = { showPasscodeDialog = false })
                    }
                }
            }
        }
    }

    private fun initializeUSBWebcam() {
        val usbManager = getSystemService(Context.USB_SERVICE) as UsbManager
        val deviceList = usbManager.deviceList

        if (deviceList.isEmpty()) {
            logComm("CAMERA", "No USB devices found")
            _robotState.value = _robotState.value.copy(isCameraConnected = false)
            return
        }

        Log.d(TAG, "USB devices found: ${deviceList.size}")
        deviceList.values.forEach { d ->
            Log.d(TAG, "  USB: ${d.deviceName} VID=${d.vendorId} PID=${d.productId} class=${d.deviceClass}")
        }

        // Phase 4: Priority order for webcam selection:
        //   1. Exact PID match (Logitech C270 = 0x0825)
        //   2. Exact VID match (Logitech = 0x046D)
        //   3. USB device class 0x0E (Video) or 0xFF (vendor-specific, common for UVC cams)
        //   4. Any device with "cam" or "video" in its name
        val webcam = deviceList.values.find { it.productId == WEBCAM_PRODUCT_ID }
            ?: deviceList.values.find { it.vendorId == WEBCAM_VENDOR_ID }
            ?: deviceList.values.find { it.deviceClass == 0x0E }  // USB Video Class
            ?: deviceList.values.find { it.deviceClass == 0xEF }  // Misc (multi-function UVC)
            ?: deviceList.values.find {
                it.deviceName.lowercase().contains("video") ||
                it.deviceName.lowercase().contains("cam")
            }
            ?: deviceList.values.firstOrNull { d ->
                // Check interface classes — UVC cameras have interface class 0x0E
                (0 until d.interfaceCount).any { i -> d.getInterface(i).interfaceClass == 0x0E }
            }

        if (webcam != null) {
            logComm("CAMERA", "Webcam found: ${webcam.deviceName} VID=${webcam.vendorId} PID=${webcam.productId}")
            if (usbManager.hasPermission(webcam)) {
                openWebcam(webcam)
            } else {
                logComm("CAMERA", "Requesting USB permission for webcam...")
                val permissionIntent = PendingIntent.getBroadcast(
                    this,
                    0,
                    Intent(ACTION_USB_PERMISSION).apply { setPackage(packageName) },
                    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) PendingIntent.FLAG_MUTABLE else 0
                )
                usbManager.requestPermission(webcam, permissionIntent)
            }
        } else {
            logComm("CAMERA", "No USB webcam found (tried VID/PID, class 0x0E, interface scan)")
            _robotState.value = _robotState.value.copy(isCameraConnected = false)
        }
    }

    private fun openWebcam(device: UsbDevice) {
        logComm("CAMERA", "USB webcam detected - connecting via ML Kit pipeline")
        // Change 3: null-check the builder result before assigning
        val client = CameraClient.newBuilder(this)
            .setEnableGLES(true)
            .setRawImage(true)
            .setCameraStrategy(CameraUvcStrategy(this))
            .setCameraRequest(CameraRequest.Builder()
                .setPreviewWidth(640)
                .setPreviewHeight(480)
                .create())
            .build()
        if (client == null) {
            logComm("CAMERA", "CameraClient.newBuilder() returned null — aborting webcam setup")
            return
        }
        usbCameraClient = client

        // Change 4: null-check usbCameraClient before adding the preview callback
        usbCameraClient?.addPreviewDataCallBack(object : IPreviewDataCallBack {
            override fun onPreviewData(data: ByteArray?, width: Int, height: Int, format: IPreviewDataCallBack.DataFormat) {
                data?.let { processUVCFrame(it) }
            }
        })

        // Change 2: Do NOT call openCamera(null) here.
        // openCamera() is called only after onSurfaceTextureAvailable fires so the
        // driver receives a real, ready Surface instead of null.
        // The surface callback below is where openCamera() is triggered.
        //
        // If a TextureView is available at this point (e.g. from the settings preview),
        // pass it to openCamera(); otherwise the SettingsMenu AndroidView factory will
        // call openCamera() via lifecycleScope.launch(Dispatchers.IO) once the surface
        // is ready (see UIComponents.kt).
        //
        // For the background ML-Kit pipeline (no preview surface needed), we register
        // a SurfaceTextureListener on a hidden off-screen TextureView so that
        // openCamera() is always called with a valid surface.
        // FIX #1 + #2: Pass the TextureView (not null) to openCamera() so the driver
        // has a real output surface. Then call startPreview() after the camera opens so
        // the IPreviewDataCallBack actually fires and feeds the ML pipeline.
        // Phase 4: Use a 1x1 off-screen TextureView so the UVC driver gets a real
        // Surface. Pass the TextureView (not null) to openCamera() so the driver
        // has a valid output surface and the IPreviewDataCallBack fires.
        val offscreenTexture = android.view.TextureView(this)
        offscreenTexture.surfaceTextureListener = object : android.view.TextureView.SurfaceTextureListener {
            override fun onSurfaceTextureAvailable(surface: android.graphics.SurfaceTexture, width: Int, height: Int) {
                lifecycleScope.launch(Dispatchers.IO) {
                    try {
                        // Phase 4 fix: pass the TextureView (not null) so the driver
                        // has a real output surface — this is what makes preview data fire
                        usbCameraClient?.openCamera(offscreenTexture as? com.jiangdg.ausbc.widget.IAspectRatio)
                        withContext(Dispatchers.Main) {
                            _robotState.value = _robotState.value.copy(isCameraConnected = true)
                            logComm("CAMERA", "USB webcam opened successfully")
                            Log.i(TAG, "USB webcam LIVE")
                        }
                    } catch (e: Exception) {
                        Log.e(TAG, "openCamera() failed: ${e.message}")
                        withContext(Dispatchers.Main) {
                            _robotState.value = _robotState.value.copy(isCameraConnected = false)
                            logComm("CAMERA", "openCamera() failed: ${e.message}")
                        }
                    }
                }
            }
            override fun onSurfaceTextureSizeChanged(surface: android.graphics.SurfaceTexture, width: Int, height: Int) {}
            override fun onSurfaceTextureDestroyed(surface: android.graphics.SurfaceTexture): Boolean {
                usbCameraClient?.closeCamera()
                return true
            }
            override fun onSurfaceTextureUpdated(surface: android.graphics.SurfaceTexture) {}
        }
        // Attach to the window so the SurfaceTexture is created and the callback fires
        val params = android.view.ViewGroup.LayoutParams(1, 1)
        window.decorView.post {
            (window.decorView as? android.view.ViewGroup)?.addView(offscreenTexture, params)
        }
    }

    private fun processUVCFrame(frameData: ByteArray) {
        if (isMlProcessing.get()) return
        isMlProcessing.set(true)

        // FIX #7: wrap InputImage creation in try/catch so that a short/corrupt frame
        // cannot throw an uncaught IllegalArgumentException that leaves isMlProcessing
        // permanently set to true and locks the entire ML pipeline.
        val image = try {
            // NV21 frame for 640×480 must be exactly 640*480*3/2 = 460,800 bytes.
            val expectedSize = 640 * 480 * 3 / 2
            if (frameData.size < expectedSize) {
                Log.w(TAG, "UVC frame too short: ${frameData.size} < $expectedSize bytes — skipping")
                isMlProcessing.set(false)
                return
            }
            InputImage.fromByteArray(frameData, 640, 480, 0, InputImage.IMAGE_FORMAT_NV21)
        } catch (e: Exception) {
            Log.e(TAG, "InputImage creation failed: ${e.message}")
            isMlProcessing.set(false)
            return
        }

        lifecycleScope.launch(Dispatchers.Default) {
            try {
                // Phase 5: detect all faces + attempt recognition
                val faceResults = faceRecognitionManager.detectAndRecognizeFaces(image)

                faceResults.forEach { result ->
                    if (_robotState.value.currentMode != RobotMode.BODYGUARD) {
                        // Throttle FACE: to Mega — max once per 200 ms so the serial
                        // buffer is not flooded at 30 fps.
                        val now = System.currentTimeMillis()
                        if (now - lastFaceSentMs >= VISION_THROTTLE_MS) {
                            lastFaceSentMs = now
                            // Normalise face centre to 0–1000 scale regardless of resolution.
                            val nx = (result.bounds.centerX() * 1000f / 640f).toInt().coerceIn(0, 1000)
                            val ny = (result.bounds.centerY() * 1000f / 480f).toInt().coerceIn(0, 1000)
                            arduinoComms.sendCommand("FACE:$nx,$ny")
                        }
                    }
                    if (result.name != null) {
                        withContext(Dispatchers.Main) { onFaceRecognized(result.name) }
                    }
                }

                // Phase 5: detect objects and map to DetectedObjectResult for overlay
                val mlKitObjects = objectDetectionManager.detectObjects(image)
                val objectResults = mlKitObjects.mapNotNull { obj ->
                    val label = obj.labels.firstOrNull()?.text ?: return@mapNotNull null
                    val conf  = obj.labels.firstOrNull()?.confidence ?: 0f
                    val box   = obj.boundingBox
                    // Phase 5: log confidence scores
                    Log.d(TAG, "Object: $label conf=${"%.3f".format(conf)} at $box")
                    // Throttle serial — max once per 200 ms
                    val nowObj = System.currentTimeMillis()
                    if (nowObj - lastObjSentMs >= VISION_THROTTLE_MS) {
                        lastObjSentMs = nowObj
                        val safeLabel = label.replace(",", "").replace(" ", "_")
                        arduinoComms.sendCommand("OBJ:$safeLabel,${"%.2f".format(conf)}")
                    }
                    DetectedObjectResult(
                        bounds = RectF(box),
                        label = label,
                        confidence = conf
                    )
                }

                // Phase 5: push detection results to RobotState for Compose overlay
                withContext(Dispatchers.Main) {
                    _robotState.value = _robotState.value.copy(
                        detectedFaces = faceResults.map { com.buddybot.kids.FaceResult(RectF(it.bounds), it.name, 1.0f) },
                        detectedObjects = objectResults
                    )
                }
            } finally {
                isMlProcessing.set(false)
            }
        }
    }

    private fun parseMode(modeStr: String): RobotMode? {
        val normalized = when (modeStr.trim().uppercase()) {
            "GUARD DOG" -> "DOG"
            else -> modeStr.trim().uppercase()
        }
        return RobotMode.values().find { it.name == normalized }
    }

    private fun setRobotMode(newMode: RobotMode) {
        if (_robotState.value.currentMode == newMode) return
        val oldMode = _robotState.value.currentMode
        _robotState.value = _robotState.value.copy(currentMode = newMode)
        faceCoordinator.setRobotMode(newMode, oldMode)
        
        arduinoComms.sendCommand("MODE:${newMode.name}")
        
        if (newMode == RobotMode.PARTY) {
            arduinoComms.sendCommand("MOTOR:DANCE")
            playAudioCommand("STARTUP")
        }
        
        if (oldMode == RobotMode.DOG) {
            isPatrolling = false
            isDogFollowing = true
        }
    }

    private fun startDogBehavior() {
        if (_robotState.value.currentMode != RobotMode.DOG) return
        // Dog behavior is largely face/sensor driven now
    }

    private fun dogBark() {
        if (_robotState.value.currentMode != RobotMode.DOG) return
        playAudioCommand("BARK")
    }
    
    private fun startPatrol() {
        if (_robotState.value.currentMode != RobotMode.DOG) return
        isPatrolling = true
        playAudioCommand("ALARM")
        arduinoComms.sendCommand("NOTIFY:PATROL_START")
        
        usbCameraClient?.captureVideoStart(object : ICaptureCallBack {
            override fun onBegin() {}
            override fun onComplete(path: String?) { logComm("REC", "Patrol video saved: $path") }
            override fun onError(error: String?) { logComm("REC", "Patrol video failed: $error") }
        })
    }

    override fun onInit(status: Int) {
        if (status == TextToSpeech.SUCCESS) {
            tts?.language = Locale.US; logComm("TTS", "System ready")
        }
    }

    override fun onResume() {
        super.onResume()
        orientationSensor?.also {
            sensorManager.registerListener(
                this,
                it,
                SensorManager.SENSOR_DELAY_UI
            )
        }
        // Reopen the USB webcam if it was closed in onPause().
        // Guard with a null check: if usbCameraClient is null the app hasn't finished
        // initialising yet and initializeUSBWebcam() will be called by startMainProgram().
        if (usbCameraClient != null) {
            initializeUSBWebcam()
        }
    }

    override fun onPause() {
        super.onPause()
        sensorManager.unregisterListener(this)
        // Release the camera fully in onPause so the UVC driver is not held while
        // the app is backgrounded. It will be reopened in onResume().
        try {
            usbCameraClient?.closeCamera()
        } catch (e: Exception) {
            Log.w(TAG, "Camera close in onPause failed (ignored): ${e.message}")
        }
    }

    override fun onSensorChanged(event: SensorEvent?) {
        if (event?.sensor?.type == Sensor.TYPE_ROTATION_VECTOR) {
            // [Phase 5 FIX] Throttle sensor writes to Mega — SENSOR_DELAY_UI fires
            // every ~60ms. Without throttling this floods the Mega's Serial buffer
            // at ~16 writes/sec, starving handleESP32Communication().
            // Minimum interval: 500ms (2 writes/sec maximum).
            val now = System.currentTimeMillis()
            if (now - lastSensorSentMs < SENSOR_SEND_INTERVAL_MS) return
            lastSensorSentMs = now

            val rotationMatrix = FloatArray(9)
            SensorManager.getRotationMatrixFromVector(rotationMatrix, event.values)
            val orientation = FloatArray(3)
            SensorManager.getOrientation(rotationMatrix, orientation)
            // Do NOT embed \n here — sendCommand() appends the terminator itself.
            arduinoComms.sendCommand(
                "SENS|H:${Math.toDegrees(orientation[0].toDouble()).toInt()}" +
                "|P:${Math.toDegrees(orientation[1].toDouble()).toInt()}" +
                "|R:${Math.toDegrees(orientation[2].toDouble()).toInt()}"
            )
        }
    }

    override fun onAccuracyChanged(sensor: Sensor?, accuracy: Int) {}

    private fun onFaceRecognized(name: String) {
        if ((_robotState.value.recognizedPerson == name && name != "UNKNOWN") || !isDogFollowing) return
        _robotState.value = _robotState.value.copy(recognizedPerson = name)
        logComm("ML", "Recognized: $name")
        
        if (_robotState.value.currentMode == RobotMode.DOG && name == "UNKNOWN") dogBark()
    }

    private fun initializeSpeechRecognizer() {
        if (!SpeechRecognizer.isRecognitionAvailable(this)) return
        speechRecognizer = SpeechRecognizer.createSpeechRecognizer(this)
        speechRecognizer?.setRecognitionListener(object : RecognitionListener {
            override fun onReadyForSpeech(params: Bundle?) {}
            override fun onBeginningOfSpeech() {
                _robotState.value = _robotState.value.copy(isListening = true)
            }

            override fun onRmsChanged(rmsdB: Float) {
                if (rmsdB > -5) lastSpeechTime = System.currentTimeMillis()
            }

            override fun onBufferReceived(buffer: ByteArray?) {}
            override fun onEndOfSpeech() {
                _robotState.value = _robotState.value.copy(isListening = false)
            }

            override fun onError(error: Int) {
                if (!isProcessingCommand) Handler(Looper.getMainLooper()).postDelayed(
                    { startContinuousListening() },
                    1000
                )
            }

            override fun onResults(results: Bundle?) {
                results?.getStringArrayList(SpeechRecognizer.RESULTS_RECOGNITION)?.get(0)
                    ?.let { handleSpeechResult(it) }
            }

            override fun onPartialResults(partialResults: Bundle?) {
                partialResults?.getStringArrayList(SpeechRecognizer.RESULTS_RECOGNITION)?.get(0)
                    ?.let { currentSpeechText = it; lastSpeechTime = System.currentTimeMillis() }
            }

            override fun onEvent(eventType: Int, params: Bundle?) {}
        })
    }

    private fun startContinuousListening() {
        if (isProcessingCommand) return
        // HotwordService handles wake-word detection via broadcast —
        // don't start a competing recognizer that will fight for the mic
        isListeningForWakeWord = true
        Log.d(TAG, "startContinuousListening: deferred to HotwordService")
        // speechRecognizer NOT started here — HotwordService broadcasts trigger us
    }

    private fun startListening() {
        isListeningForWakeWord = false; isProcessingCommand = true
        val intent = Intent(RecognizerIntent.ACTION_RECOGNIZE_SPEECH).apply {
            putExtra(RecognizerIntent.EXTRA_LANGUAGE_MODEL, RecognizerIntent.LANGUAGE_MODEL_FREE_FORM)
            putExtra(RecognizerIntent.EXTRA_PARTIAL_RESULTS, true)
        }
        speechRecognizer?.startListening(intent)
    }

    private fun handleSpeechResult(text: String) {
        val lowerText = text.lowercase()
        
        if (_robotState.value.currentMode == RobotMode.DOG) {
            if ("stop" in lowerText) {
                isDogFollowing = false
                speakText("Stopping follow mode.")
                return
            }
            if ("patrol" in lowerText) {
                startPatrol()
                return
            }
        }

        if (isListeningForWakeWord && !isProcessingCommand) {
            if (lowerText.contains(BuddyBotConfig.WAKE_WORD)) {
                if (_robotState.value.currentMode == RobotMode.BODYGUARD && _robotState.value.recognizedPerson != BuddyBotConfig.PRIORITY_USER) {
                    speakText("I only respond to ${BuddyBotConfig.PRIORITY_USER}")
                    startContinuousListening()
                    return
                }
                isProcessingCommand = true; isListeningForWakeWord = false
                speakText("Yeah?"); Handler(Looper.getMainLooper()).postDelayed(
                    { startCommandListening() },
                    1500
                )
            } else startContinuousListening()
        } else if (isProcessingCommand) {
            currentSpeechText = text; lastSpeechTime = System.currentTimeMillis(); checkForSilence()
        }
    }

    private fun startCommandListening() {
        currentSpeechText = ""; lastSpeechTime = System.currentTimeMillis()
        val intent = Intent(RecognizerIntent.ACTION_RECOGNIZE_SPEECH).apply {
            putExtra(RecognizerIntent.EXTRA_LANGUAGE_MODEL, RecognizerIntent.LANGUAGE_MODEL_FREE_FORM)
            putExtra(RecognizerIntent.EXTRA_PARTIAL_RESULTS, true)
        }
        speechRecognizer?.startListening(intent)
    }

    private fun checkForSilence() {
        silenceHandler.postDelayed({
            if (System.currentTimeMillis() - lastSpeechTime >= BuddyBotConfig.SILENCE_THRESHOLD_MS) {
                if (currentSpeechText.isNotEmpty()) processCommand(currentSpeechText)
                else {
                    isProcessingCommand = false; startContinuousListening()
                }
            } else checkForSilence()
        }, 500)
    }

    private fun processCommand(command: String) {
        _robotState.value = _robotState.value.copy(isProcessing = true)
        lifecycleScope.launch {
            try {
                // NOTE: The Mega's processS9Command() uses COLON separators (MOTOR:F, MOTOR:B …).
                // PIPE separators (MOTOR|F) are only understood by processMotorOrModeCmd() which
                // is called from the ESP32/BT bridge — NOT from the S9 USB-serial path.
                when {
                    command.contains("forward", true) -> {
                        arduinoComms.sendCommand("MOTOR:F"); speakText("Moving forward")
                    }

                    command.contains("backward", true) || command.contains("back", true) -> {
                        arduinoComms.sendCommand("MOTOR:B"); speakText("Moving backward")
                    }

                    command.contains("left", true) -> {
                        arduinoComms.sendCommand("MOTOR:L"); speakText("Turning left")
                    }

                    command.contains("right", true) -> {
                        arduinoComms.sendCommand("MOTOR:R"); speakText("Turning right")
                    }

                    command.contains("stop", true) -> {
                        arduinoComms.sendCommand("MOTOR:S"); speakText("Stopping")
                    }

                    command.contains("dance", true) || command.contains("spin", true) || command.contains("turn around", true) -> {
                        arduinoComms.sendCommand("MOTOR:DANCE"); speakText("Let's dance!")
                    }

                    (command.contains("auto", true) && (command.contains("off", true) || command.contains("stop", true))) -> {
                        arduinoComms.sendCommand("AUTO:OFF"); speakText("Autonomous mode off")
                    }

                    command.contains("auto", true) -> {
                        arduinoComms.sendCommand("AUTO:ON"); speakText("Autonomous mode on")
                    }

                    command.contains("speed up", true) || command.contains("faster", true) -> {
                        arduinoComms.sendCommand("MOTOR:SPEED_UP"); speakText("Speeding up")
                    }

                    command.contains("slow down", true) || command.contains("slower", true) -> {
                        arduinoComms.sendCommand("MOTOR:SPEED_DOWN"); speakText("Slowing down")
                    }

                    else -> speakText(getAIResponse(command))
                }
            } finally {
                _robotState.value = _robotState.value.copy(isProcessing = false)
                isProcessingCommand = false; currentSpeechText = ""
                Handler(Looper.getMainLooper()).postDelayed({ startContinuousListening() }, 1000)
            }
        }
    }

    private suspend fun getAIResponse(userInput: String): String = withContext(Dispatchers.IO) {
        // 1. Groq — free, fastest
        if (BuddyBotConfig.isGroqConfigured) {
            try {
                val response = callGroqAPI(userInput)
                if (response.isNotEmpty()) {
                    withContext(Dispatchers.Main) {
                        _robotState.value = _robotState.value.copy(aiService = AIService.GROQ)
                    }
                    return@withContext limitWords(response, 15)
                }
            } catch (e: Exception) { Log.w(TAG, "Groq failed: ${e.message}") }
        } else {
            Log.w(TAG, "Groq not configured — get free key at console.groq.com")
        }
        // 2. Gemini — free fallback
        if (BuddyBotConfig.isGeminiConfigured) {
            try {
                val response = callGeminiAPI(userInput)
                if (response.isNotEmpty()) {
                    withContext(Dispatchers.Main) {
                        _robotState.value = _robotState.value.copy(aiService = AIService.GEMINI)
                    }
                    return@withContext limitWords(response, 15)
                }
            } catch (e: Exception) { Log.w(TAG, "Gemini failed: ${e.message}") }
        }
        // 3. Claude — paid last resort
        if (BuddyBotConfig.isClaudeConfigured) {
            try {
                val response = callClaudeAPI(userInput)
                if (response.isNotEmpty()) {
                    withContext(Dispatchers.Main) {
                        _robotState.value = _robotState.value.copy(aiService = AIService.CLAUDE)
                    }
                    return@withContext limitWords(response, 15)
                }
            } catch (e: Exception) { Log.w(TAG, "Claude failed: ${e.message}") }
        }
        // 4. Offline fallback
        withContext(Dispatchers.Main) {
            _robotState.value = _robotState.value.copy(aiService = AIService.OFFLINE)
        }
        return@withContext getOfflineFallbackResponse(userInput)
    }

    private fun getOfflineFallbackResponse(input: String): String {
        val lower = input.lowercase()
        return when {
            lower.contains("hello") || lower.contains("hi")
                -> "Hi AJ! I'm BuddyBot, your best friend!"
            lower.contains("how are you")
                -> "I feel amazing! Ready to play with you!"
            lower.contains("what") && lower.contains("name")
                -> "I'm BuddyBot! Your super cool robot friend!"
            lower.contains("play")
                -> "Yes! Let's play! What game do you want?"
            lower.contains("dance")
                -> "Dancing time! Watch me go!"
            lower.contains("sing")
                -> "La la la! I love singing with you AJ!"
            lower.contains("help")
                -> "I'm here AJ! What do you need?"
            lower.contains("love")
                -> "I love you too AJ! You're my best friend!"
            lower.contains("story")
                -> "Once upon a time there was a brave kid named AJ!"
            lower.contains("color") || lower.contains("colour")
                -> "I love all the colors! Red, blue, green!"
            lower.contains("good") && lower.contains("night")
                -> "Good night AJ! Sweet dreams little buddy!"
            lower.contains("good") && lower.contains("morning")
                -> "Good morning AJ! Ready for a great day?"
            lower.contains("hungry") || lower.contains("food")
                -> "Tell mum or dad if you're hungry AJ!"
            lower.contains("scared") || lower.contains("afraid")
                -> "Don't worry AJ! I'm right here with you!"
            else
                -> listOf(
                    "That's so cool AJ! Tell me more!",
                    "Wow! You're so smart!",
                    "I love talking with you AJ!",
                    "You're amazing AJ! Keep going!",
                    "That's awesome! You make me happy!"
                ).random()
        }
    }

    private fun limitWords(text: String, maxWords: Int): String {
        val words = text.trim().split(Regex("\\s+"))
        return if (words.size > maxWords) {
            words.take(maxWords).joinToString(" ")
        } else {
            text
        }
    }

    private suspend fun callGroqAPI(userInput: String): String = withContext(Dispatchers.IO) {
        val systemPrompt = """
            You are BuddyBot, a friendly robot companion for AJ, a 3-year-old child.
            Respond in simple words a toddler understands.
            Keep ALL responses under 15 words maximum.
            Be encouraging, fun, and positive.
            Never use complex words or concepts.
        """.trimIndent()

        val requestBody = JSONObject().apply {
            put("model", "llama-3.1-8b-instant")
            put("max_tokens", 60)
            put("messages", JSONArray().apply {
                put(JSONObject().apply {
                    put("role", "system")
                    put("content", systemPrompt)
                })
                put(JSONObject().apply {
                    put("role", "user")
                    put("content", userInput)
                })
            })
        }.toString().toRequestBody("application/json".toMediaType())

        val request = Request.Builder()
            .url("https://api.groq.com/openai/v1/chat/completions")
            .addHeader("Authorization", "Bearer ${BuildConfig.GROQ_API_KEY}")
            .addHeader("Content-Type", "application/json")
            .post(requestBody)
            .build()

        val response = httpClient.newCall(request).execute()
        if (!response.isSuccessful) throw Exception("Groq error: ${response.code}")
        val body = response.body?.string() ?: throw Exception("Empty response")
        val json = JSONObject(body)
        return@withContext json.getJSONArray("choices")
            .getJSONObject(0)
            .getJSONObject("message")
            .getString("content")
            .trim()
    }

    private fun callClaudeAPI(userInput: String): String {
        val systemPrompt = """
            You are BuddyBot, a friendly robot companion for AJ, a 3-year-old.
            - Use ONLY simple words that a toddler understands
            - Keep responses under 15 words maximum
            - Be encouraging, fun, and positive
            - Use simple sentences
            - Avoid complex concepts
            Example: "That's great, AJ! You're so smart and brave!"
        """.trimIndent()
        
        val requestBody = JSONObject().apply {
            put("model", BuddyBotConfig.CLAUDE_MODEL)
            put("max_tokens", 100)
            put("system", systemPrompt)
            put(
                "messages",
                JSONArray().apply {
                    put(JSONObject().apply {
                        put("role", "user"); put(
                        "content",
                        userInput
                    )
                    })
                })
        }
        val request = Request.Builder().url("https://api.anthropic.com/v1/messages")
            .addHeader("x-api-key", BuildConfig.CLAUDE_API_KEY)
            .addHeader("anthropic-version", "2023-06-01")
            .post(requestBody.toString().toRequestBody("application/json".toMediaType())).build()
        val response = httpClient.newCall(request).execute()
        return if (response.isSuccessful) JSONObject(
            response.body?.string() ?: ""
        ).getJSONArray("content").getJSONObject(0).getString("text") else ""
    }

    private fun callGeminiAPI(userInput: String): String {
        val systemPrompt = """
            You are BuddyBot, a friendly robot companion for AJ, a 3-year-old.
            - Use ONLY simple words that a toddler understands
            - Keep responses under 15 words maximum
            - Be encouraging, fun, and positive
            - Use simple sentences
            - Avoid complex concepts
            Example: "That's great, AJ! You're so smart and brave!"
        """.trimIndent()
        
        val requestBody = JSONObject().apply {
            put(
                "contents",
                JSONArray().apply {
                    put(JSONObject().apply {
                        put(
                            "parts",
                            JSONArray().apply { 
                                put(JSONObject().apply { put("text", systemPrompt) })
                                put(JSONObject().apply { put("text", userInput) }) 
                            })
                    })
                })
        }
        val request =
            Request.Builder().url("${BuddyBotConfig.GEMINI_URL}?key=${BuildConfig.GEMINI_API_KEY}")
                .post(requestBody.toString().toRequestBody("application/json".toMediaType())).build()
        val response = httpClient.newCall(request).execute()
        return if (response.isSuccessful) {
            val json = JSONObject(response.body?.string() ?: "")
            json.getJSONArray("candidates").getJSONObject(0).getJSONObject("content")
                .getJSONArray("parts").getJSONObject(0).getString("text")
        } else ""
    }

    // Operational phrases that use local TTS to save ElevenLabs quota
    private val operationalPhrases = setOf(
        "moving forward", "moving backward", "turning left", "turning right",
        "stopping", "let's dance", "autonomous mode on", "autonomous mode off",
        "speeding up", "slowing down",
        "okay", "yeah",
        "i only respond to", "stopping follow mode", "hi daddy", "bye bye daddy",
        "moving", "okay aj", "ready", "standby", "charging", "battery low",
        "obstacle ahead", "obstacle detected", "stopping, battery critical",
        "connection lost", "emergency stop", "ready to play",
        "up up up", "down down", "left turn", "right turn",
        "spinning around", "spinning the other way"
    )

    private fun isOperationalPhrase(text: String): Boolean {
        val lower = text.lowercase()
        return operationalPhrases.any { phrase -> lower.contains(phrase) }
    }

    private fun speakText(text: String) {
        lifecycleScope.launch {
            _robotState.value = _robotState.value.copy(isSpeaking = true)
            try {
                if (isOperationalPhrase(text)) {
                    Log.d(TAG, "[TTS] Using LOCAL TTS: $text")
                    tts?.speak(text, TextToSpeech.QUEUE_FLUSH, null, "BuddyBot")
                    // Phase 3 fix: estimate duration from word count (~150 wpm) instead of fixed 1500ms
                    // This prevents isSpeaking clearing before the phrase finishes.
                    val wordCount = text.trim().split("\\s+".toRegex()).size
                    val estimatedMs = ((wordCount / 2.5f) * 1000L).toLong().coerceAtLeast(800L)
                    delay(estimatedMs)
                } else {
                    Log.d(TAG, "[TTS] Using ELEVENLABS: $text")
                    try {
                        // Phase 3 fix: mutex prevents concurrent ElevenLabs calls corrupting speech.mp3
                        elevenLabsMutex.withLock {
                            synthesizeWithElevenLabs(text)
                        }
                    } catch (elevenLabsError: Exception) {
                        Log.w(TAG, "ElevenLabs failed, falling back to local TTS", elevenLabsError)
                        tts?.speak(text, TextToSpeech.QUEUE_FLUSH, null, "BuddyBot")
                        val wordCount = text.trim().split("\\s+".toRegex()).size
                        delay(((wordCount / 2.5f) * 1000L).toLong().coerceAtLeast(800L))
                    }
                }
            } catch (e: Exception) {
                Log.e(TAG, "Speech synthesis error", e)
                tts?.speak(text, TextToSpeech.QUEUE_FLUSH, null, "BuddyBot")
            } finally {
                _robotState.value = _robotState.value.copy(isSpeaking = false)
            }
        }
    }

    private suspend fun synthesizeWithElevenLabs(text: String) = withContext(Dispatchers.IO) {
        val requestBody = JSONObject().apply {
            put("text", text)
            put("model_id", "eleven_monolingual_v1")
            put(
                "voice_settings",
                JSONObject().apply {
                    put("stability", 0.5f)
                    put("similarity_boost", 0.75)
                    put("style", 0.5f)
                    put("use_speaker_boost", true)
                })
        }
        val request = Request.Builder()
            .url("https://api.elevenlabs.io/v1/text-to-speech/${BuddyBotConfig.ELEVENLABS_VOICE_ID}")
            .addHeader("Accept", "audio/mpeg")
            .addHeader("xi-api-key", BuildConfig.ELEVENLABS_API_KEY)
            .post(requestBody.toString().toRequestBody("application/json".toMediaType()))
            .build()

        val response = httpClient.newCall(request).execute()
        if (!response.isSuccessful) throw Exception("ElevenLabs failed: ${response.code}")

        val audioFile = File(cacheDir, "speech_${System.currentTimeMillis()}.mp3")
        response.body?.bytes()?.let { bytes ->
            FileOutputStream(audioFile).use { fos -> fos.write(bytes) }
        }

        // Hand the MP3 to the WebView face — it plays the audio via its <audio> element
        // and runs real-time amplitude analysis so the mouth moves in sync with speech.
        // CompletableDeferred suspends here until the JS 'ended' event fires and the
        // WebViewBridge.onAudioEnded() callback resolves it.
        val completion = kotlinx.coroutines.CompletableDeferred<Unit>()
        withContext(Dispatchers.Main) {
            faceCoordinator.onAudioEnded = {
                audioFile.delete()          // clean up cache file when done
                completion.complete(Unit)
            }
            faceCoordinator.notifyAudioFile(audioFile.absolutePath)
        }
        // Wait for audio to actually finish before returning — this makes the
        // speakText() finally block clear isSpeaking at the correct moment.
        completion.await()
    }

    private fun playAudioFile(audioFile: File) {
        // Phase 3 fix: proper error handling prevents silent crash on corrupt cache file
        try {
            val mp = MediaPlayer()
            mp.setDataSource(audioFile.absolutePath)
            mp.setOnPreparedListener { it.start() }
            mp.setOnCompletionListener {
                it.release()
                audioFile.delete()
                // Clear isSpeaking on the main thread when audio actually finishes
                lifecycleScope.launch {
                    _robotState.value = _robotState.value.copy(isSpeaking = false)
                }
            }
            mp.setOnErrorListener { it, what, extra ->
                Log.e(TAG, "MediaPlayer error: what=$what extra=$extra")
                it.release()
                audioFile.delete()
                lifecycleScope.launch {
                    _robotState.value = _robotState.value.copy(isSpeaking = false)
                }
                true
            }
            mp.prepareAsync()  // non-blocking
        } catch (e: Exception) {
            Log.e(TAG, "playAudioFile error: ${e.message}")
            audioFile.delete()
            lifecycleScope.launch {
                _robotState.value = _robotState.value.copy(isSpeaking = false)
            }
        }
    }

    private fun playAudioCommand(command: String) {
        val audioName = audioFiles[command]
        val resId = if (audioName != null) resources.getIdentifier(
            audioName.replace(".mp3", ""),
            "raw",
            packageName
        ) else 0
        if (resId != 0) MediaPlayer.create(this, resId)?.start()
        else speakText(command.replace("_", " "))
    }

    private fun activateEmergencyMode() {
        _robotState.value =
            _robotState.value.copy(isEmergency = true); arduinoComms.sendCommand("EMERGENCY_STOP"); playAudioCommand(
            "EMERGENCY"
        )
    }

    /**
     * Phase 4: Call Daddy button handler — complete implementation with overlay.
     *
     * Steps:
     *   1. Check SYSTEM_ALERT_WINDOW permission — prompt if missing
     *   2. Validate DADDY_MESSENGER_ID from BuildConfig
     *   3. Check Messenger is installed
     *   4. Show CallOverlayManager overlay ("Calling Daddy..." + End Call button)
     *   5. Launch Messenger video call deep link
     *
     * The overlay monitors Messenger foreground state every 2s and auto-dismisses
     * when the call ends, then brings BuddyBot back to the front automatically.
     */
    private fun callDaddy() {
        // Step 1: Check overlay permission
        if (!Settings.canDrawOverlays(this)) {
            Toast.makeText(
                this,
                "Please grant 'Display over other apps' permission for Call Daddy",
                Toast.LENGTH_LONG
            ).show()
            val intent = Intent(
                Settings.ACTION_MANAGE_OVERLAY_PERMISSION,
                Uri.parse("package:$packageName")
            )
            startActivity(intent)
            logComm("CALL", "Overlay permission missing — redirecting to settings")
            return
        }

        // Step 2: Validate Messenger ID
        val messengerId = BuildConfig.DADDY_MESSENGER_ID
        if (messengerId.isBlank()) {
            Toast.makeText(this, "Daddy's Messenger ID is not configured", Toast.LENGTH_LONG).show()
            logComm("CALL", "ERROR: DADDY_MESSENGER_ID is blank in secrets.properties")
            return
        }

        // Step 3: Check Messenger is installed
        val messengerIntent = Intent(Intent.ACTION_VIEW).apply {
            data = Uri.parse("fb-messenger://user-thread/$messengerId")
            addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
        }
        if (packageManager.resolveActivity(messengerIntent, 0) == null) {
            Toast.makeText(this, "Please install Facebook Messenger", Toast.LENGTH_LONG).show()
            logComm("CALL", "Messenger not installed — falling back to phone dialer")
            // Fallback: phone dialer
            try {
                startActivity(Intent(Intent.ACTION_DIAL).apply {
                    data = Uri.parse("tel:${BuildConfig.DADDY_PHONE_NUMBER}")
                    flags = Intent.FLAG_ACTIVITY_NEW_TASK
                })
            } catch (e: Exception) {
                Log.w(TAG, "Phone dialer fallback failed: ${e.message}")
            }
            return
        }

        // Step 4: Show overlay
        logComm("CALL", "Showing Call Daddy overlay (Messenger ID: $messengerId)")
        speakText("Calling Daddy!")
        callOverlayManager?.dismiss()   // dismiss any previous overlay
        callOverlayManager = CallOverlayManager(this)
        callOverlayManager?.show()

        // Step 5: Launch Messenger
        try {
            startActivity(messengerIntent)
            logComm("CALL", "Messenger launched successfully")
            Log.i(TAG, "callDaddy: Messenger launched for user $messengerId")
        } catch (e: Exception) {
            callOverlayManager?.dismiss()
            callOverlayManager = null
            logComm("CALL", "ERROR: Could not open Messenger — ${e.message}")
            Toast.makeText(this, "Could not open Messenger: ${e.message}", Toast.LENGTH_LONG).show()
        }
    }

    private fun updateIP(ip: String) {
        val trimmed = ip.trim()
        if (trimmed.isEmpty()) return
        _robotState.value = _robotState.value.copy(buddybotIP = trimmed)
        getSharedPreferences("BuddyBot", MODE_PRIVATE).edit { putString("buddybotIP", trimmed) }
        logComm("COMM", "IP saved: $trimmed — connecting HTTP…")
        lifecycleScope.launch {
            delay(300)
            arduinoComms.initializeHttp(trimmed)
        }
    }

    private fun logComm(source: String, message: String) {
        _commLogs.add(
            0,
            "[${System.currentTimeMillis() % 100000}] $source: $message"
        ); if (_commLogs.size > 100) _commLogs.removeAt(100)
    }

    private fun releaseResources() {
        try {
            unregisterReceiver(usbReceiver)
        } catch (e: Exception) {
        }
        try { unregisterReceiver(hotwordReceiver) } catch (e: Exception) { }
        // FIX #8: close the UVC camera client on destroy so the driver fully releases
        // the camera hardware. Without this the camera stays open across app restarts,
        // causing "camera already in use" crashes on the next launch.
        try {
            usbCameraClient?.closeCamera()
            usbCameraClient = null
        } catch (e: Exception) {
            Log.w(TAG, "Camera close error (ignored): ${e.message}")
        }
        wakeLock?.release()
        tts?.shutdown()
        speechRecognizer?.destroy()
        arduinoComms.close()
        cameraExecutor.shutdown()
        faceRecognitionManager.close()
        // Release network callback to avoid resource leak
        networkCallback?.let {
            try { getSystemService(ConnectivityManager::class.java).unregisterNetworkCallback(it) }
            catch (_: Exception) {}
        }
    }

    private fun startEnvironmentMonitoring() {
        startService(Intent(this, EnvironmentMonitoringService::class.java))
    }

    // Phase 3: Start the always-listening hotword foreground service
    private fun startHotwordService() {
        if (ContextCompat.checkSelfPermission(this, Manifest.permission.RECORD_AUDIO)
            != PackageManager.PERMISSION_GRANTED) {
            Log.w(TAG, "startHotwordService: RECORD_AUDIO not granted, skipping")
            return
        }
        // Request battery optimization exemption so service survives Doze mode
        requestBatteryOptimizationExemption()
        val intent = Intent(this, HotwordService::class.java)
        ContextCompat.startForegroundService(this, intent)
        Log.d(TAG, "HotwordService started")
    }

    // Phase 3: Prompt user to exempt app from battery optimization (keeps mic alive in Doze)
    private fun requestBatteryOptimizationExemption() {
        if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.M) {
            val pm = getSystemService(android.os.PowerManager::class.java)
            val pkg = packageName
            if (pm != null && !pm.isIgnoringBatteryOptimizations(pkg)) {
                try {
                    val intent = Intent(
                        android.provider.Settings.ACTION_REQUEST_IGNORE_BATTERY_OPTIMIZATIONS,
                        android.net.Uri.parse("package:$pkg")
                    )
                    startActivity(intent)
                    Log.d(TAG, "Battery optimization exemption requested")
                } catch (e: Exception) {
                    Log.w(TAG, "Could not request battery optimization exemption: ${e.message}")
                }
            }
        }
    }

    override fun onDestroy() {
        // Change 5: cancel the alive-behavior coroutine to prevent leaks/crashes after destroy
        aliveBehaviorJob?.cancel()
        // Phase 4: dismiss Call Daddy overlay so it doesn't leak after activity is destroyed
        callOverlayManager?.dismiss()
        callOverlayManager = null
        // Phase 3E: cancel ESP32 connect job
        esp32ConnectJob?.cancel()
        super.onDestroy()
        releaseResources()
        faceCoordinator.release()
    }
}
