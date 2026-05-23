package com.buddybot.kids

import android.Manifest
import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.Service
import android.content.Intent
import android.content.pm.PackageManager
import android.content.pm.ServiceInfo
import android.media.AudioFormat
import android.media.AudioRecord
import android.media.MediaRecorder
import android.os.Build
import android.os.IBinder
import android.util.Log
import androidx.core.app.NotificationCompat
import androidx.core.content.ContextCompat
import com.buddybot.kids.R
import kotlinx.coroutines.*
import okhttp3.*
import org.json.JSONObject
import java.io.File
import java.io.FileOutputStream
import kotlin.math.pow
import kotlin.math.sqrt

/**
 * Environment Monitoring Service
 * 
 * Continuously monitors audio environment for concerning situations.
 */
class EnvironmentMonitoringService : Service() {

    companion object {
        private const val TAG = "EnvironmentMonitor"
        private const val CHANNEL_ID = "buddybot_monitoring"
        private const val NOTIFICATION_ID = 100
        
        // Audio recording constants
        private const val SAMPLE_RATE = 16000
        private const val CHANNEL_CONFIG = AudioFormat.CHANNEL_IN_MONO
        private const val AUDIO_FORMAT = AudioFormat.ENCODING_PCM_16BIT
        
        // Analysis thresholds
        private const val VOLUME_THRESHOLD_YELLING = 0.7 // 70% of max volume
        private const val SILENCE_THRESHOLD = 0.05 // 5% of max
        private const val SILENCE_DURATION_MS = 300000L // 5 minutes
        
        // Detection keywords
        private val SWEAR_WORDS = setOf(
            "damn", "hell", "crap", "shit", "fuck", "ass", "bitch",
            "stupid", "idiot", "hate", "shut up"
        )
        
        private val DISTRESS_PHRASES = setOf(
            "help", "stop", "no", "don't", "scared", "hurt", "pain",
            "mommy", "daddy", "emergency"
        )
        
        private val ARGUMENT_PHRASES = setOf(
            "you always", "you never", "shut up", "leave me alone",
            "i hate you", "go away", "stop it", "fighting"
        )
    }

    private val scope = CoroutineScope(Dispatchers.IO + SupervisorJob())
    private var audioRecord: AudioRecord? = null
    private var isMonitoring = false
    private var lastSoundTime = System.currentTimeMillis()
    private var lastNotificationTime = 0L
    private val notificationCooldown = 60000L // 1 minute between similar notifications
    
    private val httpClient = OkHttpClient()
    
    // Volume tracking
    private val recentVolumes = mutableListOf<Float>()
    private val maxVolumeHistory = 100

    override fun onCreate() {
        super.onCreate()
        createNotificationChannel()
        
        val notification = createNotification()
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.UPSIDE_DOWN_CAKE) {
            startForeground(NOTIFICATION_ID, notification, ServiceInfo.FOREGROUND_SERVICE_TYPE_MICROPHONE)
        } else {
            startForeground(NOTIFICATION_ID, notification)
        }
        
        startMonitoring()
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        return START_STICKY
    }

    override fun onBind(intent: Intent?): IBinder? = null

    private fun createNotificationChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val channel = NotificationChannel(
                CHANNEL_ID,
                "BuddyBot Monitoring",
                NotificationManager.IMPORTANCE_LOW
            ).apply {
                description = "Continuous environment monitoring"
            }
            
            val notificationManager = getSystemService(NotificationManager::class.java)
            notificationManager?.createNotificationChannel(channel)
        }
    }

    private fun createNotification(): Notification {
        return NotificationCompat.Builder(this, CHANNEL_ID)
            .setContentTitle("BuddyBot Monitoring")
            .setContentText("Actively monitoring environment")
            .setSmallIcon(R.drawable.ic_launcher_foreground)
            .setOngoing(true)
            .setPriority(NotificationCompat.PRIORITY_LOW)
            .build()
    }

    private fun startMonitoring() {
        if (ContextCompat.checkSelfPermission(this, Manifest.permission.RECORD_AUDIO) 
            != PackageManager.PERMISSION_GRANTED) {
            Log.e(TAG, "Audio permission not granted")
            stopSelf()
            return
        }

        val bufferSize = AudioRecord.getMinBufferSize(SAMPLE_RATE, CHANNEL_CONFIG, AUDIO_FORMAT)
        if (bufferSize <= 0) {
            Log.e(TAG, "Invalid buffer size: $bufferSize")
            stopSelf()
            return
        }
        
        try {
            // Use VOICE_COMMUNICATION instead of MIC so AudioRecord does NOT hold
            // the hardware mic exclusively. MIC source blocks SpeechRecognizer entirely
            // on Samsung devices — HotwordService gets ERROR_AUDIO on every attempt.
            // VOICE_COMMUNICATION is shareable with SpeechRecognizer.
            audioRecord = AudioRecord(
                MediaRecorder.AudioSource.VOICE_COMMUNICATION,
                SAMPLE_RATE,
                CHANNEL_CONFIG,
                AUDIO_FORMAT,
                bufferSize * 2
            )
            
            if (audioRecord?.state != AudioRecord.STATE_INITIALIZED) {
                Log.e(TAG, "AudioRecord initialization failed")
                stopSelf()
                return
            }
            
            audioRecord?.startRecording()
            isMonitoring = true
            
            scope.launch {
                monitorAudioLoop(bufferSize)
            }
            
            scope.launch {
                monitorSilence()
            }
            
            Log.d(TAG, "Environment monitoring started")
            
        } catch (e: Exception) {
            Log.e(TAG, "Error starting audio monitoring", e)
            stopSelf()
        }
    }

    private suspend fun monitorAudioLoop(bufferSize: Int) = withContext(Dispatchers.IO) {
        val buffer = ShortArray(bufferSize)
        val analysisInterval = SAMPLE_RATE * 3 // 3 seconds of data
        var analysisData = ShortArray(analysisInterval)
        var analysisPointer = 0
        
        while (isMonitoring) {
            val read = audioRecord?.read(buffer, 0, buffer.size) ?: 0
            
            if (read > 0) {
                // Volume check
                val volume = calculateVolume(buffer, read)
                trackVolume(volume)
                
                if (volume > SILENCE_THRESHOLD) {
                    lastSoundTime = System.currentTimeMillis()
                }
                
                if (volume > VOLUME_THRESHOLD_YELLING) {
                    handleYelling(volume)
                }

                // Copy to analysis buffer
                val remaining = analysisInterval - analysisPointer
                val toCopy = if (read < remaining) read else remaining
                System.arraycopy(buffer, 0, analysisData, analysisPointer, toCopy)
                analysisPointer += toCopy
                
                if (analysisPointer >= analysisInterval) {
                    analyzeAudio(analysisData.copyOf())
                    analysisPointer = 0
                }
            }
            delay(10)
        }
    }

    private fun calculateVolume(buffer: ShortArray, readSize: Int): Float {
        var sum = 0.0
        for (i in 0 until readSize) {
            sum += (buffer[i].toDouble() / Short.MAX_VALUE).pow(2.0)
        }
        val rms = sqrt(sum / readSize)
        return rms.toFloat()
    }

    private fun trackVolume(volume: Float) {
        synchronized(recentVolumes) {
            recentVolumes.add(volume)
            if (recentVolumes.size > maxVolumeHistory) {
                recentVolumes.removeAt(0)
            }
        }
    }

    private fun isVolumeSpiking(): Boolean {
        synchronized(recentVolumes) {
            if (recentVolumes.size < 10) return false
            val recent = recentVolumes.takeLast(5).average()
            val baseline = recentVolumes.dropLast(5).average()
            return recent > baseline * 2.0
        }
    }

    private suspend fun analyzeAudio(audioData: ShortArray) {
        val audioFile = File(cacheDir, "audio_${System.currentTimeMillis()}.wav")
        saveWavFile(audioFile, audioData)
        
        val transcript = transcribeAudio(audioFile)
        if (transcript.isNotEmpty()) {
            analyzeTranscript(transcript)
        }
        audioFile.delete()
    }

    private fun saveWavFile(file: File, audioData: ShortArray) {
        try {
            FileOutputStream(file).use { fos ->
                val channels = 1
                val bitsPerSample = 16
                val byteRate = SAMPLE_RATE * channels * bitsPerSample / 8
                val dataLen = audioData.size * 2
                val totalLen = dataLen + 36
                
                val header = ByteArray(44)
                header[0] = 'R'.code.toByte()
                header[1] = 'I'.code.toByte()
                header[2] = 'F'.code.toByte()
                header[3] = 'F'.code.toByte()
                header[4] = (totalLen and 0xff).toByte()
                header[5] = ((totalLen shr 8) and 0xff).toByte()
                header[6] = ((totalLen shr 16) and 0xff).toByte()
                header[7] = ((totalLen shr 24) and 0xff).toByte()
                header[8] = 'W'.code.toByte()
                header[9] = 'A'.code.toByte()
                header[10] = 'V'.code.toByte()
                header[11] = 'E'.code.toByte()
                header[12] = 'f'.code.toByte()
                header[13] = 'm'.code.toByte()
                header[14] = 't'.code.toByte()
                header[15] = ' '.code.toByte()
                header[16] = 16 // Subchunk1Size
                header[17] = 0
                header[18] = 0
                header[19] = 0
                header[20] = 1 // AudioFormat (PCM)
                header[21] = 0
                header[22] = channels.toByte()
                header[23] = 0
                header[24] = (SAMPLE_RATE and 0xff).toByte()
                header[25] = ((SAMPLE_RATE shr 8) and 0xff).toByte()
                header[26] = ((SAMPLE_RATE shr 16) and 0xff).toByte()
                header[27] = ((SAMPLE_RATE shr 24) and 0xff).toByte()
                header[28] = (byteRate and 0xff).toByte()
                header[29] = ((byteRate shr 8) and 0xff).toByte()
                header[30] = ((byteRate shr 16) and 0xff).toByte()
                header[31] = ((byteRate shr 24) and 0xff).toByte()
                header[32] = (channels * bitsPerSample / 8).toByte()
                header[33] = 0
                header[34] = bitsPerSample.toByte()
                header[35] = 0
                header[36] = 'd'.code.toByte()
                header[37] = 'a'.code.toByte()
                header[38] = 't'.code.toByte()
                header[39] = 'a'.code.toByte()
                header[40] = (dataLen and 0xff).toByte()
                header[41] = ((dataLen shr 8) and 0xff).toByte()
                header[42] = ((dataLen shr 16) and 0xff).toByte()
                header[43] = ((dataLen shr 24) and 0xff).toByte()
                
                fos.write(header)
                val byteBuffer = ByteArray(dataLen)
                for (i in audioData.indices) {
                    byteBuffer[i * 2] = (audioData[i].toInt() and 0xff).toByte()
                    byteBuffer[i * 2 + 1] = ((audioData[i].toInt() shr 8) and 0xff).toByte()
                }
                fos.write(byteBuffer)
            }
        } catch (e: Exception) {
            Log.e(TAG, "Error saving WAV file", e)
        }
    }

    private suspend fun transcribeAudio(audioFile: File): String = withContext(Dispatchers.IO) {
        ""
    }

    private fun analyzeTranscript(transcript: String) {
        val lowerTranscript = transcript.lowercase()
        val swearWords = SWEAR_WORDS.filter { lowerTranscript.contains(it) }
        if (swearWords.isNotEmpty()) handleSwearing()
        
        val distressWords = DISTRESS_PHRASES.filter { lowerTranscript.contains(it) }
        if (distressWords.isNotEmpty()) handleDistress(distressWords)
        
        val argumentPhrases = ARGUMENT_PHRASES.filter { lowerTranscript.contains(it) }
        if (argumentPhrases.isNotEmpty() && isVolumeSpiking()) handleArgument()
    }

    private fun handleYelling(volume: Float) {
        val currentTime = System.currentTimeMillis()
        if (currentTime - lastNotificationTime < notificationCooldown) return
        lastNotificationTime = currentTime
        sendAlert("YELLING", "Yelling detected - level: ${(volume * 100).toInt()}%", 7)
    }

    private fun handleSwearing() {
        val currentTime = System.currentTimeMillis()
        if (currentTime - lastNotificationTime < notificationCooldown) return
        lastNotificationTime = currentTime
        sendAlert("INAPPROPRIATE_LANGUAGE", "Inappropriate language detected", 6)
    }

    private fun handleDistress(words: List<String>) {
        sendAlert("CHILD_DISTRESS", "⚠️ Child distress: ${words.joinToString(", ")}", 9, true)
    }

    private fun handleArgument() {
        val currentTime = System.currentTimeMillis()
        if (currentTime - lastNotificationTime < notificationCooldown) return
        lastNotificationTime = currentTime
        sendAlert("ARGUMENT", "Possible argument detected", 7)
    }

    private suspend fun monitorSilence() {
        while (isMonitoring) {
            delay(60000)
            val silenceDuration = System.currentTimeMillis() - lastSoundTime
            if (silenceDuration > SILENCE_DURATION_MS) {
                sendAlert("EXTENDED_SILENCE", "No sound for ${silenceDuration / 60000} mins", 5)
            }
        }
    }

    private fun sendAlert(type: String, message: String, severity: Int, urgent: Boolean = false) {
        scope.launch {
            Log.d(TAG, "Alert: $type - $message")
        }
    }

    override fun onDestroy() {
        super.onDestroy()
        isMonitoring = false
        audioRecord?.stop()
        audioRecord?.release()
        scope.cancel()
    }
}
