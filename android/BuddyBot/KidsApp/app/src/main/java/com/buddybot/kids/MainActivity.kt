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
import android.provider.Settings
import android.view.Surface
import android.view.View
import android.view.WindowManager
import android.webkit.WebView
import android.widget.Toast
import android.app.NotificationChannel
import android.app.NotificationManager
import androidx.core.app.NotificationCompat
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

    private val _robotState = MutableStateFlow(RobotState())
    val robotState: StateFlow<RobotState> get() = _robotState

    private lateinit var faceCoordinator: FaceCoordinator
    private lateinit var securityGatekeeper: SecurityGatekeeper
    private lateinit var viewModel: MainViewModel
    private lateinit var aiRouter: AIRouter
    private var guardianEngine: GuardianEngine? = null

    // V37: telemetry is owned by MessageRouter via MainViewModel.
    // These StateFlows delegate to the ViewModel so all UI observers stay compatible.
    // FIX: Use a default MutableStateFlow so setupComposeUI() doesn't crash before
    // viewModel is initialized in initializeApp().
    private val _defaultTelemetry = MutableStateFlow(TelemetryData())
    private val telemetry: StateFlow<TelemetryData> get() = if (::viewModel.isInitialized) viewModel.telemetry else _defaultTelemetry

    private val _commLogs = mutableStateListOf<String>()

    private var usbCameraClient: CameraClient? = null
    private var tts: TextToSpeech? = null
    private var speechRecognizer: SpeechRecognizer? = null

    // Change 5: store the alive-behavior Job so it can be cancelled in onDestroy()
    private var aliveBehaviorJob: Job? = null

    // Standard OkHttpClient for all AI/TTS HTTP calls.
    // Uses the system default socket factory so Android's OS automatically
    // routes traffic through whatever internet connection is active (4G or WiFi).
    private var httpClient = OkHttpClient.Builder()
        .connectTimeout(15, java.util.concurrent.TimeUnit.SECONDS)
        .readTimeout(30, java.util.concurrent.TimeUnit.SECONDS)
        .writeTimeout(30, java.util.concurrent.TimeUnit.SECONDS)
        .build()

    /**
     * Applies the network preference by persisting the choice.
     * The httpClient always uses the system default socket factory so Android
     * automatically routes all HTTP traffic through whichever active internet
     * connection the phone has (4G Cellular or Home Wi-Fi).
     * No custom socket factories or Network callbacks are used, ensuring AI/TTS
     * calls always reach the internet without interference.
     */
    private fun applyNetworkPreference(pref: NetworkPreference) {
        getSharedPreferences("buddybot", MODE_PRIVATE).edit {
            putString("network_pref", pref.name)
        }
        _robotState.value = _robotState.value.copy(networkPreference = pref)
        Log.d(TAG, "Network preference: $pref (OS handles routing)")
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

    // ElevenLabs lipsync — only set during ElevenLabs playback, never during local TTS
    private val _lipSyncAmplitude     = MutableStateFlow(0f)
    private val _isElevenLabsSpeaking = MutableStateFlow(false)

    // Phase 4: Call Daddy overlay manager
    private var callOverlayManager: CallOverlayManager? = null

    // Phase 5: Sensor throttle — only send SENS| to Mega at most once per 500ms
    // (SENSOR_DELAY_UI fires ~60ms; without throttle this floods the serial buffer)
    private var lastSensorSentMs = 0L
    private val SENSOR_SEND_INTERVAL_MS = 500L

    // Permissions required for core function — app cannot run without these
    private val CRITICAL_PERMISSIONS = setOf(
        Manifest.permission.RECORD_AUDIO,   // wake word + command capture
        Manifest.permission.CAMERA           // face/object detection
    )

    private val permissionLauncher = registerForActivityResult(
        ActivityResultContracts.RequestMultiplePermissions()
    ) { permissions ->
        // Only exit if a CRITICAL permission was denied.
        // POST_NOTIFICATIONS / READ_MEDIA_* / WRITE_EXTERNAL_STORAGE are optional
        // and commonly denied on Samsung One UI — do NOT kill the app for these.
        val criticalDenied = CRITICAL_PERMISSIONS.any { perm ->
            permissions.containsKey(perm) && permissions[perm] == false
        }
        if (criticalDenied) {
            Log.e(TAG, "Critical permission denied — cannot run: " +
                CRITICAL_PERMISSIONS.filter { permissions[it] == false })
            finish()
        } else {
            val denied = permissions.filter { !it.value }.keys
            if (denied.isNotEmpty()) Log.w(TAG, "Non-critical permissions denied: $denied")
            initializeApp()
        }
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

            // V37: Build the ViewModel now that ArduinoComms exists.
            // The ViewModel wires MessageRouter → ArduinoComms.onMessageReceived.
            try {
                viewModel = MainViewModel(arduinoComms)
                // Mirror ViewModel's robotState (kept authoritative for MessageRouter's
                // mode-change / mode-request emissions) into MainActivity's local _robotState.
                lifecycleScope.launch {
                    viewModel.robotState.collect { vmState ->
                        _robotState.value = _robotState.value.copy(
                            currentMode         = vmState.currentMode,
                            communicationMode   = vmState.communicationMode,
                            showPinEntry        = vmState.showPinEntry,
                            requestedMode       = vmState.requestedMode,
                            wifiSetupPhase      = vmState.wifiSetupPhase
                        )
                    }
                }
                // Also register the local handleArduinoMessage for Activity-side side effects
                // (banners, TTS, phone-call escalation). This is additive — the ViewModel's
                // router callback is set inside MainViewModel.init and stays the primary hook.
                Log.d(TAG, "initializeApp: MainViewModel + MessageRouter initialized")
            } catch (e: Exception) {
                Log.e(TAG, "Error initializing MainViewModel", e)
            }

            // ── AIRouter — factored AI fallback chain ─────────────────────────
            try {
                aiRouter = AIRouter(
                    httpClientProvider = { this.httpClient },
                    onProviderChanged = { service ->
                        runOnUiThread {
                            _robotState.value = _robotState.value.copy(aiService = service)
                        }
                    }
                )
                Log.d(TAG, "initializeApp: AIRouter ready (Groq→Gemini→Claude→Offline)")
            } catch (e: Exception) {
                Log.e(TAG, "Error initializing AIRouter", e)
            }

            // ── GuardianEngine — behavioural monitoring pipeline ───────────────
            try {
                guardianEngine = GuardianEngine(
                    context         = this@MainActivity,
                    scope           = lifecycleScope,
                    arduinoComms    = arduinoComms,
                    speak           = { text -> speakText(text) },
                    notifyParentApp = { event -> sendGuardianAlert(event) }
                )
                // Register with EnvironmentMonitoringService so audio samples feed in
                EnvironmentMonitoringService.guardianEngine = guardianEngine
                Log.d(TAG, "initializeApp: GuardianEngine active")
            } catch (e: Exception) {
                Log.e(TAG, "Error initializing GuardianEngine", e)
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
                arduinoComms.onUsbPermissionRequested = {
                    runOnUiThread {
                        Toast.makeText(
                            this,
                            "Allow USB access so BuddyBot can talk to the robot",
                            Toast.LENGTH_LONG
                        ).show()
                        logComm("COMM", "USB permission requested — tap OK on the dialog")
                    }
                }
                arduinoComms.onUsbPermissionDenied = {
                    runOnUiThread {
                        Toast.makeText(
                            this,
                            "USB permission denied — serial control won't work",
                            Toast.LENGTH_LONG
                        ).show()
                        logComm("COMM", "USB permission denied by user")
                    }
                }
                // V37: MessageRouter (owned by ViewModel) is the primary parser.
                // Chain the Activity-side side-effect delegator AFTER the router.
                arduinoComms.onMessageReceived = { msg ->
                    // First: let the ViewModel's router parse the line (owns telemetry state).
                    viewModel.router.handleLine(msg)
                    // Then: run Activity-side side effects (banners, TTS, phone-call).
                    if (Looper.myLooper() == Looper.getMainLooper()) {
                        handleArduinoMessage(msg)
                    } else {
                        runOnUiThread { handleArduinoMessage(msg) }
                    }
                }
                arduinoComms.initialize()
                Log.d(TAG, "initializeApp: Arduino communications initialized")
                
                // Monitor communication status
                lifecycleScope.launch {
                    arduinoComms.communicationMode.collect { mode ->
                        withContext(Dispatchers.Main) {
                            Log.d(TAG, "Communication mode changed: $mode")
                            _robotState.value = _robotState.value.copy(communicationMode = mode)
                            when (mode) {
                                CommunicationMode.USB_SERIAL -> logComm("COMM", "USB Serial CONNECTED")
                                CommunicationMode.DISCONNECTED -> logComm("COMM", "Communication DISCONNECTED")
                            }
                        }
                    }
                }
            } catch (e: Exception) {
                Log.e(TAG, "Error initializing Arduino communications", e)
            }
            
            // HotwordService MUST start before EnvironmentMonitoringService.
            // Both use the microphone — Samsung S9 only allows one SpeechRecognizer
            // at a time. Starting EnvironmentMonitoring first causes ERROR_RECOGNIZER_BUSY
            // which makes "Hey Buddy" silently fail.
            try {
                startHotwordService()
                Log.d(TAG, "initializeApp: HotwordService started")
            } catch (e: Exception) {
                Log.e(TAG, "Error starting HotwordService", e)
            }

            // Delay EnvironmentMonitoringService 4 s so HotwordService has time
            // to acquire the SpeechRecognizer before any mic contention.
            lifecycleScope.launch {
                kotlinx.coroutines.delay(4000)
                try {
                    startEnvironmentMonitoring()
                    Log.d(TAG, "initializeApp: Environment monitoring started (delayed)")
                } catch (e: Exception) {
                    Log.e(TAG, "Error starting environment monitoring", e)
                }
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

    /**
     * V37: parsing lives in MessageRouter (single source of truth).
     * This delegator is kept only for Activity-side side effects:
     * banners, TTS triggers, phone-call escalation, security gatekeeper.
     */
    private fun handleArduinoMessage(msg: String) {
        logComm("ARD", msg)
        try {
            when {
                msg.startsWith("EVENT:")           -> handleEvent(msg.substring(6))
                msg.startsWith("ALERT:")           -> handleEvent(msg.substring(6))
                msg.startsWith("GESTURE:")         -> handleGesture(msg)
                msg.startsWith("REQ_MODE_CHANGE:") -> securityGatekeeper.processCommand(msg)
                msg.startsWith("ESTOP|") -> {
                    logComm("ESTOP", msg.substring(6))
                    _robotState.value = _robotState.value.copy(isEmergency = true)
                }
            }
        } catch (e: Exception) {
            Log.e(TAG, "handleArduinoMessage error on: $msg", e)
        }
    }
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

    // V37 STATUS| packet from sendTelemetryToPico()
    // Format: STATUS|ESTOP:YES|AUTO:ON|BAT:12.4|PCT:80|...|S9:OK|FW:V37.0|...
    private fun parseStatusPipe(msg: String) {
        try {
            val isEstop = msg.contains("ESTOP:YES")
            val isAuto  = msg.contains("AUTO:ON")
            val s9Ok    = msg.contains("S9:OK")
            val fwIdx   = msg.indexOf("FW:")
            val fw      = if (fwIdx >= 0) msg.substring(fwIdx + 3).substringBefore("|") else "--"

            _robotState.value = _robotState.value.copy(isAutoMode = isAuto)

            if (isEstop) {
                arduinoComms.sendCommand("MOTOR:S")
                handleEvent("OBSTACLE", 8000)
            }
        } catch (e: Exception) { Log.e(TAG, "parseStatusPipe error: ${e.message}") }
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
        if (isProcessingCommand) {
            Log.d(TAG, "Hotword ignored — already processing a command")
            resumeHotwordService()
            return
        }
        if (_robotState.value.isSpeaking) {
            Log.d(TAG, "Hotword ignored — robot is speaking")
            resumeHotwordService()
            return
        }
        Log.d(TAG, "Hotword triggered — capturing command")
        pauseHotwordService()
        isProcessingCommand = true
        isListeningForWakeWord = false
        speakText("Yeah?")
        Handler(Looper.getMainLooper()).postDelayed({ startCommandListening() }, 1500)
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
                    // Re-check after the delay — a user command may have started speaking
                    // during the wait, and we must not talk over it.
                    if (!isProcessingCommand && !_robotState.value.isSpeaking) {
                        speakText(getAIResponse("Tell me a random short unhinged joke or comment."))
                    }
                }
                delay(5000)
            }
        }
    }

    private fun setupComposeUI() {
        findViewById<androidx.compose.ui.platform.ComposeView>(R.id.composeView).setContent {
            val state by robotState.collectAsState()
            val telemetryData by telemetry.collectAsState()
            val isElevenLabsSpeaking by _isElevenLabsSpeaking.collectAsState()
            val lipSyncAmplitude by _lipSyncAmplitude.collectAsState()
            val commLogs by remember { derivedStateOf { _commLogs.toList() } }
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

                    // ── ElevenLabs lipsync mouth overlay (bottom-centre, landscape) ──
                    if (isElevenLabsSpeaking && !state.isSplashScreen && !state.showIntroDialog && !showSettings) {
                        LipSyncMouthOverlay(
                            amplitude = lipSyncAmplitude,
                            mode = state.currentMode,
                            modifier = Modifier
                                .align(Alignment.BottomCenter)
                                .padding(bottom = 56.dp)
                        )
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
                            logs = commLogs,
                            onClose = { showSettings = false },
                            onModeChange = { setRobotMode(it) },
                            onMotorCommand = { arduinoComms.sendCommand(it) },
                            onConnectRobotWifi = { ssid, password ->
                                try {
                                    connectRobotToWifi(ssid, password)
                                } catch (e: Exception) {
                                    Log.e(TAG, "connectRobotToWifi crashed", e)
                                    logComm("WIFI", "Error: ${e.message}")
                                    Toast.makeText(this@MainActivity, "WiFi connect failed: ${e.message}", Toast.LENGTH_LONG).show()
                                    false
                                }
                            },
                            onSaveWifiPassword = { ssid, password -> saveWifiPassword(ssid, password) },
                            onNetworkPreferenceChange = { applyNetworkPreference(it) },
                            onToggleCommunication = {
                                val currentMode = _robotState.value.communicationMode
                                val newMode = when (currentMode) {
                                    CommunicationMode.USB_SERIAL  -> CommunicationMode.DISCONNECTED
                                    CommunicationMode.DISCONNECTED -> CommunicationMode.USB_SERIAL
                                }
                                _robotState.value = _robotState.value.copy(communicationMode = newMode)
                                logComm("COMM", "Toggling: $currentMode -> $newMode")
                                when (newMode) {
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
                val objectResults = mlKitObjects.mapNotNull { obj: com.google.mlkit.vision.objects.DetectedObject ->
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
                        arduinoComms.sendCommand("OBJ:$safeLabel," + String.format("%.2f", conf))
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
            // at ~16 writes/sec, starving Mega serial command handling.
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
                Log.w(TAG, "SpeechRecognizer error=$error isProcessingCommand=$isProcessingCommand")
                _robotState.value = _robotState.value.copy(isListening = false)
                // Reset command-capture state so the robot doesn't go deaf after a
                // mic error mid-command (e.g. ERROR_RECOGNIZER_BUSY on Samsung S9).
                isProcessingCommand = false
                isListeningForWakeWord = false
                silenceHandler.removeCallbacksAndMessages(null)
                Handler(Looper.getMainLooper()).postDelayed({ returnToWakeWordListening() }, 1000)
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

    private fun returnToWakeWordListening() {
        startContinuousListening()
        resumeHotwordService()
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
                isProcessingCommand = false
                returnToWakeWordListening()
                speakText("Stopping follow mode.")
                return
            }
            if ("patrol" in lowerText) {
                isProcessingCommand = false
                returnToWakeWordListening()
                startPatrol()
                return
            }
        }

        if (isListeningForWakeWord && !isProcessingCommand) {
            if (BuddyBotConfig.matchesWakeWord(lowerText)) {
                if (_robotState.value.currentMode == RobotMode.BODYGUARD && _robotState.value.recognizedPerson != BuddyBotConfig.PRIORITY_USER) {
                    speakText("I only respond to ${BuddyBotConfig.PRIORITY_USER}")
                    returnToWakeWordListening()
                    return
                }
                isProcessingCommand = true; isListeningForWakeWord = false
                speakText("Yeah?"); Handler(Looper.getMainLooper()).postDelayed(
                    { startCommandListening() },
                    1500
                )
            } else returnToWakeWordListening()
        } else if (isProcessingCommand) {
            // Final result from recognizer — we already have the complete utterance.
            // Skip checkForSilence() polling (which adds a 2s dead wait) and go direct.
            silenceHandler.removeCallbacksAndMessages(null)
            if (text.isNotBlank()) {
                currentSpeechText = text
                processCommand(text)
            } else {
                isProcessingCommand = false
                returnToWakeWordListening()
            }
        }
    }

    private fun startCommandListening() {
        currentSpeechText = ""
        lastSpeechTime = System.currentTimeMillis()
        silenceHandler.removeCallbacksAndMessages(null)
        speechRecognizer?.cancel()
        val intent = Intent(RecognizerIntent.ACTION_RECOGNIZE_SPEECH).apply {
            putExtra(RecognizerIntent.EXTRA_LANGUAGE_MODEL, RecognizerIntent.LANGUAGE_MODEL_FREE_FORM)
            putExtra(RecognizerIntent.EXTRA_LANGUAGE, "en-AU")          // S9 locale
            putExtra(RecognizerIntent.EXTRA_PARTIAL_RESULTS, true)
            putExtra(RecognizerIntent.EXTRA_MAX_RESULTS, 3)
            // Give AJ plenty of time to form a sentence — 3-year-old speech is slower
            putExtra(RecognizerIntent.EXTRA_SPEECH_INPUT_COMPLETE_SILENCE_LENGTH_MILLIS, 2000L)
            putExtra(RecognizerIntent.EXTRA_SPEECH_INPUT_POSSIBLY_COMPLETE_SILENCE_LENGTH_MILLIS, 2000L)
            putExtra(RecognizerIntent.EXTRA_SPEECH_INPUT_MINIMUM_LENGTH_MILLIS, 300L)
        }
        speechRecognizer?.startListening(intent)
        Log.d(TAG, "Command listening started")
    }

    private fun checkForSilence() {
        silenceHandler.postDelayed({
            if (System.currentTimeMillis() - lastSpeechTime >= BuddyBotConfig.SILENCE_THRESHOLD_MS) {
                if (currentSpeechText.isNotEmpty()) processCommand(currentSpeechText)
                else {
                    isProcessingCommand = false; returnToWakeWordListening()
                }
            } else checkForSilence()
        }, 500)
    }

    private fun processCommand(command: String) {
        _robotState.value = _robotState.value.copy(isProcessing = true)
        lifecycleScope.launch {
            try {
                // Map spoken commands to Mega V37 motor/control commands.
                // Falls through to AI for anything conversational.
                val spokenLower = command.lowercase()
                when {
                    "forward" in spokenLower ->
                        { arduinoComms.sendCommand("MOTOR:F"); speakTextSuspend("Moving forward") }
                    "backward" in spokenLower || "back" in spokenLower ->
                        { arduinoComms.sendCommand("MOTOR:B"); speakTextSuspend("Moving backward") }
                    "turn left" in spokenLower || (spokenLower == "left") ->
                        { arduinoComms.sendCommand("MOTOR:L"); speakTextSuspend("Turning left") }
                    "turn right" in spokenLower || (spokenLower == "right") ->
                        { arduinoComms.sendCommand("MOTOR:R"); speakTextSuspend("Turning right") }
                    "stop" in spokenLower ->
                        { arduinoComms.sendCommand("MOTOR:S"); speakTextSuspend("Stopping") }
                    "dance" in spokenLower || "spin" in spokenLower || "turn around" in spokenLower ->
                        { arduinoComms.sendCommand("MOTOR:DANCE"); speakTextSuspend("Let's dance!") }
                    "auto" in spokenLower && ("off" in spokenLower || "stop" in spokenLower) ->
                        { arduinoComms.sendCommand("AUTO:OFF"); speakTextSuspend("Autonomous mode off") }
                    "auto" in spokenLower ->
                        { arduinoComms.sendCommand("AUTO:ON"); speakTextSuspend("Autonomous mode on") }
                    "speed up" in spokenLower || "faster" in spokenLower ->
                        { arduinoComms.sendCommand("FAST"); speakTextSuspend("Speeding up") }
                    "slow down" in spokenLower || "slower" in spokenLower ->
                        { arduinoComms.sendCommand("SLOW"); speakTextSuspend("Slowing down") }
                    else -> {
                        // AI response — log which provider is active
                        Log.d(TAG, "AI query: \"$command\"")
                        val response = getAIResponse(command)
                        Log.d(TAG, "AI response: \"$response\"")
                        // Await speech — HotwordService resumes only AFTER robot finishes speaking
                        speakTextSuspend(response)
                    }
                }
            } catch (e: Exception) {
                Log.e(TAG, "processCommand error: ${e.message}", e)
            } finally {
                _robotState.value = _robotState.value.copy(isProcessing = false)
                isProcessingCommand = false
                currentSpeechText = ""
                // 500ms gap after speech before resuming wake-word listening
                delay(500)
                withContext(Dispatchers.Main) { returnToWakeWordListening() }
            }
        }
    }

    /** V37: AI fallback chain delegated to AIRouter (single source of truth). */
    private suspend fun getAIResponse(userInput: String): String {
        val response = aiRouter.getResponse(userInput)
        withContext(Dispatchers.Main) {
            _robotState.value = _robotState.value.copy(
                aiService = when {
                    BuddyBotConfig.isGroqConfigured   -> AIService.GROQ
                    BuddyBotConfig.isGeminiConfigured -> AIService.GEMINI
                    BuddyBotConfig.isClaudeConfigured -> AIService.CLAUDE
                    else                              -> AIService.OFFLINE
                }
            )
        }
        return response
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
        "spinning around", "spinning the other way",
        "connecting to wi-fi", "connecting to wifi"
    )

    private fun isOperationalPhrase(text: String): Boolean {
        val lower = text.lowercase()
        return operationalPhrases.any { phrase -> lower.contains(phrase) }
    }

    private fun speakText(text: String) {
        lifecycleScope.launch { speakTextSuspend(text) }
    }

    /** Suspend version — use this inside coroutines that need to AWAIT speech completion.
     *  This ensures HotwordService is not resumed mid-sentence after AI responses. */
    private suspend fun speakTextSuspend(text: String) {
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

    /**
     * Downloads ElevenLabs TTS MP3 and plays it directly via MediaPlayer.
     *
     * Why MediaPlayer instead of WebView audio:
     *   WebView blocks file:// audio on Android 9+ due to CORS — audio silently
     *   never plays, onAudioEnded never fires, and isSpeaking hangs forever.
     *   MediaPlayer has no such restriction and routes to the device speaker correctly.
     *
     * Lip sync: a coroutine polls MediaPlayer.getCurrentPosition() while playing
     * and calls faceCoordinator.setAmplitude() to animate the mouth overlay.
     * For richer amplitude we use android.media.audiofx.Visualizer if available.
     */
    private suspend fun synthesizeWithElevenLabs(text: String) = withContext(Dispatchers.IO) {
        val requestBody = JSONObject().apply {
            put("text", text)
            put("model_id", "eleven_monolingual_v1")
            put("voice_settings", JSONObject().apply {
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
        if (!response.isSuccessful) throw Exception("ElevenLabs HTTP ${response.code}")

        val audioFile = File(cacheDir, "speech_${System.currentTimeMillis()}.mp3")
        try {
            response.body?.bytes()?.let { FileOutputStream(audioFile).use { fos -> fos.write(it) } }
                ?: throw Exception("ElevenLabs response body was null")
        } catch (e: Exception) {
            audioFile.delete()   // clean up any partially-written file before re-throwing
            throw e
        }

        // ── Play via MediaPlayer and wait for completion ─────────────────
        val completion = kotlinx.coroutines.CompletableDeferred<Unit>()

        withContext(Dispatchers.Main) {
            try {
                val mp = MediaPlayer()
                mp.setAudioAttributes(
                    android.media.AudioAttributes.Builder()
                        .setUsage(android.media.AudioAttributes.USAGE_MEDIA)
                        .setContentType(android.media.AudioAttributes.CONTENT_TYPE_SPEECH)
                        .build()
                )
                mp.setDataSource(audioFile.absolutePath)

                mp.setOnPreparedListener { player ->
                    player.start()
                    Log.d(TAG, "[TTS] MediaPlayer started — audio session ${player.audioSessionId}")

                    // Tell FaceCoordinator to start the talk video for this mode
                    faceCoordinator.setSpeaking(true)
                    _isElevenLabsSpeaking.value = true

                    // ── Amplitude polling for lip sync ────────────────────
                    // Poll every 50 ms, synthesise amplitude from a sine envelope
                    // timed to speech rhythm. Replace with Visualizer if you want
                    // true PCM amplitude (requires RECORD_AUDIO at runtime).
                    val totalMs = player.duration.toLong().coerceAtLeast(500L)
                    lifecycleScope.launch {
                        var phase = 0.0
                        while (player.isPlaying) {
                            phase += 0.35
                            val amp = (0.15f +
                                kotlin.math.abs(kotlin.math.sin(phase)).toFloat() * 0.55f +
                                kotlin.math.abs(kotlin.math.sin(phase * 2.3)).toFloat() * 0.20f)
                                .coerceIn(0f, 1f)
                            faceCoordinator.setAmplitude(amp)
                            _lipSyncAmplitude.value = amp
                            delay(50)
                        }
                        faceCoordinator.setAmplitude(0f)
                        _lipSyncAmplitude.value = 0f
                    }
                }

                mp.setOnCompletionListener { player ->
                    Log.d(TAG, "[TTS] MediaPlayer completed")
                    faceCoordinator.setSpeaking(false)
                    faceCoordinator.setAmplitude(0f)
                    _lipSyncAmplitude.value = 0f
                    _isElevenLabsSpeaking.value = false
                    try { player.release() } catch (_: Exception) {}
                    audioFile.delete()
                    completion.complete(Unit)
                }

                mp.setOnErrorListener { player, what, extra ->
                    Log.e(TAG, "[TTS] MediaPlayer error: what=$what extra=$extra")
                    faceCoordinator.setSpeaking(false)
                    faceCoordinator.setAmplitude(0f)
                    _lipSyncAmplitude.value = 0f
                    _isElevenLabsSpeaking.value = false
                    try { player.release() } catch (_: Exception) {}
                    audioFile.delete()
                    completion.complete(Unit)   // unblock speakText()
                    true
                }

                mp.prepareAsync()

            } catch (e: Exception) {
                Log.e(TAG, "[TTS] MediaPlayer setup failed: ${e.message}")
                faceCoordinator.setSpeaking(false)
                audioFile.delete()
                completion.complete(Unit)
            }
        }

        // 30-second hard timeout — if MediaPlayer never fires onCompletion/onError
        // (corrupted MP3, audio focus stolen, speaker disabled), this unblocks
        // speakText() so the robot doesn't hang forever with isSpeaking=true.
        if (withTimeoutOrNull(30_000L) { completion.await() } == null) {
            Log.e(TAG, "[TTS] MediaPlayer timed out after 30 s — forcing speech completion")
            faceCoordinator.setSpeaking(false)
            faceCoordinator.setAmplitude(0f)
            _lipSyncAmplitude.value = 0f
            _isElevenLabsSpeaking.value = false
            audioFile.delete()
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
    // ─── GuardianEngine parent notification ─────────────────────────────────────
    /**
     * Fires when GuardianEngine detects aggression above threshold.
     * Sends a local high-priority notification and forwards to BuddyBotMessagingService
     * for FCM push to the companion Parent App.
     */
    private fun sendGuardianAlert(event: GuardianEvent) {
        Log.i(TAG, "Guardian alert: conf=" + event.confidence + " text=" + event.recognizedText)

        // Local notification — visible even if Parent App is backgrounded
        val nm = getSystemService(NotificationManager::class.java)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val ch = NotificationChannel("guardian_alerts", "Guardian Alerts",
                NotificationManager.IMPORTANCE_HIGH)
            nm?.createNotificationChannel(ch)
        }
        val pct = (event.confidence * 100).toInt()
        val notif = NotificationCompat.Builder(this, "guardian_alerts")
            .setSmallIcon(R.drawable.ic_launcher_foreground)
            .setContentTitle("BuddyBot Guardian Alert")
            .setContentText("Concerning behaviour detected ($pct% confidence)")
            .setStyle(NotificationCompat.BigTextStyle().bigText(
                "Heard: \"" + event.recognizedText + "\"\n" +
                "Confidence: $pct%\n" +
                "Audio level: " + (event.audioRms * 100).toInt() + "%"
            ))
            .setPriority(NotificationCompat.PRIORITY_HIGH)
            .setAutoCancel(true)
            .build()
        nm?.notify((System.currentTimeMillis() % Int.MAX_VALUE).toInt(), notif)

        // FCM push to companion Parent App via BuddyBotMessagingService
        try {
            val intent = Intent(this, BuddyBotMessagingService::class.java).apply {
                action = "GUARDIAN_ALERT"
                putExtra("confidence",  event.confidence)
                putExtra("text",        event.recognizedText)
                putExtra("timestamp",   event.timestamp)
                putExtra("audioRms",    event.audioRms)
            }
            startService(intent)
        } catch (e: Exception) {
            Log.e(TAG, "Failed to forward guardian alert to MessagingService", e)
        }
    }
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

    private fun connectRobotToWifi(ssid: String, password: String): Boolean {
        return try {
            val safeSsid = ssid.trim().replace("|", "").replace("\n", "").replace("\r", "")
            val safePass = password.replace("|", "").replace("\n", "").replace("\r", "")
            when {
                safeSsid.isEmpty() -> {
                    logComm("WIFI", "Cannot connect — phone not on WiFi")
                    Toast.makeText(this, "Connect this phone to WiFi first", Toast.LENGTH_SHORT).show()
                    false
                }
                safePass.isEmpty() -> {
                    logComm("WIFI", "Cannot connect — password empty")
                    Toast.makeText(this, "Enter the WiFi password", Toast.LENGTH_SHORT).show()
                    false
                }
                !::arduinoComms.isInitialized -> {
                    logComm("WIFI", "Cannot connect — serial comms not ready")
                    Toast.makeText(this, "Robot communication not ready yet", Toast.LENGTH_SHORT).show()
                    false
                }
                arduinoComms.communicationMode.value != CommunicationMode.USB_SERIAL -> {
                    logComm("WIFI", "Cannot connect — USB serial not connected (mode=${arduinoComms.communicationMode.value})")
                    Toast.makeText(this, "USB serial not connected — plug robot into phone", Toast.LENGTH_LONG).show()
                    false
                }
                else -> {
                    WiFiCredentialsStore.validateMegaCommandLength(safeSsid, safePass)?.let { err ->
                        logComm("WIFI", err)
                        Toast.makeText(this, err, Toast.LENGTH_LONG).show()
                        return false
                    }
                    logComm("WIFI", "Sending credentials for \"$safeSsid\" to robot")
                    _robotState.value = _robotState.value.copy(wifiSetupPhase = "sending")
                    arduinoComms.sendCommand("WIFI|$safeSsid|$safePass")
                    WiFiCredentialsStore.save(this, safeSsid, safePass)
                    Toast.makeText(this, "WiFi credentials sent to robot", Toast.LENGTH_SHORT).show()
                    true
                }
            }
        } catch (e: Exception) {
            Log.e(TAG, "connectRobotToWifi failed", e)
            logComm("WIFI", "Send failed: ${e.message}")
            Toast.makeText(this, "Could not send WiFi credentials: ${e.message}", Toast.LENGTH_LONG).show()
            false
        }
    }

    private fun saveWifiPassword(ssid: String, password: String): Boolean {
        val safeSsid = ssid.trim()
        if (safeSsid.isEmpty() || password.isEmpty()) {
            Toast.makeText(this, "Enter network name and password first", Toast.LENGTH_SHORT).show()
            return false
        }
        val saved = WiFiCredentialsStore.save(this, safeSsid, password)
        if (saved) {
            logComm("WIFI", "Password saved for \"$safeSsid\"")
            Toast.makeText(this, "WiFi password saved", Toast.LENGTH_SHORT).show()
        } else {
            Toast.makeText(this, "Could not save password", Toast.LENGTH_SHORT).show()
        }
        return saved
    }

    private fun logComm(source: String, message: String) {
        val entry = "[${System.currentTimeMillis() % 100000}] $source: $message"
        val append = {
            _commLogs.add(0, entry)
            if (_commLogs.size > 100) _commLogs.removeAt(100)
        }
        if (Looper.myLooper() == Looper.getMainLooper()) append()
        else runOnUiThread(append)
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
    }

    private fun startEnvironmentMonitoring() {
        startService(Intent(this, EnvironmentMonitoringService::class.java))
    }

    private fun pauseHotwordService() {
        val intent = Intent(this, HotwordService::class.java).apply {
            action = HotwordService.ACTION_PAUSE_LISTENING
        }
        startService(intent)
    }

    private fun resumeHotwordService() {
        val intent = Intent(this, HotwordService::class.java).apply {
            action = HotwordService.ACTION_RESUME_LISTENING
        }
        startService(intent)
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
        // Cancel alive-behavior coroutine to prevent leaks/crashes after destroy
        aliveBehaviorJob?.cancel()
        // Drain all pending silence-detection callbacks so they can't fire on a dead activity
        silenceHandler.removeCallbacksAndMessages(null)
        // Phase 4: dismiss Call Daddy overlay so it doesn't leak after activity is destroyed
        callOverlayManager?.dismiss()
        callOverlayManager = null
        super.onDestroy()
        releaseResources()
        faceCoordinator.release()
    }
}
