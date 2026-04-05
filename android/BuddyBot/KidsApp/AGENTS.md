# 🤖 AGENTS.md — BuddyBot Kids AI Coding Guide

**For AI agents working on this Android codebase**

---

## 📋 Project Overview

**BuddyBot Kids** is an AI-powered robot companion Android app (Kotlin) that runs on a Samsung Galaxy S9, featuring computer vision (ML Kit + TensorFlow Lite), voice AI (Claude via Anthropic), text-to-speech (ElevenLabs), and 24/7 child safety monitoring.

**Key constraint:** This is a production-ready app with real-time child safety requirements—understand the monitoring architecture and permission model before making changes.

---

## 🏗️ Critical Architecture Patterns

### 1. **State Management → RobotState (Single Source of Truth)**
- **File:** `Models.kt` defines the `RobotState` data class
- **Pattern:** Not Redux/MVI, but shared mutable state via Compose
- **Key fields:** `currentMode` (NORMAL|DOG|BODYGUARD|UNHINGED|PARTY), `isListening`, `isProcessing`, `isSpeaking`, `recognizedPerson`, `isEmergency`
- **When modifying state:** Use `mutableStateOf()` in Compose to ensure UI recomposition. State changes in `MainActivity` must emit properly.
- **Example:** Switching robot modes requires updating both `currentMode` AND notifying the video player via `FaceCoordinator`

### 2. **Multi-Service Architecture**
The app doesn't use a single monolithic Activity. Three key coordinators:

**MainActivity** (931 lines)
- Handles USB camera setup, speech recognition, AI calls
- Manages foreground service lifecycle
- Triggers notifications and parent alerts
- **Don't:** Add complex business logic here—extract to `*Coordinator` or `*Manager` classes

**EnvironmentMonitoringService** (382 lines)
- Runs as Android Service (background foreground service on API 14+)
- 24/7 audio monitoring with keyword detection + volume analysis
- Handles notification cooldown (60s between similar alerts)
- **Key:** Runs in background INDEPENDENTLY of MainActivity lifecycle
- **When adding monitoring:** Add keywords to `SWEAR_WORDS`, `DISTRESS_PHRASES`, or `ARGUMENT_PHRASES` sets

**FaceCoordinator** (95 lines)
- Manages ExoPlayer for video animations (idle, speaking, mode transitions)
- Handles 4 robot personality videos per mode (idle, talk, transition, warning)
- Video paths: `android.resource://com.buddybot.kids/raw/{mode_name}_{video_type}`
- **Important:** Player volume is always `0f` (muted) — audio comes from TTS/MediaPlayer separately

### 3. **Dual-Backend AI Architecture**
- **Primary:** Claude Sonnet (Anthropic) via `ANTHROPIC_API_KEY` from `BuddyBotConfig.kt`
- **Fallback:** Google Gemini (rarely used)
- **API calls:** Use OkHttpClient; see `MainActivity` for POST pattern to Anthropic
- **Config values:** Stored in `BuddyBotConfig.kt` (API keys, voice IDs, model names)

### 4. **ML Pipeline: Face Recognition → Stranger Detection**
- **FaceRecognitionManager.kt:** Detects faces using ML Kit, compares embeddings via TensorFlow Lite
- **ObjectDetectionManager.kt:** Parallel detection of toys, people, pets
- **Flow:** USB webcam → CameraX → ML Kit detection → TensorFlow embedding → similarity check (60% threshold)
- **Integration:** Called from `MainActivity` via `FaceCoordinator`; unknown faces trigger parent notifications

### 5. **Permission & Security Model**
- **Runtime permissions:** Camera, Microphone, Phone (for emergency calls) all requested at startup
- **SecurityGatekeeper.kt:** Validates PIN (`1234`) for mode changes via Arduino serial protocol: `REQ_MODE_CHANGE:BODYGUARD:1234`
- **API keys:** NEVER hardcoded—injected via Gradle `buildConfigField` from `secrets.properties`
- **Critical:** Foreground service requires `FOREGROUND_SERVICE_MICROPHONE` permission on API 34+ (see `AndroidManifest.xml`)

---

## 🔧 Critical Developer Workflows

### Build & Run
```bash
# First time setup: Copy and edit secrets.properties
cp secrets.properties.template secrets.properties  # Fill in API keys

# Build from command line
./gradlew.bat assembleDebug

# Run on device
./gradlew.bat installDebug
adb shell am start -n com.buddybot.kids/.MainActivity
```

### Debugging Specific Subsystems
```bash
# 1. Environment monitoring (audio detection)
adb logcat -s "EnvironmentMonitor" | grep -E "SWEAR|DISTRESS|ARGUMENT|YELLING|SILENCE"

# 2. Face recognition + stranger detection
adb logcat -s "BuddyBotMainActivity" | grep "Face\|Stranger"

# 3. USB/Serial communication with Arduino
adb logcat -s "BuddyBotMainActivity" | grep "USB\|Serial"

# 4. Firebase messaging / parent notifications
adb logcat -s "FCM" | grep "Message\|Token"
```

### Common Build Fixes
- **Compose compiler error:** Must match Kotlin version. Currently: Kotlin 2.2.10 + Compose compiler 2.2.10 (NOT the older 1.5.10)
- **USB permission not granted:** Check `AndroidManifest.xml` for `FOREGROUND_SERVICE_MICROPHONE` and camera/mic permissions
- **Video files not found:** Video asset names are lowercase with underscores: `{mode_name}_{video_type}.mp4` (e.g., `dog_idle.mp4`)

---

## 🎯 Key Conventions & Patterns

### 1. **Async Patterns → Coroutines + Flow**
- Use `lifecycleScope.launch(Dispatchers.Main)` for UI updates
- API calls happen in `Dispatchers.IO` (OkHttpClient is thread-safe)
- `EnvironmentMonitoringService` uses `CoroutineScope(Dispatchers.IO + SupervisorJob())` for independent background tasks
- **Don't:** Mix callbacks with coroutines unnecessarily

### 2. **Notification Cooldown Pattern**
```kotlin
// From EnvironmentMonitoringService:
private val notificationCooldown = 60000L
private var lastNotificationTime = 0L
if (System.currentTimeMillis() - lastNotificationTime > notificationCooldown) {
    // Send notification
    lastNotificationTime = System.currentTimeMillis()
}
```
**Apply to:** Stranger detection (5-minute cooldown), any repeated alert types

### 3. **Resource Loading Pattern (Video/Audio)**
```kotlin
// From FaceCoordinator:
val resId = context.resources.getIdentifier(assetName, "raw", context.packageName)
if (resId != 0) {
    val uri = Uri.parse("android.resource://${context.packageName}/$resId")
    // Load via MediaItem or MediaPlayer
}
```
**Asset locations:** 
- Videos: `app/src/main/res/raw/{name}.mp4`
- Audio: `app/src/main/res/raw/{name}.mp3`

### 4. **Error Handling in API Calls**
```kotlin
// Pattern from MainActivity (simplified):
try {
    val response = httpClient.newCall(request).execute()
    if (!response.isSuccessful) throw Exception("API error: ${response.code}")
    val body = response.body?.string() ?: ""
    val json = JSONObject(body)
    // Process response
} catch (e: Exception) {
    Log.e(TAG, "API call failed", e)
    // Fallback or retry
}
```
Always wrap OkHttp calls in try-catch. Always check `response.isSuccessful` before parsing.

### 5. **Jetpack Compose State Updates**
```kotlin
// Pattern from MainActivity:
val robotState = remember { mutableStateOf(RobotState()) }
LaunchedEffect(robotState.value.currentMode) {
    faceCoordinator.setRobotMode(robotState.value.currentMode, oldMode)
}
```
Don't directly mutate state—use `copy()` on data classes:
```kotlin
robotState.value = robotState.value.copy(isListening = true)
```

---

## 🚨 Safety-Critical Components (Handle with Care)

### 1. **Environment Monitoring Service**
- Runs as `startForeground()` (can't be killed by OS)
- Keyword detection is case-insensitive
- **Child distress detection (HIGHEST PRIORITY):** Phrases bypass cooldown and trigger immediate urgent notifications
- **When adding keywords:** Test with realistic audio samples first

### 2. **Face Recognition**
- Unknown faces (similarity < 60%) → parent notification
- **Race condition risk:** Multiple concurrent detections can duplicate alerts
- Use `AtomicBoolean` or lock for critical sections (see `MainActivity`)

### 3. **Parent Notifications (Firebase)**
- Severity levels: 9/10 (child distress), 8/10 (stranger), 6/10 (swearing), 5/10 (silence)
- **When changing thresholds or keywords:** Update both `BuddyBotConfig.kt` AND notification severity mapping

### 4. **Arduino Serial Communication**
- Baud rate: 115200 (hardcoded in `BuddyBotConfig.SERIAL_BAUD_RATE`)
- Protocol: "REQ_MODE_CHANGE:BODYGUARD:1234" (PIN-protected)
- **Never trust user input** from serial—always validate via `SecurityGatekeeper`

---

## 📁 Critical File Reference

| File | Purpose | When to Edit |
|------|---------|-------------|
| `MainActivity.kt` | App entry point, USB camera, speech, AI calls | Speech pipeline, camera setup |
| `Models.kt` | `RobotState`, `RobotMode` enums | Adding new state fields or robot modes |
| `BuddyBotConfig.kt` | All config constants (API keys, thresholds) | Changing keywords, thresholds, model versions |
| `EnvironmentMonitoringService.kt` | Audio monitoring, phrase detection | Adding detection types (keywords, volume rules) |
| `FaceCoordinator.kt` | Video playback & mode transitions | Adding new animation videos or modes |
| `FaceRecognitionManager.kt` | ML Kit + TensorFlow face detection | Tuning similarity threshold or embedding model |
| `SecurityGatekeeper.kt` | PIN validation for mode changes | Changing PIN or adding new restricted modes |
| `app/build.gradle` | Dependencies, compose compiler version | Adding libraries (check transitive deps!) |
| `secrets.properties` | API keys (NEVER commit) | LOCAL ONLY—use template for setup |

---

## ⚡ Quick Troubleshooting

| Problem | Likely Cause | Fix |
|---------|-------------|-----|
| Compose compiler error | Kotlin/Compose mismatch | Check `build.gradle`: Kotlin 2.2.10 requires Compose compiler 2.2.10 |
| USB webcam not detected | Missing `device_filter.xml` | Add to `app/src/main/res/xml/` |
| Video assets not playing | Wrong filename or path | Asset names are lowercase; check `FaceCoordinator.playVideo()` |
| Notifications not received | FCM token not registered | Ensure `BuddyBotMessagingService.onNewToken()` persists token to backend |
| "Access Denied" serial messages | PIN mismatch or baud rate | Verify Arduino is sending correct format; check `SERIAL_BAUD_RATE` |
| Memory leak on repeated calls | MediaPlayer not released | Always call `.release()` in `onDestroy()` or when switching modes |

---

## 🔌 Integration Points (External Dependencies)

1. **Anthropic Claude API** — Speech understanding + response generation
2. **ElevenLabs TTS** — Voice output via HTTP API
3. **Firebase Cloud Messaging** — Parent push notifications
4. **ML Kit + TensorFlow Lite** — On-device face/object detection
5. **Arduino (USB Serial)** — Robot hardware control
6. **Facebook Messenger** — Video call integration

**Pattern:** All external API calls go through OkHttpClient in `MainActivity`. Add logging before each call for debugging.

---

## 🎓 Best Practices for This Codebase

1. **Always** use `lifecycleScope` for Activity-scoped coroutines; use `scope` property for Service-scoped
2. **Never** block the main thread with audio recording, API calls, or ML inference
3. **Always** validate API responses before parsing JSON
4. **Always** check permissions at runtime (even if in `AndroidManifest.xml`)
5. **Never** hardcode API keys—use `BuddyBotConfig` or `buildConfigField`
6. **Always** add keyword/threshold changes to `BuddyBotConfig.kt` for central management
7. **Never** skip try-catch on OkHttp calls or JSON parsing
8. **Always** test environment monitoring keywords with realistic audio first

---

**Generated:** 2026-03-28  
**For:** AI agents working on BuddyBot Kids (Android, Kotlin, Compose)  
**Stability:** Production-ready; assume high impact from changes to safety-critical components

