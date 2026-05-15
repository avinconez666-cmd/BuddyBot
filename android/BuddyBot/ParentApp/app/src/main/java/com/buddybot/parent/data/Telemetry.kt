package com.buddybot.parent.data

data class Telemetry(
    val battery: Int       = 0,      // percent
    val voltage: Float     = 0f,     // volts
    val temp: Float        = 0f,     // celsius
    val humidity: Float    = 0f,     // percent
    val gas: Int           = 0,
    val flame: Boolean     = false,
    val tilt: Boolean      = false,
    val pir: Boolean       = false,
    val ir: Boolean        = false,
    val estop: Boolean     = false,
    val autoMode: Boolean  = false,
    val mode: String       = "MANUAL",
    val front: Int         = -1,
    val rear: Int          = -1,
    val left: Int          = -1,
    val right: Int         = -1,
    val r3ok: Boolean      = false,
    val espOk: Boolean     = false,
    val amps: Float        = 0f,
    val fw: String         = "--",
)

sealed class BotEvent {
    data class FlameAlert(val msg: String) : BotEvent()
    data class GasAlert(val msg: String)   : BotEvent()
    data class BatLow(val msg: String)     : BotEvent()
    data class Connected(val ip: String)   : BotEvent()
    object Disconnected                    : BotEvent()
}
