/*
 * ════════════════════════════════════════════════════════════
 *  BuddyBotConfig.kt — centralised configuration constants
 * ════════════════════════════════════════════════════════════
 *
 *  AI PRIORITY ORDER (cost-first):
 *    1. Groq        — FREE  (14,400 req/day on free tier) — console.groq.com
 *    2. Gemini      — FREE  (1,500 req/day on free tier)
 *    3. Claude      — PAID  (last resort)
 *    4. Offline     — hardcoded child-friendly fallback responses
 *
 *  TTS ORDER:
 *    • ElevenLabs   — premium voice (10,000 chars/month free)
 *    • Android TTS  — fallback for operational phrases (free, on-device)
 */

package com.buddybot.kids

import android.Manifest
import android.os.Build

object BuddyBotConfig {

    // ── App / child settings ─────────────────────────────────────────────
    const val EXIT_PASSCODE = "1234"
    const val PRIORITY_USER = "AJ"
    const val CHILD_NAME    = "AJ"
    const val CHILD_AGE     = 3

    // ── Hardware ─────────────────────────────────────────────────────────
    const val SERIAL_BAUD_RATE = 115200
    const val WEBSOCKET_PORT   = 81

    // ── Voice / AI ───────────────────────────────────────────────────────
    const val WAKE_WORD            = "hey buddy"
    const val SILENCE_THRESHOLD_MS = 2000L

    // Model strings
    const val CLAUDE_MODEL     = "claude-3-haiku-20240307"   // cheapest Claude tier
    const val GROQ_MODEL_FAST  = "llama-3.1-8b-instant"     // fastest Groq model
    const val GROQ_MODEL_SMART = "llama-3.3-70b-versatile"  // smarter Groq model
    const val GEMINI_URL       = "https://generativelanguage.googleapis.com/v1beta/models/gemini-2.0-flash:generateContent"
    const val ELEVENLABS_VOICE_ID = "XP4U9NPyLGdlseTzp9Hf"  // ElevenLabs voice ID for BuddyBot

    // ── BuildConfig key accessors ────────────────────────────────────────
    // These are injected at build time from secrets.properties via Gradle.
    // We use BuildConfig fields here; direct hardcoded values have been removed.

    /**
     * Returns true if the Groq API key looks like a real key (not a placeholder).
     * Groq keys are long strings; "your_groq_key_here" is only 18 chars.
     */
    val isGroqConfigured: Boolean
        get() = try {
            val k = BuildConfig.GROQ_API_KEY
            k.isNotBlank() &&
                k != "your_groq_key_here" &&
                !k.startsWith("your_") &&
                k.length > 20
        } catch (e: Exception) { false }

    /**
     * Returns true if the Gemini API key looks like a real key.
     * Real Gemini keys start with "AIzaSy" and are ~39 chars.
     */
    val isGeminiConfigured: Boolean
        get() = try {
            val k = BuildConfig.GEMINI_API_KEY
            k.isNotBlank() &&
                k.startsWith("AIzaSy") &&
                k.length >= 35
        } catch (e: Exception) { false }

    /**
     * Returns true if a Claude API key is present.
     * Claude keys start with "sk-ant-".
     */
    val isClaudeConfigured: Boolean
        get() = try {
            val k = BuildConfig.CLAUDE_API_KEY
            k.isNotBlank() &&
                k.startsWith("sk-ant-") &&
                k.length > 30
        } catch (e: Exception) { false }

    /**
     * Returns true if ElevenLabs API key is present (not default placeholder).
     */
    val isElevenLabsConfigured: Boolean
        get() = try {
            val k = BuildConfig.ELEVENLABS_API_KEY
            k.isNotBlank() && k.length > 20
        } catch (e: Exception) { false }

    // ── Video assets ─────────────────────────────────────────────────────
    const val VIDEO_BASE_PATH = "/storage/emulated/0/BuddyBot/videos/"

    val VIDEO_STATES = mapOf(
        "splash"               to "splash",
        "intro"                to "intro",
        "idle"                 to "idle",
        "dog_transition"       to "dog_transition",
        "dog_idle"             to "dog_idle",
        "party_transition"     to "party_transition",
        "unhinged_idle"        to "unhinged_idle",
        "bodyguard_transition" to "bodyguard_transition",
        "bodyguard_looking"    to "bodyguard_looking"
    )

    val AUDIO_FILES = mapOf(
        "STARTUP"              to "startup",
        "HELLO"                to "hello",
        "ALARM"                to "alarm",
        "EMERGENCY"            to "emergency",
        "HAZARD"               to "hazard",
        "INTRUDER"             to "intruder_alert",
        "BATTERY_CRITICAL"     to "battery_low",
        "OVERTEMP"             to "overheat_warning",
        "TILT"                 to "tilt_detected",
        "OBSTACLE"             to "obstacle",
        "GUARDIAN_ACTIVATED"   to "guardian_on",
        "GUARDIAN_DEACTIVATED" to "guardian_off",
        "MOTION_DETECTED"      to "motion",
        "ROAST_RANDOM"         to "roast",
        "SARCASTIC_WAVE"       to "wave",
        "BARK"                 to "bark"
    )

    // ── Required permissions ─────────────────────────────────────────────
    val REQUIRED_PERMISSIONS: Array<String> = mutableListOf(
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
