/*
 * ═══════════════════════════════════════════════════════════════════════
 *  MainViewModel — thin binding layer over MessageRouter + ArduinoComms
 * ═══════════════════════════════════════════════════════════════════════
 *  V37 REFACTOR
 *  ─────────────
 *  All parsing moved to MessageRouter (single source of truth). ViewModel
 *  subscribes to router flows and exposes RobotState + telemetry to UI.
 *  MainActivity no longer parses lines directly.
 * ═══════════════════════════════════════════════════════════════════════
 */

package com.buddybot.kids

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.launchIn
import kotlinx.coroutines.flow.onEach
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch

class MainViewModel(
    private val arduinoComms: ArduinoComms,
    val router: MessageRouter = MessageRouter()
) : ViewModel() {

    private val _robotState = MutableStateFlow(RobotState())
    val robotState: StateFlow<RobotState> = _robotState.asStateFlow()

    /** Telemetry flow proxied from router. */
    val telemetry: StateFlow<TelemetryData> = router.telemetry

    private val _commLogs = MutableStateFlow<List<String>>(emptyList())
    val commLogs: StateFlow<List<String>> = _commLogs.asStateFlow()

    init {
        // Route every raw line from ArduinoComms into the router.
        arduinoComms.onMessageReceived = { msg ->
            router.handleLine(msg)
            logComm("ARDUINO", msg)
        }

        // Track link state
        arduinoComms.communicationMode
            .onEach { mode -> _robotState.update { it.copy(communicationMode = mode) } }
            .launchIn(viewModelScope)

        // Mirror mode changes from router into robotState
        router.modeChange
            .onEach { newMode -> _robotState.update { it.copy(currentMode = newMode) } }
            .launchIn(viewModelScope)

        // Mode requests (from Mega REQ_MODE:) trigger PIN entry
        router.modeRequest
            .onEach { req -> _robotState.update { it.copy(showPinEntry = true, requestedMode = req) } }
            .launchIn(viewModelScope)

        // Log every alert
        router.alerts
            .onEach { a -> logComm("ALERT/${a.level}", a.code) }
            .launchIn(viewModelScope)

        // Auto-populate BuddyBot IP when Pico W broadcasts it
        router.wifiIps
            .onEach { ip -> if (ip.isNotEmpty() && ip != "0.0.0.0") updateIP(ip) }
            .launchIn(viewModelScope)

        // Drain raw log channel to StateFlow
        viewModelScope.launch {
            for (raw in router.rawLog) {
                _commLogs.update { (it + "[RX] $raw").takeLast(200) }
            }
        }
    }

    // ─── Motor commands (V37 processS9Command format) ───────────────────
    fun sendMotorCommand(dir: String) {
        val cmd = when (dir.uppercase()) {
            "F", "FORWARD"  -> "MOTOR:F"
            "B", "BACKWARD" -> "MOTOR:B"
            "L", "LEFT"     -> "MOTOR:L"
            "R", "RIGHT"    -> "MOTOR:R"
            "S", "STOP"     -> "MOTOR:S"
            "DANCE"         -> "MOTOR:DANCE"
            else            -> "MOTOR:S"
        }
        sendArduinoCommand(cmd)
    }

    fun setSpeed(level: String) = sendArduinoCommand("SPEED:$level")
    fun toggleAuto(on: Boolean) = sendArduinoCommand(if (on) "AUTO:ON" else "AUTO:OFF")
    fun triggerEstop()          = sendArduinoCommand("EMERGENCY_STOP")
    fun clearEstop()            = sendArduinoCommand("ESTOP_CLEAR")

    fun sendArduinoCommand(command: String) {
        arduinoComms.sendCommand(command)
        logComm("SENT", command)
    }

    fun logComm(source: String, message: String) {
        _commLogs.update { (it + "[$source] $message").takeLast(200) }
    }

    fun setRobotMode(newMode: RobotMode) {
        _robotState.update { it.copy(currentMode = newMode) }
    }

    fun setProcessing(processing: Boolean) {
        _robotState.update { it.copy(isProcessing = processing) }
    }

    fun setSpeaking(speaking: Boolean) {
        _robotState.update { it.copy(isSpeaking = speaking) }
    }

    fun updateIP(ip: String) {
        _robotState.update { it.copy(buddybotIP = ip) }
    }
}
