/*
 * AI BACKEND FREE TIER LIMITS (as of 2026):
 *
 * GROQ (primary):
 *   - Free tier: 14,400 requests/day, 6,000 requests/minute
 *   - Model: llama-3.1-8b-instant (fastest) or llama-3.3-70b-versatile (smarter)
 *   - Sign up: console.groq.com
 *   - Cost: FREE
 *
 * GEMINI 2.0 FLASH (secondary fallback):
 *   - Free tier: 1,500 requests/day, 15 requests/minute
 *   - Model: gemini-2.0-flash
 *   - Sign up: aistudio.google.com
 *   - Cost: FREE
 *
 * CLAUDE (last resort):
 *   - No free tier - costs per token
 *   - Only used when Groq and Gemini both fail
 *   - Cost: ~$0.003 per 1K input tokens (Haiku)
 *
 * ELEVENLABS TTS (premium voice):
 *   - Free tier: 10,000 characters/month
 *   - Disabled by default - toggle in Settings
 *   - Cost: $5/month for 30,000 chars on paid plan
 *
 * ANDROID TTS (operational speech):
 *   - Completely free, on-device
 *   - Used for all operational phrases and when premium voice is OFF
 *   - Cost: FREE
 */

package com.buddybot.kids

import android.Manifest
import android.os.Build

object BuddyBotConfig {
    const val EXIT_PASSCODE = "1234"
    const val PRIORITY_USER = "AJ"
    const val CHILD_NAME = "AJ"
    const val CHILD_AGE = 3

    // AI Configuration - Dual Backend
    const val ANTHROPIC_API_KEY = "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"
    const val GEMINI_API_KEY = "AIzaSyAHl_qpH7b6iWdOQ6c4XYmhpNLYJZDUA00"
    const val CLAUDE_MODEL = "claude-3-sonnet-20240229"
    const val GEMINI_URL = "https://generativelanguage.googleapis.com/v1beta/models/gemini-1.5-flash:generateContent"

    // Voice Configuration - ElevenLabs
    const val ELEVENLABS_API_KEY = "f71d83de87e1adbc6ae322e7350a2bdb3b6a3edee14b6d9e29fa8953fca05afc"
    const val ELEVENLABS_VOICE_ID = "XP4U9NPyLGdlseTzp9Hf"

    const val SERIAL_BAUD_RATE = 115200
    const val WEBSOCKET_PORT = 81
    const val WAKE_WORD = "hey buddy"
    const val SILENCE_THRESHOLD_MS = 2000L

    const val VIDEO_BASE_PATH = "/storage/emulated/0/BuddyBot/videos/"

    val VIDEO_STATES = mapOf(
        "splash" to "splash",
        "intro" to "intro",
        "idle" to "idle",
        "dog_transition" to "dog_transition",
        "dog_idle" to "dog_idle",
        "party_transition" to "party_transition",
        "unhinged_idle" to "unhinged_idle",
        "bodyguard_transition" to "bodyguard_transition",
        "bodyguard_looking" to "bodyguard_looking"
    )

    val AUDIO_FILES = mapOf(
        "STARTUP" to "startup",
        "HELLO" to "hello",
        "ALARM" to "alarm",
        "EMERGENCY" to "emergency",
        "HAZARD" to "hazard",
        "INTRUDER" to "intruder_alert",
        "BATTERY_CRITICAL" to "battery_low",
        "OVERTEMP" to "overheat_warning",
        "TILT" to "tilt_detected",
        "OBSTACLE" to "obstacle",
        "GUARDIAN_ACTIVATED" to "guardian_on",
        "GUARDIAN_DEACTIVATED" to "guardian_off",
        "MOTION_DETECTED" to "motion",
        "ROAST_RANDOM" to "roast",
        "SARCASTIC_WAVE" to "wave",
        "BARK" to "bark"
    )

    val REQUIRED_PERMISSIONS = mutableListOf(
        Manifest.permission.CAMERA,
        Manifest.permission.RECORD_AUDIO,
        Manifest.permission.INTERNET,
        Manifest.permission.ACCESS_NETWORK_STATE,
        Manifest.permission.CALL_PHONE,
        Manifest.permission.VIBRATE,
        Manifest.permission.WAKE_LOCK
    ).apply {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            add(Manifest.permission.POST_NOTIFICATIONS)
            add(Manifest.permission.READ_MEDIA_VIDEO)
            add(Manifest.permission.READ_MEDIA_AUDIO)
            add(Manifest.permission.READ_MEDIA_IMAGES)
        } else {
            add(Manifest.permission.READ_EXTERNAL_STORAGE)
            add(Manifest.permission.WRITE_EXTERNAL_STORAGE)
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.UPSIDE_DOWN_CAKE) {
            add(Manifest.permission.FOREGROUND_SERVICE_MICROPHONE)
        }
    }.toTypedArray()
}
