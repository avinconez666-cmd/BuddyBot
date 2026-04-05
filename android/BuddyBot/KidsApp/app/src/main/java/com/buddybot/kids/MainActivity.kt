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
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp
import androidx.core.content.ContextCompat
import androidx.core.content.IntentCompat
import androidx.core.content.edit
import androidx.core.net.toUri
import androidx.lifecycle.lifecycleScope
import com.buddybot.kids.ml.FaceRecognitionManager
import com.buddybot.kids.ml.ObjectDetectionManager
import com.buddybot.kids.services.EnvironmentMonitoringService
import com.google.mlkit.vision.common.InputImage
import com.jiangdg.ausbc.CameraClient
import com.jiangdg.ausbc.callback.ICaptureCallBack
import com.jiangdg.ausbc.callback.IPreviewDataCallBack
import com.jiangdg.ausbc.camera.CameraUvcStrategy
import com.jiangdg.ausbc.camera.bean.CameraRequest
import kotlinx.coroutines.*
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
            if (ACTION_USB_PERMISSION == intent.action) {
                synchronized(this) {
                    val device: UsbDevice? = IntentCompat.getParcelableExtra(
                        intent,
                        UsbManager.EXTRA_DEVICE,
                        UsbDevice::class.java
                    )
                    if (intent.getBooleanExtra(UsbManager.EXTRA_PERMISSION_GRANTED, false)) {
                        device?.let { openWebcam(it) }
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

        val filter = IntentFilter(ACTION_USB_PERMISSION)
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
            } catch (e: Exception) {
                Log.e(TAG, "Error initializing Arduino communications", e)
            }
            
            try {
                startEnvironmentMonitoring()
                Log.d(TAG, "initializeApp: Environment monitoring started")
            } catch (e: Exception) {
                Log.e(TAG, "Error starting environment monitoring", e)
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
                val data = msg.substring(5).split(",")
                if (data.size >= 4) {
                    val batteryPercent = data[1].toIntOrNull() ?: 0
                    _telemetry.value = _telemetry.value.copy(
                        batteryVoltage = data[0].toFloatOrNull() ?: 0f,
                        batteryPercent = batteryPercent,
                        isMoving = data[3] == "1"
                    )
                    
                    if (batteryPercent < 15 && batteryPercent > 0) {
                        speakText("My battery is getting very low. Please plug me in soon!")
                    }
                }
            } else if (msg.startsWith("EVENT:")) {
                val event = msg.substring(6)
                if (event == "OBSTACLE") playAudioCommand("OBSTACLE")
                if (event == "TILT") playAudioCommand("TILT")
            } else if (msg.startsWith("US:")) {
                parseUltrasonicData(msg)
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

            if (!_robotState.value.isSpeaking && !isMlProcessing.get()) {
                if (f < PROXIMITY_THRESHOLD && f != -1) arduinoComms.sendCommand("HEAD:LOOK_DOWN")
                if (l < PROXIMITY_THRESHOLD && l != -1) arduinoComms.sendCommand("HEAD:LOOK_LEFT")
                if (ri < PROXIMITY_THRESHOLD && ri != -1) arduinoComms.sendCommand("HEAD:LOOK_RIGHT")
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
        lifecycleScope.launch {
            while (isActive) {
                if (_robotState.value.currentMode == RobotMode.NORMAL && !isProcessingCommand && !_robotState.value.isSpeaking) {
                    delay((10000..30000).random().toLong())
                    if (_robotState.value.currentMode == RobotMode.NORMAL) {
                        arduinoComms.sendCommand("HEAD:RANDOM")
                    }
                }
                
                if (_robotState.value.currentMode == RobotMode.UNHINGED && !isProcessingCommand && !_robotState.value.isSpeaking) {
                    delay((5000..15000).random().toLong())
                    if (Math.random() > 0.7) {
                        speakText(getAIResponse("Tell me a random short unhinged joke or comment."))
                    } else {
                        arduinoComms.sendCommand("HEAD:JITTER")
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
                            onToggleCommunication = { /*TODO*/ },
                            webcamClient = usbCameraClient
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
        // Find webcam by Product ID first, then fallback to Vendor ID
        val webcam = deviceList.values.find { it.productId == WEBCAM_PRODUCT_ID } 
            ?: deviceList.values.find { it.vendorId == WEBCAM_VENDOR_ID }

        if (webcam != null) {
            if (usbManager.hasPermission(webcam)) {
                openWebcam(webcam)
            } else {
                val permissionIntent = PendingIntent.getBroadcast(
                    this,
                    0,
                    Intent(ACTION_USB_PERMISSION),
                    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) PendingIntent.FLAG_MUTABLE else 0
                )
                usbManager.requestPermission(webcam, permissionIntent)
            }
        } else {
            logComm("CAMERA", "External Webcam (C270) not found!")
        }
    }

    private fun openWebcam(device: UsbDevice) {
        logComm("CAMERA", "USB webcam detected - connecting via ML Kit pipeline")
        usbCameraClient = CameraClient.newBuilder(this)
            .setEnableGLES(true)
            .setRawImage(true)
            .setCameraStrategy(CameraUvcStrategy(this))
            .setCameraRequest(CameraRequest.Builder()
                .setPreviewWidth(640)
                .setPreviewHeight(480)
                .create())
            .build()

        usbCameraClient?.addPreviewDataCallBack(object : IPreviewDataCallBack {
            override fun onPreviewData(data: ByteArray?, width: Int, height: Int, format: IPreviewDataCallBack.DataFormat) {
                data?.let { processUVCFrame(it) }
            }
        })
        usbCameraClient?.openCamera(null)
    }

    private fun processUVCFrame(frameData: ByteArray) {
        if (isMlProcessing.get()) return
        isMlProcessing.set(true)

        val image = InputImage.fromByteArray(frameData, 640, 480, 0, InputImage.IMAGE_FORMAT_NV21)

        lifecycleScope.launch(Dispatchers.Default) {
            try {
                val faceResults = faceRecognitionManager.detectAndRecognizeFaces(image)
                faceResults.forEach { result ->
                    if (_robotState.value.currentMode != RobotMode.BODYGUARD) {
                        arduinoComms.sendCommand("FACE:${result.bounds.centerX()},${result.bounds.centerY()}")
                    }
                    
                    if (result.name != null) {
                        withContext(Dispatchers.Main) { onFaceRecognized(result.name) }
                    } else if (_robotState.value.currentMode == RobotMode.BODYGUARD) {
                        // Triggers bodyguard logic via parseUltrasonicData based on proximity
                    }
                }

                val objects = objectDetectionManager.detectObjects(image)
                objects.forEach { obj ->
                    arduinoComms.sendCommand("OBJ:${obj.labels.firstOrNull()?.text ?: "Unknown"},${obj.labels.firstOrNull()?.confidence ?: 0f}")
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
    }

    override fun onPause() {
        super.onPause()
        sensorManager.unregisterListener(this)
    }

    override fun onSensorChanged(event: SensorEvent?) {
        if (event?.sensor?.type == Sensor.TYPE_ROTATION_VECTOR) {
            val rotationMatrix = FloatArray(9)
            SensorManager.getRotationMatrixFromVector(rotationMatrix, event.values)
            val orientation = FloatArray(3)
            SensorManager.getOrientation(rotationMatrix, orientation)
            arduinoComms.sendCommand(
                "SENS|H:${
                    Math.toDegrees(orientation[0].toDouble()).toInt()
                }|P:${
                    Math.toDegrees(orientation[1].toDouble()).toInt()
                }|R:${Math.toDegrees(orientation[2].toDouble()).toInt()}\n"
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
                when {
                    command.contains("forward", true) -> {
                        arduinoComms.sendCommand("MOTOR|F"); speakText("Moving forward")
                    }

                    command.contains("stop", true) -> {
                        arduinoComms.sendCommand("MOTOR|S"); speakText("Stopping")
                    }

                    command.contains("dance", true) -> {
                        arduinoComms.sendCommand("MOTOR|DANCE"); speakText("Let's dance!")
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
            if (response.isNotEmpty()) return@withContext response
        } catch (e: Exception) {
            Log.e(TAG, "Claude failed", e)
        }
        try {
            val response = callGeminiAPI(userInput)
            if (response.isNotEmpty()) return@withContext response
        } catch (e: Exception) {
            Log.e(TAG, "Gemini failed", e)
        }
        "Offline mode."
    }

    private fun callClaudeAPI(userInput: String): String {
        val requestBody = JSONObject().apply {
            put("model", BuddyBotConfig.CLAUDE_MODEL)
            put("max_tokens", 300)
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
        val requestBody = JSONObject().apply {
            put(
                "contents",
                JSONArray().apply {
                    put(JSONObject().apply {
                        put(
                            "parts",
                            JSONArray().apply { put(JSONObject().apply { put("text", userInput) }) })
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

    private fun speakText(text: String) {
        lifecycleScope.launch {
            _robotState.value = _robotState.value.copy(isSpeaking = true)
            try {
                synthesizeWithElevenLabs(text)
            } catch (e: Exception) {
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
            val audioFile = File(cacheDir, "speech.mp3")
            response.body?.bytes()?.let { bytes ->
                FileOutputStream(audioFile).use { fos ->
                    fos.write(bytes)
                }
            }
            withContext(Dispatchers.Main) { playAudioFile(audioFile) }
        } else throw Exception("ElevenLabs failed")
    }

    private fun playAudioFile(audioFile: File) {
        MediaPlayer().apply { setDataSource(audioFile.absolutePath); prepare(); setOnCompletionListener { release(); audioFile.delete() }; start() }
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

    private fun callDaddy() {
        try {
            startActivity(Intent(Intent.ACTION_DIAL).apply {
                data = "tel:${BuildConfig.DADDY_PHONE_NUMBER}".toUri()
            })
        } catch (ignored: Exception) {
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
        wakeLock?.release(); tts?.shutdown(); speechRecognizer?.destroy(); arduinoComms.close(); cameraExecutor.shutdown(); faceRecognitionManager.close()
    }

    private fun startEnvironmentMonitoring() {
        startService(Intent(this, EnvironmentMonitoringService::class.java))
    }

    override fun onDestroy() {
        super.onDestroy(); releaseResources(); faceCoordinator.release()
    }
}
