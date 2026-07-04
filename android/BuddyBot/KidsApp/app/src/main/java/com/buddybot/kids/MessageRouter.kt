package com.buddybot.kids

import android.util.Log
import kotlinx.coroutines.channels.BufferOverflow
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharedFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asSharedFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update

/**
 * MessageRouter — single Mega V37 line dispatcher.
 * V37: All parsing lives here. Solves the dual-registration bug where
 * MainActivity and MainViewModel both hooked ArduinoComms.onMessageReceived.
 */
class MessageRouter {
    companion object { private const val TAG = "MsgRouter" }

    private val _telemetry = MutableStateFlow(TelemetryData())
    val telemetry: StateFlow<TelemetryData> = _telemetry.asStateFlow()

    private val _modeChange = MutableSharedFlow<RobotMode>(extraBufferCapacity = 4)
    val modeChange: SharedFlow<RobotMode> = _modeChange.asSharedFlow()

    private val _modeRequest = MutableSharedFlow<RobotMode>(extraBufferCapacity = 4)
    val modeRequest: SharedFlow<RobotMode> = _modeRequest.asSharedFlow()

    private val _alerts = MutableSharedFlow<SystemAlert>(extraBufferCapacity = 16)
    val alerts: SharedFlow<SystemAlert> = _alerts.asSharedFlow()

    private val _gestures = MutableSharedFlow<String>(extraBufferCapacity = 8)
    val gestures: SharedFlow<String> = _gestures.asSharedFlow()

    private val _acks = MutableSharedFlow<String>(extraBufferCapacity = 16)
    val acks: SharedFlow<String> = _acks.asSharedFlow()

    private val _wifiIps = MutableSharedFlow<String>(extraBufferCapacity = 4)
    val wifiIps: SharedFlow<String> = _wifiIps.asSharedFlow()

    val rawLog: Channel<String> = Channel(capacity = 200, onBufferOverflow = BufferOverflow.DROP_OLDEST)

    private val _fwVersion = MutableStateFlow("")
    val fwVersion: StateFlow<String> = _fwVersion.asStateFlow()

    fun handleLine(line: String) {
        val msg = line.trim()
        if (msg.isEmpty()) return
        rawLog.trySend(msg)
        try {
            when {
                msg.startsWith("STAT:")         -> parseStat(msg.substring(5))
                msg.startsWith("US:")           -> parseUS(msg.substring(3))
                msg.startsWith("STATUS|")       -> parseStatus(msg.substring(7))
                msg.startsWith("HDG:")          -> parseHeading(msg.substring(4))
                msg.startsWith("TELE:")         -> parseTele(msg.substring(5))
                msg.startsWith("DIAG|")         -> parseDiag(msg.substring(5))
                msg.startsWith("MODE:")         -> parseModeName(msg.substring(5))?.let { _modeChange.tryEmit(it) }
                msg.startsWith("REQ_MODE:")     -> parseModeName(msg.substring(9))?.let { _modeRequest.tryEmit(it) }
                msg.startsWith("ACK|")          -> _acks.tryEmit(msg.removePrefix("ACK|").removeSuffix("|END"))
                msg.startsWith("ALERT:")        -> _alerts.tryEmit(SystemAlert(msg.substring(6), SystemAlert.Level.CRITICAL))
                msg.startsWith("EVENT:")        -> _alerts.tryEmit(SystemAlert(msg.substring(6), SystemAlert.Level.INFO))
                msg.startsWith("GESTURE:")      -> _gestures.tryEmit(msg.substring(8))
                msg.startsWith("SYSTEM|READY|") -> parseSystemReady(msg.substring(13))
                msg.startsWith("PONG_PICO:")    -> Unit
                msg.startsWith("DBG:")          -> Log.d(TAG, "Mega DBG: " + msg.substring(4))
                msg.startsWith("WIFI_IP:")      -> _wifiIps.tryEmit(msg.substring(8).trim())
                msg.startsWith("BEEP:")         -> Unit
                msg.startsWith("IR:")           -> parseIr(msg.substring(3))
                else                            -> Log.v(TAG, "Unhandled: $msg")
            }
        } catch (e: Exception) {
            Log.e(TAG, "Parse error on line '$msg'", e)
        }
    }

    // V37 STAT:gas:temp:hum:haz:pir:tilt:flame:volt:pct:amps  (10 fields)
    private fun parseStat(data: String) {
        val f = data.split(":")
        if (f.size < 10) return
        _telemetry.update {
            it.copy(
                gasLevel        = f[0].toIntOrNull()   ?: it.gasLevel,
                temperature     = f[1].toFloatOrNull() ?: it.temperature,
                humidity        = f[2].toFloatOrNull() ?: it.humidity,
                hazardDetected  = f[3] == "1",
                pirAlert        = f[4] == "1",
                tiltAlert       = f[5] == "1",
                flameAlert      = f[6] == "1",
                batteryVoltage  = f[7].toFloatOrNull() ?: it.batteryVoltage,
                batteryPercent  = f[8].toIntOrNull()   ?: it.batteryPercent,
                currentAmps     = f[9].toFloatOrNull() ?: it.currentAmps
            )
        }
    }

    // US:<front>,<rear>,<left>,<right>   -1 = sensor offline
    private fun parseUS(data: String) {
        val d = data.split(",")
        if (d.size < 4) return
        _telemetry.update {
            it.copy(
                frontDistance = d[0].toIntOrNull() ?: it.frontDistance,
                rearDistance  = d[1].toIntOrNull() ?: it.rearDistance,
                leftDistance  = d[2].toIntOrNull() ?: it.leftDistance,
                rightDistance = d[3].toIntOrNull() ?: it.rightDistance
            )
        }
    }

    // STATUS|ESTOP:x|AUTO:x|BAT:x|PCT:x|HTEMP:x|FANS:HB:x,HE:x,BD:x|UV:x|S9:x|FW:x|CHG:x|ADOCK:x|DOCKST:x
    private fun parseStatus(data: String) {
        val kv = mutableMapOf<String, String>()
        data.split("|").forEach { part ->
            val i = part.indexOf(':')
            if (i > 0) kv[part.substring(0, i)] = part.substring(i + 1)
        }
        _telemetry.update { current ->
            var t = current
            kv["ESTOP"]?.let { t = t.copy(estopActive = it == "YES" || it == "1") }
            kv["BAT"  ]?.toFloatOrNull()?.let { t = t.copy(batteryVoltage = it) }
            kv["PCT"  ]?.toIntOrNull()  ?.let { t = t.copy(batteryPercent = it) }
            kv["HTEMP"]?.toFloatOrNull()?.let { t = t.copy(headTemp = it) }
            kv["UV"   ]?.let { t = t.copy(uvActive = it == "ON") }
            kv["S9"   ]?.let { t = t.copy(s9LinkState = it) }
            kv["FW"   ]?.let { t = t.copy(firmwareVersion = it); _fwVersion.value = it }
            kv["CHG"  ]?.let { t = t.copy(chargeState = it) }
            kv["DOCKST"]?.let { t = t.copy(dockState = it) }
            kv["FANS" ]?.let { fans ->
                fans.split(",").forEach { pair ->
                    val p = pair.split(":")
                    if (p.size == 2) {
                        val v = p[1].toIntOrNull() ?: return@forEach
                        when (p[0]) {
                            "HB" -> t = t.copy(fanHeadBlow    = v)
                            "HE" -> t = t.copy(fanHeadExhaust = v)
                            "BD" -> t = t.copy(fanBody        = v)
                        }
                    }
                }
            }
            t
        }
    }

    private fun parseHeading(data: String) {
        val h = data.toFloatOrNull() ?: return
        _telemetry.update { it.copy(headingDeg = h) }
    }

    private fun parseTele(data: String) {
        val d = data.split(",")
        if (d.size < 3) return
        _telemetry.update {
            it.copy(
                batteryVoltage = d[0].toFloatOrNull() ?: it.batteryVoltage,
                batteryPercent = d[1].toIntOrNull()   ?: it.batteryPercent,
                isMoving       = d[2] == "1"
            )
        }
    }

    private fun parseDiag(data: String) {
        data.split("|").forEach { part ->
            when {
                part.startsWith("GPS:") -> {
                    val coords = part.substring(4).split(",")
                    if (coords.size == 2) {
                        val lat = coords[0].toDoubleOrNull() ?: return@forEach
                        val lon = coords[1].toDoubleOrNull() ?: return@forEach
                        _telemetry.update { it.copy(gpsLat = lat, gpsLon = lon) }
                    }
                }
                part.startsWith("SAT:") -> part.substring(4).toIntOrNull()?.let { s ->
                    _telemetry.update { it.copy(satellites = s) }
                }
                part.startsWith("UPT:") -> {
                    val u = part.substring(4).replace("s", "").toLongOrNull()
                    if (u != null) _telemetry.update { it.copy(uptimeSec = u) }
                }
            }
        }
    }

    private fun parseIr(data: String) {
        val d = data.split(",")
        if (d.isEmpty()) return
        _telemetry.update {
            it.copy(irAlert = (d.getOrNull(0) == "1") || (d.getOrNull(1) == "1"))
        }
    }

    private fun parseSystemReady(data: String) {
        val v = data.removeSuffix("|END").trim()
        _fwVersion.value = v
        _telemetry.update { it.copy(firmwareVersion = v) }
        Log.i(TAG, "Mega V37 boot: firmware=$v")
    }

    private fun parseModeName(raw: String): RobotMode? =
        when (raw.uppercase().trim()) {
            "NORMAL"    -> RobotMode.NORMAL
            "DOG"       -> RobotMode.DOG
            "BODYGUARD" -> RobotMode.BODYGUARD
            "UNHINGED"  -> RobotMode.UNHINGED
            "PARTY"     -> RobotMode.PARTY
            else        -> null
        }
}

/** System alert emitted for ALERT: and EVENT: lines. */
data class SystemAlert(val code: String, val level: Level) {
    enum class Level { INFO, WARNING, CRITICAL }
}