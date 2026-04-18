package com.buddybot.kids

import android.annotation.SuppressLint
import android.app.PendingIntent
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.content.pm.PackageManager
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
import android.view.Surface
import android.view.View
import android.view.WindowManager
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

    private val httpClient = OkHttpClient.Builder()
        .connectTimeout(30, java.util.concurrent.TimeUnit.SECONDS)
        .build()

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

    private val permissionLauncher = registerForActivityResult(
        ActivityResultContracts.RequestMultiplePermissions()
    ) { permissions ->
        if (permissions.all { it.value }) initializeApp() else finish()
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
        window.attributes.layoutInDisplayCutoutMode =
            WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES
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
        faceCoordinator = FaceCoordinator(this, playerView, lifecycleScope)

        val filter = IntentFilter(ACTION_USB_PERMISSION).also {
            // Phase 4: listen for hot-plug attach/detach so webcam auto-connects
            it.addAction(UsbManager.ACTION_USB_DEVICE_ATTACHED)
            it.addAction(UsbManager.ACTION_USB_DEVICE_DETACHED)
        }
        ContextCompat.registerReceiver(this, usbReceiver, filter, ContextCompat.RECEIVER_EXPORTED)

        setupComposeUI()
        checkAndRequestPermissions()
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
            } else if (msg.startsWith("REQ_MODE:")) {
                val parts = msg.split(":")
                if (parts.size >= 2) {
                    val modeStr = parts[1]
                    RobotMode.values().find { it.name == modeStr }?.let {
                        _robotState.value = _robotState.value.copy(showPinEntry = true, requestedMode = it)
                    }
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

    private fun startSequence(playIntro: Boolean) {
        _robotState.value = _robotState.value.copy(showIntroDialog = false)
        if (playIntro) {
            faceCoordinator.playVideoOnce("intro") {
                playSplashThenMain()
            }
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
                                            logComm("COMM", "Connecting WebSocket to $ip")
                                            arduinoComms.initializeWebSocket(ip)
                                        } else {
                                            logComm("COMM", "No IP configured for WebSocket")
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
                                    logComm("TEST", "Testing WebSocket to $ip...")
                                    arduinoComms.initializeWebSocket(ip)
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
                        usbCameraClient?.openCamera(offscreenTexture)
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
                        detectedFaces = faceResults,
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
        isListeningForWakeWord = true
        val intent = Intent(RecognizerIntent.ACTION_RECOGNIZE_SPEECH).apply {
            putExtra(RecognizerIntent.EXTRA_LANGUAGE_MODEL, RecognizerIntent.LANGUAGE_MODEL_FREE_FORM)
            putExtra(RecognizerIntent.EXTRA_PARTIAL_RESULTS, true)
        }
        speechRecognizer?.startListening(intent)
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
        try {
            val response = callClaudeAPI(userInput)
            if (response.isNotEmpty()) {
                val limited = limitWords(response, 15)
                Log.d(TAG, "[AI] Claude response (${response.split(" ").size} words) → limited to (${limited.split(" ").size} words): $limited")
                return@withContext limited
            }
        } catch (e: Exception) {
            Log.e(TAG, "Claude failed", e)
        }
        try {
            val response = callGeminiAPI(userInput)
            if (response.isNotEmpty()) {
                val limited = limitWords(response, 15)
                Log.d(TAG, "[AI] Gemini response (${response.split(" ").size} words) → limited to (${limited.split(" ").size} words): $limited")
                return@withContext limited
            }
        } catch (e: Exception) {
            Log.e(TAG, "Gemini failed", e)
        }
        "Offline mode."
    }

    private fun limitWords(text: String, maxWords: Int): String {
        val words = text.trim().split(Regex("\\s+"))
        return if (words.size > maxWords) {
            words.take(maxWords).joinToString(" ")
        } else {
            text
        }
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
            .addHeader("x-api-key", BuddyBotConfig.ANTHROPIC_API_KEY)
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
            Request.Builder().url("${BuddyBotConfig.GEMINI_URL}?key=${BuddyBotConfig.GEMINI_API_KEY}")
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
                    put("stability", 0.5f); put("similarity_boost", 0.75); put(
                    "style",
                    0.5f
                ); put("use_speaker_boost", true)
                })
        }
        val request = Request.Builder()
            .url("https://api.elevenlabs.io/v1/text-to-speech/${BuddyBotConfig.ELEVENLABS_VOICE_ID}")
            .addHeader("Accept", "audio/mpeg")
            .addHeader("xi-api-key", BuddyBotConfig.ELEVENLABS_API_KEY)
            .post(requestBody.toString().toRequestBody("application/json".toMediaType())).build()
        val response = httpClient.newCall(request).execute()
        if (response.isSuccessful) {
            // Phase 3 fix: unique filename prevents concurrent call corruption
            val audioFile = File(cacheDir, "speech_${System.currentTimeMillis()}.mp3")
            response.body?.bytes()?.let { bytes ->
                FileOutputStream(audioFile).use { fos ->
                    fos.write(bytes)
                }
            }
            withContext(Dispatchers.Main) { playAudioFile(audioFile) }
        } else throw Exception("ElevenLabs failed")
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
     * Phase 4: Call Daddy button handler.
     * Strategy (in order):
     *   1. fb-messenger://videocall/<ID>  — direct video call deep link
     *   2. fb-messenger://user/<ID>       — open Messenger chat (user can tap video)
     *   3. intent to com.facebook.orca   — open Messenger app
     *   4. Phone dialer fallback
     *
     * Camera status is logged before the call so the user knows if the webcam
     * feed is available. The webcam feed is handled by the UVC driver and
     * Messenger uses the system camera API — on most Android tablets the USB
     * webcam is exposed as a camera source that Messenger can select.
     */
    private fun callDaddy() {
        val cameraOk = _robotState.value.isCameraConnected
        logComm("CALL", "Call Daddy pressed. Camera: ${if (cameraOk) "CONNECTED" else "NOT CONNECTED"}")

        if (!cameraOk) {
            logComm("CALL", "Warning: USB webcam not connected — call will use device camera if available")
            speakText("Calling Daddy! Camera might not work.")
        } else {
            speakText("Hi Daddy!")
        }

        // Strategy 1: Direct Messenger video call deep link
        val messengerId = BuildConfig.DADDY_MESSENGER_ID
        val strategies = listOf(
            // Direct video call (works when Messenger is installed and ID is correct)
            Intent(Intent.ACTION_VIEW).apply {
                data = "fb-messenger://videocall/$messengerId".toUri()
                setPackage("com.facebook.orca")
                flags = Intent.FLAG_ACTIVITY_NEW_TASK
            },
            // Open Messenger chat thread (user taps video icon)
            Intent(Intent.ACTION_VIEW).apply {
                data = "fb-messenger://user/$messengerId".toUri()
                setPackage("com.facebook.orca")
                flags = Intent.FLAG_ACTIVITY_NEW_TASK
            },
            // Open Messenger app (home screen)
            packageManager.getLaunchIntentForPackage("com.facebook.orca")?.apply {
                flags = Intent.FLAG_ACTIVITY_NEW_TASK
            },
            // Phone dialer fallback
            Intent(Intent.ACTION_DIAL).apply {
                data = "tel:${BuildConfig.DADDY_PHONE_NUMBER}".toUri()
                flags = Intent.FLAG_ACTIVITY_NEW_TASK
            }
        )

        var launched = false
        for ((index, intent) in strategies.withIndex()) {
            if (intent == null) continue
            try {
                startActivity(intent)
                val strategyName = when (index) {
                    0 -> "Messenger video call deep link"
                    1 -> "Messenger chat deep link"
                    2 -> "Messenger app launch"
                    else -> "Phone dialer"
                }
                logComm("CALL", "Success: $strategyName")
                Log.i(TAG, "callDaddy: launched via $strategyName")
                launched = true
                break
            } catch (e: Exception) {
                Log.w(TAG, "callDaddy strategy $index failed: ${e.message}")
            }
        }

        if (!launched) {
            logComm("ERROR", "All call strategies failed — is Messenger installed?")
            speakText("Sorry, I could not call Daddy. Please check Messenger is installed.")
        }
    }

    private fun updateIP(ip: String) {
        _robotState.value = _robotState.value.copy(buddybotIP = ip); getSharedPreferences(
            "BuddyBot",
            MODE_PRIVATE
        ).edit { putString("buddybotIP", ip) }
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
        super.onDestroy(); releaseResources(); faceCoordinator.release()
    }
}
