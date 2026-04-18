package com.buddybot.kids

// ═══════════════════════════════════════════════════════════════════════════════
// HotwordService.kt  –  PHASE 3: Always-Listening Foreground Service
//
// Runs as a foreground service with FOREGROUND_SERVICE_TYPE_MICROPHONE.
// Continuously listens for the wake-word "Hey Buddy" using Android's on-device
// SpeechRecognizer (no cloud dependency for hotword detection).
// On trigger → broadcasts ACTION_HOTWORD_DETECTED so MainActivity can start
// the full command-capture + Claude AI + ElevenLabs TTS pipeline.
//
// Lifecycle:
//   start  → startForeground() → startListeningLoop()
//   hotword detected → broadcast → restart loop
//   error  → exponential backoff restart (max 30 s)
//   stop   → cancel coroutine, release recognizer
// ═══════════════════════════════════════════════════════════════════════════════

import android.Manifest
import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Intent
import android.content.pm.PackageManager
import android.content.pm.ServiceInfo
import android.os.Build
import android.os.Bundle
import android.os.IBinder
import android.speech.RecognitionListener
import android.speech.RecognizerIntent
import android.speech.SpeechRecognizer
import android.util.Log
import androidx.core.app.NotificationCompat
import androidx.core.content.ContextCompat
import kotlinx.coroutines.*

class HotwordService : Service() {

    companion object {
        private const val TAG = "HotwordService"
        const val CHANNEL_ID   = "buddybot_hotword"
        const val NOTIF_ID     = 200
        const val ACTION_HOTWORD_DETECTED = "com.buddybot.kids.HOTWORD_DETECTED"
        const val ACTION_STOP_SERVICE     = "com.buddybot.kids.STOP_HOTWORD"

        // Backoff constants
        private const val INITIAL_RESTART_DELAY_MS = 500L
        private const val MAX_RESTART_DELAY_MS      = 30_000L
    }

    private val scope = CoroutineScope(Dispatchers.Main + SupervisorJob())

    /** The recognizer is created/destroyed on the main thread (Android requirement). */
    private var recognizer: SpeechRecognizer? = null

    /** Tracks whether we are currently in an active recognition session. */
    @Volatile private var isListening = false

    /** Tracks whether the service should keep running. */
    @Volatile private var shouldRun = true

    /** Exponential backoff counter for error restarts. */
    private var restartDelayMs = INITIAL_RESTART_DELAY_MS

    // ── Lifecycle ─────────────────────────────────────────────────────────────

    override fun onCreate() {
        super.onCreate()
        Log.d(TAG, "HotwordService created")
        createNotificationChannel()
        startForegroundCompat()
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        if (intent?.action == ACTION_STOP_SERVICE) {
            Log.d(TAG, "Stop requested via intent")
            stopSelf()
            return START_NOT_STICKY
        }
        if (shouldRun && !isListening) {
            startListeningLoop()
        }
        return START_STICKY   // OS restarts us if killed
    }

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onDestroy() {
        Log.d(TAG, "HotwordService destroyed")
        shouldRun = false
        isListening = false
        releaseRecognizer()
        scope.cancel()
        super.onDestroy()
    }

    // ── Foreground notification ───────────────────────────────────────────────

    private fun createNotificationChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val channel = NotificationChannel(
                CHANNEL_ID,
                "BuddyBot Listening",
                NotificationManager.IMPORTANCE_LOW
            ).apply {
                description = "Always-on wake-word detection"
                setShowBadge(false)
            }
            getSystemService(NotificationManager::class.java)
                ?.createNotificationChannel(channel)
        }
    }

    private fun buildNotification(): Notification {
        // Tap notification → open MainActivity
        val openIntent = PendingIntent.getActivity(
            this, 0,
            Intent(this, MainActivity::class.java),
            PendingIntent.FLAG_IMMUTABLE
        )
        // Stop action
        val stopIntent = PendingIntent.getService(
            this, 1,
            Intent(this, HotwordService::class.java).apply { action = ACTION_STOP_SERVICE },
            PendingIntent.FLAG_IMMUTABLE
        )
        return NotificationCompat.Builder(this, CHANNEL_ID)
            .setContentTitle("BuddyBot is listening…")
            .setContentText("Say \"Hey Buddy\" to wake me up!")
            .setSmallIcon(R.drawable.ic_launcher_foreground)
            .setOngoing(true)
            .setPriority(NotificationCompat.PRIORITY_LOW)
            .setContentIntent(openIntent)
            .addAction(0, "Stop", stopIntent)
            .build()
    }

    private fun startForegroundCompat() {
        val notification = buildNotification()
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.UPSIDE_DOWN_CAKE) {
            startForeground(
                NOTIF_ID, notification,
                ServiceInfo.FOREGROUND_SERVICE_TYPE_MICROPHONE
            )
        } else {
            startForeground(NOTIF_ID, notification)
        }
    }

    // ── Recognition loop ──────────────────────────────────────────────────────

    /**
     * Starts the continuous listening loop on the main thread (required by
     * SpeechRecognizer). Restarts automatically after results or errors.
     */
    private fun startListeningLoop() {
        if (!shouldRun) return

        // Guard: microphone permission
        if (ContextCompat.checkSelfPermission(this, Manifest.permission.RECORD_AUDIO)
            != PackageManager.PERMISSION_GRANTED
        ) {
            Log.e(TAG, "RECORD_AUDIO permission not granted — stopping service")
            stopSelf()
            return
        }

        // Guard: device supports speech recognition
        if (!SpeechRecognizer.isRecognitionAvailable(this)) {
            Log.e(TAG, "SpeechRecognizer not available on this device")
            stopSelf()
            return
        }

        scope.launch(Dispatchers.Main) {
            startRecognition()
        }
    }

    private fun startRecognition() {
        if (!shouldRun) return

        // Release any stale recognizer before creating a new one
        releaseRecognizer()

        recognizer = SpeechRecognizer.createSpeechRecognizer(this).also { sr ->
            sr.setRecognitionListener(object : RecognitionListener {

                override fun onReadyForSpeech(params: Bundle?) {
                    Log.d(TAG, "Ready for speech (hotword mode)")
                    isListening = true
                    restartDelayMs = INITIAL_RESTART_DELAY_MS  // reset backoff on success
                }

                override fun onResults(results: Bundle?) {
                    isListening = false
                    val matches = results
                        ?.getStringArrayList(SpeechRecognizer.RESULTS_RECOGNITION)
                        ?: return scheduleRestart()

                    val transcript = matches.joinToString(" ").lowercase()
                    Log.d(TAG, "Hotword check: \"$transcript\"")

                    if (transcript.contains(BuddyBotConfig.WAKE_WORD.lowercase())) {
                        Log.i(TAG, "🎤 HOTWORD DETECTED: \"$transcript\"")
                        broadcastHotword()
                        // Short pause so MainActivity can take over the mic
                        scope.launch {
                            delay(1500)
                            scheduleRestart()
                        }
                    } else {
                        scheduleRestart()
                    }
                }

                override fun onPartialResults(partialResults: Bundle?) {
                    // Check partial results for faster hotword response
                    val partial = partialResults
                        ?.getStringArrayList(SpeechRecognizer.RESULTS_RECOGNITION)
                        ?.firstOrNull()?.lowercase() ?: return
                    if (partial.contains(BuddyBotConfig.WAKE_WORD.lowercase())) {
                        Log.i(TAG, "🎤 HOTWORD (partial): \"$partial\"")
                        isListening = false
                        broadcastHotword()
                        scope.launch {
                            delay(1500)
                            scheduleRestart()
                        }
                    }
                }

                override fun onError(error: Int) {
                    isListening = false
                    val errorName = speechErrorName(error)
                    Log.w(TAG, "SpeechRecognizer error: $errorName ($error)")
                    // NO_MATCH and SPEECH_TIMEOUT are normal — restart immediately
                    val delay = when (error) {
                        SpeechRecognizer.ERROR_NO_MATCH,
                        SpeechRecognizer.ERROR_SPEECH_TIMEOUT -> 300L
                        SpeechRecognizer.ERROR_RECOGNIZER_BUSY -> 2000L
                        else -> restartDelayMs.also {
                            restartDelayMs = (restartDelayMs * 2).coerceAtMost(MAX_RESTART_DELAY_MS)
                        }
                    }
                    scope.launch {
                        delay(delay)
                        scheduleRestart()
                    }
                }

                // ── Unused callbacks ─────────────────────────────────────────
                override fun onBeginningOfSpeech() {}
                override fun onRmsChanged(rmsdB: Float) {}
                override fun onBufferReceived(buffer: ByteArray?) {}
                override fun onEndOfSpeech() { isListening = false }
                override fun onEvent(eventType: Int, params: Bundle?) {}
            })
        }

        // Build the recognition intent — continuous, offline-preferred
        val intent = Intent(RecognizerIntent.ACTION_RECOGNIZE_SPEECH).apply {
            putExtra(RecognizerIntent.EXTRA_LANGUAGE_MODEL, RecognizerIntent.LANGUAGE_MODEL_FREE_FORM)
            putExtra(RecognizerIntent.EXTRA_CALLING_PACKAGE, packageName)
            putExtra(RecognizerIntent.EXTRA_PARTIAL_RESULTS, true)
            putExtra(RecognizerIntent.EXTRA_MAX_RESULTS, 3)
            // Prefer on-device recognition (Android 13+)
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                putExtra(RecognizerIntent.EXTRA_PREFER_OFFLINE, true)
            }
            // Keep listening for up to 10 seconds of silence before auto-stopping
            putExtra(RecognizerIntent.EXTRA_SPEECH_INPUT_COMPLETE_SILENCE_LENGTH_MILLIS, 10_000L)
            putExtra(RecognizerIntent.EXTRA_SPEECH_INPUT_POSSIBLY_COMPLETE_SILENCE_LENGTH_MILLIS, 10_000L)
            putExtra(RecognizerIntent.EXTRA_SPEECH_INPUT_MINIMUM_LENGTH_MILLIS, 500L)
        }

        try {
            recognizer?.startListening(intent)
            Log.d(TAG, "SpeechRecognizer.startListening() called")
        } catch (e: Exception) {
            Log.e(TAG, "startListening() threw: ${e.message}")
            scope.launch {
                delay(restartDelayMs)
                restartDelayMs = (restartDelayMs * 2).coerceAtMost(MAX_RESTART_DELAY_MS)
                scheduleRestart()
            }
        }
    }

    private fun scheduleRestart() {
        if (!shouldRun) return
        scope.launch(Dispatchers.Main) {
            startRecognition()
        }
    }

    private fun releaseRecognizer() {
        try {
            recognizer?.cancel()
            recognizer?.destroy()
        } catch (e: Exception) {
            Log.w(TAG, "Error releasing recognizer: ${e.message}")
        } finally {
            recognizer = null
        }
    }

    // ── Broadcast ─────────────────────────────────────────────────────────────

    private fun broadcastHotword() {
        val intent = Intent(ACTION_HOTWORD_DETECTED).apply {
            setPackage(packageName)
        }
        sendBroadcast(intent)
        Log.d(TAG, "Hotword broadcast sent")
    }

    // ── Helpers ───────────────────────────────────────────────────────────────

    private fun speechErrorName(error: Int) = when (error) {
        SpeechRecognizer.ERROR_AUDIO                -> "AUDIO"
        SpeechRecognizer.ERROR_CLIENT               -> "CLIENT"
        SpeechRecognizer.ERROR_INSUFFICIENT_PERMISSIONS -> "INSUFFICIENT_PERMISSIONS"
        SpeechRecognizer.ERROR_NETWORK              -> "NETWORK"
        SpeechRecognizer.ERROR_NETWORK_TIMEOUT      -> "NETWORK_TIMEOUT"
        SpeechRecognizer.ERROR_NO_MATCH             -> "NO_MATCH"
        SpeechRecognizer.ERROR_RECOGNIZER_BUSY      -> "RECOGNIZER_BUSY"
        SpeechRecognizer.ERROR_SERVER               -> "SERVER"
        SpeechRecognizer.ERROR_SPEECH_TIMEOUT       -> "SPEECH_TIMEOUT"
        else                                        -> "UNKNOWN($error)"
    }
}
