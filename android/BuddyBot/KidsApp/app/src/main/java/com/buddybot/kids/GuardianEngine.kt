package com.buddybot.kids

import android.content.Context
import android.util.Log
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch

/**
 * ═══════════════════════════════════════════════════════════════════════
 *  GuardianEngine — behavioural monitoring pipeline
 * ═══════════════════════════════════════════════════════════════════════
 *
 *  MISSION
 *  ────────
 *  Actively monitor the environment for detrimental behaviour targeting
 *  AJ (arguing, swearing, yelling, aggression). When detected:
 *
 *    1. Physically approach the source of the conflict
 *    2. Issue a stern verbal warning to the perpetrator
 *    3. Send an immediate notification to the companion "Parent App"
 *
 *  ARCHITECTURE
 *  ─────────────
 *  Detection pipeline: EnvironmentMonitoringService streams audio energy
 *  and Speech-to-Text into detectAggression(). Detection signals are
 *  fused with confidence weighting.
 *
 *  On detection, engine drives a state machine:
 *
 *     IDLE ── detected ──▶ APPROACHING ──▶ WARNING ──▶ ALERTING ──▶ COOLDOWN ──▶ IDLE
 *                 │                              │             │
 *                 └─── cancellable at any point ─┴─────────────┘
 *
 *  V37 STUB
 *  ─────────
 *  Detection is currently pattern-based (keyword + volume). Future upgrades:
 *   - On-device audio classifier (YAMNet/AudioSet: "yelling", "shouting")
 *   - STT sentiment analysis (via AIRouter for context)
 *   - Face-emotion overlay when detected in camera feed
 *
 *  All state transitions log to Logcat with tag "Guardian" for auditing.
 * ═══════════════════════════════════════════════════════════════════════
 */
class GuardianEngine(
    private val context: Context,
    private val scope: CoroutineScope,
    private val arduinoComms: ArduinoComms,
    private val speak: (String) -> Unit,
    private val notifyParentApp: (GuardianEvent) -> Unit
) {
    companion object {
        private const val TAG = "Guardian"

        /** Words that trip the aggression detector when spoken loudly. */
        private val AGGRESSION_KEYWORDS = setOf(
            "shut up", "stupid", "hate", "idiot", "loser", "moron",
            "damn", "fuck", "shit", "bitch", "asshole",
            "get out", "shut it", "go away", "no one likes you"
        )

        /** Cool-down after warning issued to avoid nagging cascade. */
        private const val COOLDOWN_MS = 30_000L

        /** Approach timeout — after this we give the warning regardless. */
        private const val APPROACH_TIMEOUT_MS = 8_000L

        /** Aggression confidence threshold (0.0-1.0). */
        private const val DETECTION_THRESHOLD = 0.65f
    }

    enum class State { IDLE, APPROACHING, WARNING, ALERTING, COOLDOWN }

    private val _state = MutableStateFlow(State.IDLE)
    val state: StateFlow<State> = _state.asStateFlow()

    private val _lastEvent = MutableStateFlow<GuardianEvent?>(null)
    val lastEvent: StateFlow<GuardianEvent?> = _lastEvent.asStateFlow()

    private var currentJob: Job? = null

    /**
     * Called by EnvironmentMonitoringService when audio + STT samples arrive.
     * Fuses volume and keyword signals into a confidence score.
     *
     * @param recognizedText  most recent STT output (may be null)
     * @param audioRms        RMS audio level (0.0-1.0, calibrated to mic)
     * @return confidence 0.0-1.0
     */
    fun detectAggression(recognizedText: String?, audioRms: Float): Float {
        var score = 0f

        // Volume component (yelling): sustained loud audio contributes 0.0-0.5
        if (audioRms > 0.6f) score += (audioRms - 0.6f) * 1.25f

        // Keyword component: 0.0-0.5
        val text = recognizedText?.lowercase()?.trim().orEmpty()
        if (text.isNotEmpty()) {
            for (kw in AGGRESSION_KEYWORDS) {
                if (text.contains(kw)) { score += 0.5f; break }
            }
        }

        return score.coerceIn(0f, 1f)
    }

    /**
     * External entry point: EnvironmentMonitoringService calls this every
     * audio sample tick. Triggers the guardian FSM when threshold is crossed.
     */
    fun onAudioSample(recognizedText: String?, audioRms: Float, sourceBearing: Float? = null) {
        if (_state.value != State.IDLE) return  // already in FSM
        val confidence = detectAggression(recognizedText, audioRms)
        if (confidence >= DETECTION_THRESHOLD) {
            val evt = GuardianEvent(
                timestamp = System.currentTimeMillis(),
                confidence = confidence,
                recognizedText = recognizedText.orEmpty(),
                audioRms = audioRms,
                sourceBearing = sourceBearing
            )
            triggerGuardianResponse(evt)
        }
    }

    /**
     * Kick off the approach → warn → alert sequence.
     * Cancellable via cancel() if the situation resolves.
     */
    private fun triggerGuardianResponse(evt: GuardianEvent) {
        Log.i(TAG, "Aggression detected: conf=${evt.confidence} text='${evt.recognizedText}'")
        _lastEvent.value = evt

        currentJob = scope.launch(Dispatchers.Main) {
            try {
                // ─── APPROACH ────────────────────────────────────────────
                _state.value = State.APPROACHING
                arduinoComms.sendCommand("GUARDIAN:APPROACH")
                if (evt.sourceBearing != null) {
                    val turnCmd = if (evt.sourceBearing > 0) "MOTOR:R" else "MOTOR:L"
                    arduinoComms.sendCommand(turnCmd)
                    delay(400)
                    arduinoComms.sendCommand("MOTOR:S")
                }
                arduinoComms.sendCommand("MOTOR:F")
                delay(APPROACH_TIMEOUT_MS.coerceAtMost(3000L))
                arduinoComms.sendCommand("MOTOR:S")

                // ─── WARNING ─────────────────────────────────────────────
                _state.value = State.WARNING
                arduinoComms.sendCommand("LED:ALERT")
                arduinoComms.sendCommand("BEEP:SEQ:800,150,600,150,800,150")
                delay(400)
                speak(buildWarningPhrase(evt.recognizedText))
                delay(3500)

                // ─── ALERTING ───────────────────────────────────────────
                _state.value = State.ALERTING
                notifyParentApp(evt)
                arduinoComms.sendCommand("LED:OFF")

                // ─── COOLDOWN ───────────────────────────────────────────
                _state.value = State.COOLDOWN
                delay(COOLDOWN_MS)
            } finally {
                _state.value = State.IDLE
            }
        }
    }

    fun cancel() {
        currentJob?.cancel()
        currentJob = null
        _state.value = State.IDLE
        arduinoComms.sendCommand("MOTOR:S")
        arduinoComms.sendCommand("LED:OFF")
    }

    /** Choose a warning phrase adaptively. */
    private fun buildWarningPhrase(offendingText: String): String {
        val lower = offendingText.lowercase()
        return when {
            lower.contains("shut up") || lower.contains("shut it") ->
                "Hey, please be kind. That is not okay."
            lower.contains("stupid") || lower.contains("idiot") || lower.contains("moron") ->
                "That's a mean word. Let's use nice words instead."
            lower.contains("hate") ->
                "Please don't say hate. Everyone deserves kindness."
            lower.isBlank() ->
                "Voices are getting loud. Let's take a breath together."
            else ->
                "Hey, that's not kind. Please stop and try again."
        }
    }
}

/**
 * Structured event forwarded to the parent app.
 * Serialisable via FCM data payload for the companion Parent App.
 */
data class GuardianEvent(
    val timestamp: Long,
    val confidence: Float,
    val recognizedText: String,
    val audioRms: Float,
    val sourceBearing: Float? = null
)