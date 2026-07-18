package com.buddybot.kids

import android.graphics.RectF

enum class RobotMode {
    NORMAL, DOG, BODYGUARD, UNHINGED, PARTY
}

enum class CommunicationMode {
    /** USB serial to Pico W bridge → Mega (primary path, V37). */
    USB_SERIAL,
    /** HTTP polling to Pico W web server (fallback when USB unavailable). */
    HTTP_PICO_W,
    /** No link. */
    DISCONNECTED;

    companion object {
        /** Legacy alias for any external code that still references WEBSOCKET. */
        @Deprecated("Use HTTP_PICO_W", ReplaceWith("HTTP_PICO_W"))
        val WEBSOCKET: CommunicationMode get() = HTTP_PICO_W
    }
}

/**
 * AI service enum — GROQ added as the default free-tier option.
 * Priority order: GROQ → GEMINI → CLAUDE → OFFLINE
 */
enum class AIService {
    GROQ,       // Free — Llama 3 via Groq API (console.groq.com)
    GEMINI,     // Free — Gemini 2.0 Flash via Google AI Studio
    CLAUDE,     // Paid — Claude Haiku / Sonnet (last resort)
    OFFLINE     // No network — hardcoded child-friendly responses
}

/**
 * Which network interface AI/TTS API calls are allowed to use.
 *
 * WIFI_ONLY    — only uses WiFi; if WiFi is down, AI goes OFFLINE.
 *                Best for home use where you don't want surprise mobile data bills.
 * MOBILE_ONLY  — forces calls through mobile data even when WiFi is available.
 *                Useful when the robot roams out of WiFi range.
 * ANY          — system default: use whatever's connected (WiFi preferred).
 *                Seamlessly falls back from WiFi → mobile data automatically.
 */
enum class NetworkPreference {
    ANY,         // System default (WiFi preferred, auto-fallback to mobile)
    WIFI_ONLY,   // WiFi only — go OFFLINE if WiFi unavailable
    MOBILE_ONLY  // Force mobile data even when WiFi is present
}

data class RobotState(
    val currentMode: RobotMode = RobotMode.NORMAL,
    val isListening: Boolean = false,
    val isProcessing: Boolean = false,
    val isSpeaking: Boolean = false,
    // Phase 4: tracks whether USB webcam is connected and open
    val isCameraConnected: Boolean = false,
    // Phase 5: live detection results for on-screen overlay
    val detectedFaces: List<FaceResult> = emptyList(),
    val detectedObjects: List<DetectedObjectResult> = emptyList(),
    val recognizedPerson: String? = null,
    val isEmergency: Boolean = false,
    val isSplashScreen: Boolean = true,
    val showIntroDialog: Boolean = true,
    val showPinEntry: Boolean = false,
    val requestedMode: RobotMode? = null,
    val showCameraFeed: Boolean = false,
    val communicationMode: CommunicationMode = CommunicationMode.DISCONNECTED,
    // Default OFFLINE — updates to GROQ/GEMINI/CLAUDE on first successful AI call
    val aiService: AIService = AIService.OFFLINE,
    // Which network to use for AI/TTS API calls (persisted in SharedPreferences)
    val networkPreference: NetworkPreference = NetworkPreference.ANY,
    val batteryVoltage: Float = 0f,
    val batteryPercent: Int = 100,
    val temperature: Float = 25f,
    val currentAmps: Float = 0f,
    val isAutonomous: Boolean = false,
    val lastGesture: String = "",
    val estopRetryCount: Int = 0,
    val buddybotIP: String = "",
    /** WiFi provisioning UI phase: "", "connecting", "connected", "failed" */
    val wifiSetupPhase: String = "",
    val gestureReactionsEnabled: Boolean = true,
    val isAutoMode: Boolean = false,
    val eventBanner: Pair<String, BannerLevel>? = null
)

// ─── Phase 5: Detection result types ────────────────────────────────────────

/**
 * Result from face detection + recognition pipeline.
 * @param bounds      Bounding box in camera pixel coordinates (640×480)
 * @param name        Recognised person name, or null if unknown
 * @param confidence  Cosine similarity score (0..1), 0 if unknown
 */
data class FaceResult(
    val bounds: RectF,
    val name: String?,
    val confidence: Float
)

/**
 * Result from object detection pipeline.
 * @param bounds      Bounding box in camera pixel coordinates (640×480)
 * @param label       Object class label (e.g. "person", "dog")
 * @param confidence  Detection confidence score (0..1)
 */
data class DetectedObjectResult(
    val bounds: RectF,
    val label: String,
    val confidence: Float
)

/** Severity level for the event banner overlay. */
enum class BannerLevel { INFO, WARNING, CRITICAL }

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
    val hazAlert: Boolean = false,
    val pirAlert: Boolean = false,
    val tiltAlert: Boolean = false,
    val flameAlert: Boolean = false,
    val irAlert: Boolean = false,
    val gpsLat: Double = 0.0,
    val gpsLon: Double = 0.0,
    val satellites: Int = 0,
    val uptimeSec: Long = 0,
    // ─── V37 additions ───────────────────────────────────────────────────
    /** Compass heading in degrees, 0–360, from Mega HDG: telemetry. */
    val headingDeg: Float = 0f,
    /** Mega V37 emergency stop state — motors disabled when true. */
    val estopActive: Boolean = false,
    /** Head blower fan PWM level 0-255, from STATUS|FANS:HB:x,HE:x,BD:x */
    val fanHeadBlow: Int = 0,
    /** Head exhaust fan PWM level 0-255. */
    val fanHeadExhaust: Int = 0,
    /** Body fan PWM level 0-255. */
    val fanBody: Int = 0,
    /** Head temperature in °C — critical for S9 thermal management. */
    val headTemp: Float = 0f,
    /** Charging state: "NO", "MANUAL", "DOCK". */
    val chargeState: String = "NO",
    /** Auto-dock state: "IDLE", "SEARCHING", "APPROACHING", "DOCKED". */
    val dockState: String = "IDLE",
    /** UV light state — true when guardian sterilization mode active. */
    val uvActive: Boolean = false,
    /** S9 link handshake state as reported by Mega — "OK", "WAIT", "TIMEOUT". */
    val s9LinkState: String = "WAIT",
    /** Firmware version reported by Mega — sanity check against EXPECTED_FW_VERSION. */
    val firmwareVersion: String = ""
)

data class EnvironmentAlert(val type: String, val message: String, val severity: Int)
