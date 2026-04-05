package com.buddybot.kids

import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.media.MediaPlayer
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.os.PowerManager
import android.speech.RecognitionListener
import android.speech.RecognizerIntent
import android.speech.SpeechRecognizer
import android.speech.tts.TextToSpeech
import android.util.Log
import android.view.SurfaceHolder
import android.view.View
import android.view.WindowManager
import android.widget.Toast
import androidx.activity.ComponentActivity
import androidx.activity.compose.BackHandler
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp
import androidx.core.content.ContextCompat
import androidx.lifecycle.lifecycleScope
import com.buddybot.kids.ml.FaceRecognitionManager
import com.felhr.usbserial.UsbSerialDevice
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import okhttp3.OkHttpClient
import okhttp3.WebSocket
import java.io.File
import java.util.Locale
import java.util.concurrent.ExecutorService
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit

class MasterActivity : ComponentActivity(), TextToSpeech.OnInitListener {

    companion object {
        private const val TAG = "BuddyBotMaster"
    }

    private val _robotState = MutableStateFlow(RobotState())
    private val _telemetry = MutableStateFlow(TelemetryData())
    private val _commLogs = mutableStateListOf<String>()

    private var surfaceHolder: SurfaceHolder? = null
    private var mediaPlayer: MediaPlayer? = null
    private var tts: TextToSpeech? = null
    private var speechRecognizer: SpeechRecognizer? = null
    private var usbSerial: UsbSerialDevice? = null
    private var webSocket: WebSocket? = null
    private val httpClient = OkHttpClient.Builder().build()
    private var wakeLock: PowerManager.WakeLock? = null
    private var isListeningForWakeWord = false
    private var isProcessingCommand = false
    private var lastSpeechTime = 0L
    private val silenceHandler = Handler(Looper.getMainLooper())
    private var currentSpeechText = ""
    private var currentVideoState = "splash"

    private val permissionLauncher = registerForActivityResult(
        ActivityResultContracts.RequestMultiplePermissions()
    ) { permissions ->
        if (permissions.all { it.value }) {
            initializeApp()
        } else {
            Toast.makeText(this, "Required permissions not granted", Toast.LENGTH_LONG).show()
            finish()
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setupFullscreen()
        acquireWakeLock()
        setContentView(R.layout.activity_master)
        setupComposeUI()
        checkAndRequestPermissions()
    }

    private fun setupFullscreen() {
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        window.setFlags(WindowManager.LayoutParams.FLAG_LAYOUT_NO_LIMITS, WindowManager.LayoutParams.FLAG_LAYOUT_NO_LIMITS)
    }

    private fun acquireWakeLock() {
        val powerManager = getSystemService(POWER_SERVICE) as PowerManager
        wakeLock = powerManager.newWakeLock(PowerManager.FULL_WAKE_LOCK or PowerManager.ACQUIRE_CAUSES_WAKEUP, "BuddyBot::WakeLock")
        wakeLock?.acquire(10 * 60 * 1000L)
    }

    private fun checkAndRequestPermissions() {
        val needed = BuddyBotConfig.REQUIRED_PERMISSIONS.filter { ContextCompat.checkSelfPermission(this, it) != PackageManager.PERMISSION_GRANTED }
        if (needed.isEmpty()) initializeApp() else permissionLauncher.launch(needed.toTypedArray())
    }

    private fun initializeApp() {
        logComm("SYS", "Initializing BuddyBot Master")
        tts = TextToSpeech(this, this)
        initializeSpeechRecognizer()
        playVideo("splash", loop = true)
    }

    private fun setupComposeUI() { /* ... */ }
    private fun playVideo(videoState: String, loop: Boolean = false, onComplete: (() -> Unit)? = null) { /* ... */ }
    private fun initializeSpeechRecognizer() { /* ... */ }
    private fun logComm(source: String, message: String) { /* ... */ }

    override fun onInit(p0: Int) { /* ... */ }
}
