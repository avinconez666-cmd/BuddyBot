package com.buddybot.parent.data

import kotlinx.coroutines.*
import kotlinx.coroutines.flow.*
import okhttp3.*
import org.json.JSONObject
import java.io.IOException
import java.util.concurrent.TimeUnit

class BotRepository {

    private val client = OkHttpClient.Builder()
        .connectTimeout(3, TimeUnit.SECONDS)
        .readTimeout(4, TimeUnit.SECONDS)
        .writeTimeout(3, TimeUnit.SECONDS)
        .build()

    private val _telemetry = MutableStateFlow(Telemetry())
    val telemetry: StateFlow<Telemetry> = _telemetry.asStateFlow()

    private val _connected = MutableStateFlow(false)
    val connected: StateFlow<Boolean> = _connected.asStateFlow()

    private val _events = MutableSharedFlow<BotEvent>(extraBufferCapacity = 8)
    val events: SharedFlow<BotEvent> = _events.asSharedFlow()

    private var pollJob: Job? = null
    private var lastBat = 100
    private var lastGas = 0
    private var lastFlame = false

    fun startPolling(ip: String, scope: CoroutineScope) {
        pollJob?.cancel()
        pollJob = scope.launch(Dispatchers.IO) {
            while (isActive) {
                try {
                    val json = get("http://$ip/status")
                    val t = parse(json)
                    _connected.value = true
                    _telemetry.value = t
                    checkAlerts(t)
                } catch (e: Exception) {
                    _connected.value = false
                }
                delay(900)
            }
        }
    }

    fun stopPolling() { pollJob?.cancel() }

    suspend fun sendCmd(ip: String, cmd: String) = withContext(Dispatchers.IO) {
        try {
            val url = "http://$ip/cmd?c=${java.net.URLEncoder.encode(cmd, "UTF-8")}"
            val req = Request.Builder().url(url).get().build()
            client.newCall(req).execute().close()
        } catch (_: Exception) {}
    }

    private fun get(url: String): String {
        val req = Request.Builder().url(url).get().build()
        val resp = client.newCall(req).execute()
        val body = resp.body?.string() ?: throw IOException("Empty body")
        resp.close()
        return body
    }

    private fun parse(json: String): Telemetry {
        val j = JSONObject(json)
        val batStr = j.optString("battery", "0")
        val voltage = batStr.toFloatOrNull() ?: 0f
        val pct = ((voltage / 8.4f) * 100).toInt().coerceIn(0, 100)
        return Telemetry(
            battery  = pct,
            voltage  = voltage,
            temp     = j.optString("temp", "0").toFloatOrNull() ?: 0f,
            humidity = j.optString("humidity", "0").toFloatOrNull() ?: 0f,
            gas      = j.optString("gas", "0").toIntOrNull() ?: 0,
            flame    = j.optString("flame", "0") == "1",
            estop    = j.optString("status", "").contains("ESTOP"),
            autoMode = j.optString("mode", "") == "AUTO",
            mode     = j.optString("mode", "MANUAL"),
            front    = j.optString("front", "-1").toIntOrNull() ?: -1,
            rear     = j.optString("rear",  "-1").toIntOrNull() ?: -1,
            left     = j.optString("left",  "-1").toIntOrNull() ?: -1,
            right    = j.optString("right", "-1").toIntOrNull() ?: -1,
            r3ok     = true,
            espOk    = true,
        )
    }

    private fun checkAlerts(t: Telemetry) {
        if (t.flame && !lastFlame)
            _events.tryEmit(BotEvent.FlameAlert("FLAME DETECTED — CHECK SURROUNDINGS"))
        if (t.gas > 400 && lastGas <= 400)
            _events.tryEmit(BotEvent.GasAlert("HIGH GAS LEVEL DETECTED"))
        if (t.battery < 20 && lastBat >= 20)
            _events.tryEmit(BotEvent.BatLow("BATTERY CRITICAL — CHARGE NOW"))
        lastFlame = t.flame
        lastGas   = t.gas
        lastBat   = t.battery
    }
}
