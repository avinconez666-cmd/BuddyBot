package com.buddybot.kids

enum class RobotMode {
    NORMAL, DOG, BODYGUARD, UNHINGED, PARTY
}

enum class CommunicationMode {
    USB_SERIAL, WEBSOCKET, DISCONNECTED
}

enum class AIService {
    CLAUDE, GEMINI, OFFLINE
}

data class RobotState(
    val currentMode: RobotMode = RobotMode.NORMAL,
    val isListening: Boolean = false,
    val isProcessing: Boolean = false,
    val isSpeaking: Boolean = false,
    val recognizedPerson: String? = null,
    val isEmergency: Boolean = false,
    val isSplashScreen: Boolean = true,
    val showIntroDialog: Boolean = true,
    val showPinEntry: Boolean = false,
    val requestedMode: RobotMode? = null,
    val showCameraFeed: Boolean = false,
    val communicationMode: CommunicationMode = CommunicationMode.DISCONNECTED,
    val aiService: AIService = AIService.CLAUDE,
    val batteryVoltage: Float = 0f,
    val batteryPercent: Int = 100,
    val temperature: Float = 25f,
    val currentAmps: Float = 0f,
    val isAutonomous: Boolean = false,
    val lastGesture: String = "",
    val estopRetryCount: Int = 0,
    val buddybotIP: String = ""
)

data class ConversationMessage(
    val role: String,
    val content: String,
    val timestamp: Long = System.currentTimeMillis()
)

data class TelemetryData(
    val batteryVoltage: Float = 0f,
    val batteryPercent: Int = 100,
    val batteryTemp: Float = 25f,
    val temperature: Float = 25f,
    val humidity: Float = 50f,
    val currentAmps: Float = 0f,
    val powerWatts: Float = 0f,
    val frontDistance: Int = -1,
    val rearDistance: Int = -1,
    val leftDistance: Int = -1,
    val rightDistance: Int = -1,
    val isMoving: Boolean = false,
    val isAvoiding: Boolean = false,
    val hazardDetected: Boolean = false,
    val gasLevel: Int = 0,
    val lightLevel: Int = 500,
    val latitude: Double = 0.0,
    val longitude: Double = 0.0,
    val satellites: Int = 0,
    val uptime: Long = 0
)

data class EnvironmentAlert(val type: String, val message: String, val severity: Int)
