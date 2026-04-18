package com.buddybot.kids

import android.content.Context
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.jiangdg.ausbc.camera.bean.CameraRequest
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch

class MainViewModel(
    private val arduinoComms: ArduinoComms
) : ViewModel() {

    private val _robotState = MutableStateFlow(RobotState())
    val robotState: StateFlow<RobotState> = _robotState.asStateFlow()

    private val _telemetry = MutableStateFlow(TelemetryData())
    val telemetry: StateFlow<TelemetryData> = _telemetry.asStateFlow()

    private val _commLogs = MutableStateFlow<List<String>>(emptyList())
    val commLogs: StateFlow<List<String>> = _commLogs.asStateFlow()

    init {
        arduinoComms.onMessageReceived = { msg ->
            handleArduinoMessage(msg)
        }
        
        viewModelScope.launch {
            arduinoComms.communicationMode.collect { mode ->
                _robotState.update { it.copy(communicationMode = mode) }
            }
        }
    }

    private fun handleArduinoMessage(msg: String) {
        logComm("ARDUINO", msg)
        
        when {
            msg.startsWith("TELE:") -> parseTele(msg.substring(5))
            msg.startsWith("US:") -> parseUltrasonic(msg.substring(3))
            msg.startsWith("STAT:") -> parseStat(msg.substring(5))
            msg.startsWith("DIAG|") -> parseDiag(msg.substring(5))
            msg.startsWith("ALERT:") -> handleAlert(msg.substring(6))
            msg.startsWith("MODE:") -> {
                val modeStr = msg.substring(5).trim()
                try {
                    val mode = RobotMode.valueOf(modeStr)
                    _robotState.update { it.copy(currentMode = mode) }
                } catch (e: Exception) {
                    logComm("SYS", "Invalid mode from Mega: $modeStr")
                }
            }
            msg == "PONG" -> logComm("SYS", "Arduino Heartbeat: PONG")
            msg.startsWith("DIAG:") -> logComm("DIAG", msg.substring(5))
        }
    }

    private fun parseTele(data: String) {
        try {
            val parts = data.split(",")
            if (parts.size >= 2) {
                _telemetry.update { it.copy(
                    batteryVoltage = parts[0].toFloatOrNull() ?: it.batteryVoltage,
                    batteryPercent = parts[1].toIntOrNull() ?: it.batteryPercent
                )}
            }
        } catch (e: Exception) {
            logComm("SYS", "TELE parse error: ${e.message}")
        }
    }

    private fun parseUltrasonic(data: String) {
        try {
            val parts = data.split(",")
            if (parts.size >= 4) {
                _telemetry.update { it.copy(
                    frontDistance = parts[0].toIntOrNull() ?: it.frontDistance,
                    rearDistance = parts[1].toIntOrNull() ?: it.rearDistance,
                    leftDistance = parts[2].toIntOrNull() ?: it.leftDistance,
                    rightDistance = parts[3].toIntOrNull() ?: it.rightDistance
                )}
            }
        } catch (e: Exception) {
            logComm("SYS", "US parse error: ${e.message}")
        }
    }

    private fun parseStat(data: String) {
        try {
            val parts = data.split(":")
            if (parts.size >= 11) {
                // STAT:gas:temp:hum:haz:pir:tilt:flame:ir:volt:pct:amps
                _telemetry.update { it.copy(
                    gasLevel = parts[0].toIntOrNull() ?: it.gasLevel,
                    temperature = parts[1].toFloatOrNull() ?: it.temperature,
                    humidity = parts[2].toFloatOrNull() ?: it.humidity,
                    hazAlert = parts[3] == "1",
                    pirAlert = parts[4] == "1",
                    tiltAlert = parts[5] == "1",
                    flameAlert = parts[6] == "1",
                    irAlert = parts[7] == "1",
                    batteryVoltage = parts[8].toFloatOrNull() ?: it.batteryVoltage,
                    batteryPercent = parts[9].toIntOrNull() ?: it.batteryPercent,
                    currentAmps = parts[10].toFloatOrNull() ?: it.currentAmps
                )}
            }
        } catch (e: Exception) {
            logComm("SYS", "STAT parse error: ${e.message}")
        }
    }

    private fun parseDiag(data: String) {
        // DIAG|BAT:x.xV|PCT:x%|TEMP:x.xC|F:xcm|R:xcm|L:xcm|Ri:xcm|GPS:x.x,x.x|SAT:x|S9:OK|AUTO:OFF|UPT:xs|END
        // We mainly use it for GPS and uptime which aren't in STAT
        try {
            val sections = data.split("|")
            sections.forEach { section ->
                when {
                    section.startsWith("GPS:") -> {
                        val coords = section.substring(4).split(",")
                        if (coords.size == 2) {
                            _telemetry.update { it.copy(
                                gpsLat = coords[0].toDoubleOrNull() ?: it.gpsLat,
                                gpsLon = coords[1].toDoubleOrNull() ?: it.gpsLon
                            )}
                        }
                    }
                    section.startsWith("UPT:") -> {
                        val uptimeStr = section.substring(4).replace("s", "")
                        _telemetry.update { it.copy(
                            uptimeSec = uptimeStr.toLongOrNull() ?: it.uptimeSec
                        )}
                    }
                }
            }
        } catch (e: Exception) {
            logComm("SYS", "DIAG parse error: ${e.message}")
        }
    }

    private fun handleAlert(alert: String) {
        logComm("ALERT", alert)
        // Handle specific alerts like "LOW_BATTERY", "OVERHEAT", etc.
    }

    fun sendMotorCommand(dir: String) {
        val cmd = when(dir.uppercase()) {
            "F", "FORWARD" -> "MOTOR:F"
            "B", "BACKWARD" -> "MOTOR:B"
            "L", "LEFT" -> "MOTOR:L"
            "R", "RIGHT" -> "MOTOR:R"
            "S", "STOP" -> "MOTOR:S"
            "DANCE" -> "MOTOR:DANCE"
            else -> "MOTOR:S"
        }
        sendArduinoCommand(cmd)
    }

    fun setSpeed(level: String) {
        // level can be SLOW, NORMAL, FAST or a number
        sendArduinoCommand("SPEED:$level")
    }

    fun toggleAuto(on: Boolean) {
        sendArduinoCommand(if (on) "AUTO:ON" else "AUTO:OFF")
    }

    fun triggerEstop() {
        sendArduinoCommand("EMERGENCY_STOP")
    }

    fun clearEstop() {
        sendArduinoCommand("ESTOP_CLEAR")
    }

    fun sendArduinoCommand(command: String) {
        arduinoComms.sendCommand(command)
        logComm("SENT", command)
    }

    fun logComm(source: String, message: String) {
        val entry = "[$source] $message"
        _commLogs.update { (it + entry).takeLast(100) }
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

    fun updateTelemetry(newData: TelemetryData) {
        _telemetry.value = newData
    }

    fun updateIP(ip: String) {
        _robotState.update { it.copy(buddybotIP = ip) }
    }
}
