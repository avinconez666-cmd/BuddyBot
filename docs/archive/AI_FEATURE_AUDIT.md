# BuddyBot AI & Autonomous Feature Audit
**File:** `android/BuddyBot/KidsApp/app/src/main/java/com/buddybot/kids/MainActivity.kt`  
**Date:** 2026-04-11  
**Method:** Full line-by-line read + regex search across all feature areas

---

## Legend
| Status | Meaning |
|--------|---------|
| ✅ **WORKING** | Fully implemented end-to-end |
| ⚠️ **PARTIAL** | Core path works but edge cases / sub-features are missing |
| 🔧 **STUB** | Function exists, body is empty or a single comment |
| ❌ **BROKEN** | Code exists but has a structural defect that prevents it working |

---

## 1. Voice Commands — Speech Recognition

**Supposed to do:** Always-on wake-word listening ("Buddy" or configured word). On wake, switches to command mode, captures full utterance, routes to motor commands or AI fallback.

**Status: ⚠️ PARTIAL**

| Sub-feature | Lines | Status | Notes |
|-------------|-------|--------|-------|
| Continuous wake-word loop | 977–985 | ✅ | `startContinuousListening()` restarts on error/result |
| Wake-word detection | 1011–1023 | ✅ | `BuddyBotConfig.WAKE_WORD` checked in `handleSpeechResult` |
| Command capture after wake | 1029–1036 | ✅ | `startCommandListening()` starts a new recognition session |
| Silence detection to end command | 1038–1047 | ✅ | 500 ms polling loop via `silenceHandler` |
| Motor commands via voice | 1056–1086 | ✅ | forward/back/left/right/stop/dance/auto all mapped |
| AI fallback for unknown commands | 1085 | ✅ | `else -> speakText(getAIResponse(command))` |
| BODYGUARD voice lock (only AJ) | 1013–1017 | ✅ | Refuses wake if `recognizedPerson != PRIORITY_USER` |
| DOG mode voice: "stop" | 999–1003 | ✅ | Sets `isDogFollowing = false` |
| DOG mode voice: "patrol" | 1005–1008 | ✅ | Calls `startPatrol()` |
| Tap-to-listen (manual trigger) | 637, 987–994 | ✅ | `onTap` → `startListening()` |

**What's missing:**
- No `AUTO:OFF` voice command — only `AUTO:ON` is mapped (line 1082). There is no "stop auto" or "manual" voice path.
- No volume/sensitivity control.
- `SpeechRecognizer` is created on the main thread (line 940) but never checked for null before `startListening` — if `createSpeechRecognizer` returns null the app silently stops listening with no retry.

---

## 2. Text-to-Speech Responses

**Supposed to do:** Speak robot responses using ElevenLabs for personality phrases, falling back to Android TTS for operational phrases to save quota.

**Status: ✅ WORKING** (with one caveat)

| Sub-feature | Lines | Status | Notes |
|-------------|-------|--------|-------|
| Local Android TTS (operational) | 1218–1223 | ✅ | Used for movement confirmations, "Yeah?", etc. |
| ElevenLabs synthesis | 1242–1270 | ✅ | HTTP POST, saves to cache, plays via MediaPlayer |
| ElevenLabs → TTS fallback | 1228–1231 | ✅ | Catches exception, falls back gracefully |
| `operationalPhrases` set | 1201–1207 | ✅ | 22 phrases routed to local TTS |
| `isSpeaking` state flag | 1216, 1237 | ✅ | Set/cleared around speech, used to suppress ML reactions |
| Word limiter (15 words max) | 1119–1126 | ✅ | Applied to both Claude and Gemini responses |

**What's missing:**
- `delay(1500)` after local TTS (line 1223) is a fixed blind wait — if the phrase is longer than 1.5 s, `isSpeaking` clears too early and the robot may react to its own voice via the microphone.
- ElevenLabs audio is written to a single shared file `speech.mp3` (line 1262). Concurrent calls would corrupt it. No mutex guards this.
- `MediaPlayer` in `playAudioFile` (line 1273) is created without `prepare()` error handling — a corrupt cache file would crash silently.

---

## 3. Face Detection and Tracking

**Supposed to do:** Detect faces in every USB camera frame, normalise centre coordinates to 0–1000 scale, send `FACE:x,y` to Mega (throttled 200 ms), recognise known faces by name.

**Status: ⚠️ PARTIAL**

| Sub-feature | Lines | Status | Notes |
|-------------|-------|--------|-------|
| Frame capture from USB camera | 738–742 | ✅ | `IPreviewDataCallBack` with null guard |
| Frame size validation | 795–800 | ✅ | Rejects frames < 460,800 bytes |
| `InputImage` creation | 801 | ✅ | NV21 format, 640×480 |
| Face detection call | 810 | ✅ | `faceRecognitionManager.detectAndRecognizeFaces(image)` |
| Coordinate normalisation | 822–824 | ✅ | Scaled to 0–1000 |
| `FACE:x,y` send to Mega | 825 | ✅ | Throttled to 200 ms (fixed in previous session) |
| Named face recognition | 827–828 | ✅ | `result.name != null` → `onFaceRecognized()` |
| `recognizedPerson` state update | 932 | ✅ | Stored in `_robotState` |
| BODYGUARD mode suppresses FACE: | 817 | ✅ | `if (currentMode != BODYGUARD)` gate |
| Face tracking (move robot to follow) | — | ❌ | `FACE:x,y` is sent to Mega but **Mega firmware must act on it** — no follow logic exists in MainActivity. Whether the Mega actually steers toward the face depends entirely on firmware. |
| DOG mode: bark at unknown face | 935 | ✅ | `if (name == "UNKNOWN") dogBark()` |

**What's missing:**
- No confidence threshold filter — every detected face, no matter how low confidence, triggers a send.
- `FaceRecognitionManager` is a separate class not audited here; its internal recognition accuracy is unknown.
- Face tracking (servo/motor steering) is delegated to Mega firmware — MainActivity only sends coordinates.

---

## 4. Object Detection and Classification

**Supposed to do:** Detect objects in every camera frame, send `OBJ:label,confidence` to Mega (throttled 200 ms).

**Status: ⚠️ PARTIAL**

| Sub-feature | Lines | Status | Notes |
|-------------|-------|--------|-------|
| Object detection call | 836 | ✅ | `objectDetectionManager.detectObjects(image)` |
| Label sanitisation | 845 | ✅ | Commas and spaces replaced |
| Confidence formatting | 846 | ✅ | 2 decimal places |
| `OBJ:label,confidence` send | 847 | ✅ | Throttled to 200 ms (fixed in previous session) |
| React to detected objects in app | — | ❌ | **Nothing in MainActivity acts on detected objects.** The label is sent to Mega but there is no in-app behaviour (no speech, no mode change, no alert) triggered by object type. |
| Hazard object detection | — | ❌ | `HAZARD` audio file exists (line 89) but is **never played** in response to any detected object. No code maps object labels to hazard alerts. |

**What's missing:**
- No in-app reaction to object labels (e.g., "knife" → alert, "ball" → speak).
- `HAZARD` audio asset is dead code — nothing triggers it.
- No minimum confidence threshold before sending to Mega.

---

## 5. Gesture Recognition (GESTURE: messages from Mega)

**Supposed to do:** Receive `GESTURE:UP/DOWN/LEFT/RIGHT/CLOCKWISE/COUNTERCLOCKWISE` from Mega, speak a response, play a face animation, send a motor command back.

**Status: ⚠️ PARTIAL**

| Sub-feature | Lines | Status | Notes |
|-------------|-------|--------|-------|
| GESTURE: message routing | 341–342 | ✅ | `handleGesture(msg)` called from `handleArduinoMessage` |
| UP → speak + motor | 363–374 | ✅ | `speakText("Up up up!")` + `MOTOR:UP_WAVE` |
| DOWN → speak + motor | 375–385 | ✅ | `speakText("Down down!")` + `MOTOR:DOWN_BOUNCE` |
| LEFT → speak + motor | 386–396 | ✅ | `speakText("Left turn!")` + `MOTOR:L` |
| RIGHT → speak + motor | 397–407 | ✅ | `speakText("Right turn!")` + `MOTOR:R` |
| CLOCKWISE → speak + motor | 408–418 | ✅ | `speakText("Spinning around!")` + `MOTOR:DANCE` |
| COUNTERCLOCKWISE → speak + motor | 419–429 | ✅ | `speakText("Spinning the other way!")` + `MOTOR:DANCE` |
| Face animations for gestures | 366–372, 378–384, etc. | ⚠️ | `faceCoordinator.playVideoOnce("gesture_up")` etc. are called but wrapped in `try/catch` with a `Log.w` — if the video assets don't exist the animation silently fails. |

**What's missing:**
- `MOTOR:UP_WAVE` and `MOTOR:DOWN_BOUNCE` are sent to Mega but these are non-standard commands — whether the Mega firmware handles them is unknown from this file alone.
- CLOCKWISE and COUNTERCLOCKWISE both send `MOTOR:DANCE` — they are identical in effect.
- No gesture-triggered mode changes or AI responses.

---

## 6. Auto Mode (AUTO:ON / AUTO:OFF)

**Supposed to do:** Toggle the Mega's autonomous navigation on/off.

**Status: ⚠️ PARTIAL**

| Sub-feature | Lines | Status | Notes |
|-------------|-------|--------|-------|
| `AUTO:ON` via voice | 1082 | ✅ | Sent when command contains "auto" |
| `AUTO:OFF` via voice | — | ❌ | **Not implemented.** No voice path sends `AUTO:OFF`. |
| `AUTO:ON` via settings/UI | — | ❌ | No button in SettingsMenu or BuddyBotOverlay for auto mode. |
| State tracking of auto mode | — | ❌ | No `isAutoMode` flag in `RobotState` — the app has no idea if auto is on or off after sending the command. |

**What's missing:**
- `AUTO:OFF` command entirely absent.
- No UI toggle for auto mode.
- No state variable to track whether auto is currently active.

---

## 7. DOG Mode

**Supposed to do:** Robot follows AJ, barks at unknown faces, can patrol (record video + emergency on sudden obstacle change), responds to "stop" and "patrol" voice commands.

**Status: ⚠️ PARTIAL**

| Sub-feature | Lines | Status | Notes |
|-------------|-------|--------|-------|
| Mode entry | 846–863 | ✅ | `setRobotMode(DOG)` sends `MODE:DOG` to Mega |
| Mode exit cleanup | 859–862 | ✅ | Resets `isPatrolling`, `isDogFollowing` |
| Bark at unknown face | 935 | ✅ | `dogBark()` → `playAudioCommand("BARK")` |
| Voice "stop" → stop following | 999–1003 | ✅ | Sets `isDogFollowing = false` |
| Voice "patrol" → start patrol | 1005–1008 | ✅ | Calls `startPatrol()` |
| Patrol: alarm + video record | 875–886 | ✅ | `playAudioCommand("ALARM")` + `captureVideoStart` |
| Patrol: obstacle delta → emergency | 442–451 | ✅ | `fDelta > 50` → `activateEmergencyMode()` + video |
| `startDogBehavior()` | 865–868 | 🔧 **STUB** | Function exists, body is `// Dog behavior is largely face/sensor driven now` — **does nothing** |
| Dog follow motor commands | — | ❌ | `isDogFollowing = true` is set but **no motor commands are sent** to make the robot actually follow. Following is entirely dependent on Mega firmware reacting to `FACE:x,y`. |
| `NOTIFY:PATROL_START` | 879 | ⚠️ | Sent to Mega but whether Mega handles this token is unknown from this file. |

**What's missing:**
- `startDogBehavior()` is a complete stub — it is called nowhere and does nothing.
- No `isDogFollowing` → motor command mapping in MainActivity. The app sets the flag but never acts on it locally.
- No patrol stop mechanism — once `isPatrolling = true` there is no voice command or timer to set it back to `false` (only mode change resets it).

---

## 8. PARTY Mode

**Supposed to do:** Robot dances and plays startup audio.

**Status: ⚠️ PARTIAL**

| Sub-feature | Lines | Status | Notes |
|-------------|-------|--------|-------|
| Mode entry | 854–857 | ✅ | Sends `MOTOR:DANCE` + `playAudioCommand("STARTUP")` |
| Continuous party behaviour | — | ❌ | **No loop or timer.** Dance command is sent once on mode entry. After that, nothing. |
| Face animation for party | — | ❌ | No `faceCoordinator` call in PARTY mode entry. |

**What's missing:**
- No repeating dance/audio loop while in PARTY mode.
- No party-specific face animation.
- No way to exit PARTY mode via voice.

---

## 9. BODYGUARD Mode

**Supposed to do:** Only respond to the priority user (AJ). Warn and send `KEEP_DISTANCE` if an unknown person comes within 30 cm. Lock voice commands to recognised user.

**Status: ⚠️ PARTIAL**

| Sub-feature | Lines | Status | Notes |
|-------------|-------|--------|-------|
| Voice lock to priority user | 1013–1017 | ✅ | Refuses wake word if `recognizedPerson != PRIORITY_USER` |
| Proximity warning (ultrasonic) | 454–459 | ✅ | `f < 30 && person != "AJ" && person != "Parent"` → `triggerWarning()` + `KEEP_DISTANCE` |
| `FACE:x,y` suppressed | 817 | ✅ | Face coordinates not sent to Mega in BODYGUARD mode |
| Face recognition in BODYGUARD | 827–830 | ⚠️ | Comment says "Triggers bodyguard logic via parseUltrasonicData based on proximity" — but the face result is **not used** to set `recognizedPerson` in BODYGUARD mode. `onFaceRecognized` is only called when `result.name != null`, which is correct, but the BODYGUARD proximity check reads `_robotState.value.recognizedPerson` which may be stale. |
| Alert/alarm on intruder | — | ❌ | `INTRUDER` audio asset exists (line 90) but is **never played** in BODYGUARD mode. `triggerWarning()` is called on `faceCoordinator` but no audio alert fires. |
| Record intruder video | — | ❌ | No `captureVideoStart` in BODYGUARD proximity path (unlike DOG patrol). |

**What's missing:**
- `INTRUDER` audio is dead code — nothing plays it.
- No video recording when an unknown person is detected close.
- `recognizedPerson` staleness: if the face leaves frame, the last known name persists and the proximity check may incorrectly allow or deny.

---

## 10. UNHINGED Mode

**Supposed to do:** Robot spontaneously says random unhinged jokes/comments every 5–15 seconds via AI.

**Status: ✅ WORKING**

| Sub-feature | Lines | Status | Notes |
|-------------|-------|--------|-------|
| Random AI speech loop | 571–575 | ✅ | `delay((5000..15000).random())` → `speakText(getAIResponse(...))` |
| Prompt content | 574 | ✅ | "Tell me a random short unhinged joke or comment." |
| Guard: not speaking, not processing | 571 | ✅ | Checks `!isProcessingCommand && !isSpeaking` |

**What's missing:**
- No UNHINGED-specific face animation — uses whatever the current face loop is.
- No motor behaviour in UNHINGED mode (original design had `HEAD:JITTER` but servo hardware was removed).

---

## 11. NORMAL Mode — Alive Behaviour

**Supposed to do:** Periodic idle behaviour (originally random head movements) to make the robot feel alive.

**Status: 🔧 STUB**

| Sub-feature | Lines | Status | Notes |
|-------------|-------|--------|-------|
| Alive behaviour loop | 562–579 | ✅ | Coroutine runs, delay 10–30 s |
| Actual behaviour | 568 | 🔧 **STUB** | `// HEAD:RANDOM command removed — no servo hardware` — **the loop runs but does nothing in NORMAL mode** |

**What's missing:**
- No replacement behaviour for NORMAL mode idle (e.g., random face animation, random speech, random motor wiggle).

---

## 12. Sensor-Based Reactions

### 12a. Battery Warning

**Status: ✅ WORKING**

| Sub-feature | Lines | Notes |
|-------------|-------|-------|
| Low battery TTS (< 15%) | 320–322 | Speaks "My battery is getting very low" |
| `BATTERY_WARN` event from Mega | 329–332 | Plays `BATTERY_CRITICAL` audio + speaks |
| `BATTERY_CRITICAL` audio asset | 91 | Mapped to `battery_low` raw resource |

### 12b. Obstacle Detection

**Status: ⚠️ PARTIAL**

| Sub-feature | Lines | Notes |
|-------------|-------|-------|
| `EVENT:OBSTACLE` from Mega | 326 | Plays `obstacle.mp3` audio |
| Patrol obstacle delta (DOG mode) | 442–451 | `fDelta > 50` → emergency + video |
| Obstacle avoidance motor commands | — | ❌ Not implemented in MainActivity — delegated entirely to Mega firmware |

### 12c. Tilt Detection

**Status: ⚠️ PARTIAL**

| Sub-feature | Lines | Notes |
|-------------|-------|-------|
| `EVENT:TILT` from Mega | 327 | Plays `tilt_detected` audio |
| App-side tilt reaction | — | ❌ No speech, no mode change, no motor stop triggered by tilt |

### 12d. Hazard Detection

**Status: ❌ BROKEN / DEAD CODE**

| Sub-feature | Lines | Notes |
|-------------|-------|-------|
| `HAZARD` audio asset | 89 | Mapped to `hazard.mp3` |
| `HAZARD` event handler | — | ❌ **`EVENT:HAZARD` is never handled** in `handleArduinoMessage`. The audio asset exists but nothing plays it. |
| Object-detection hazard | — | ❌ No object label → hazard mapping exists |

### 12e. Orientation Sensor (Rotation Vector)

**Status: ⚠️ PARTIAL**

| Sub-feature | Lines | Notes |
|-------------|-------|-------|
| Sensor registration | 896–902 | ✅ Registered in `onResume` |
| `SENS\|H:…\|P:…\|R:…` send | 920–924 | ✅ Sent to Mega on every sensor event |
| App-side reaction to orientation | — | ❌ No tilt/fall detection logic in MainActivity — data is forwarded to Mega only |
| Sensor unregister | 922 | ✅ Unregistered in `onPause` |

---

## 13. AI Response Engine (Claude / Gemini)

**Supposed to do:** Call Claude API first, fall back to Gemini, return a ≤15-word child-friendly response.

**Status: ✅ WORKING** (network-dependent)

| Sub-feature | Lines | Status | Notes |
|-------------|-------|--------|-------|
| Claude API call | 1128–1162 | ✅ | Anthropic v1/messages endpoint |
| Gemini API call | 1164–1198 | ✅ | Gemini URL from `BuddyBotConfig` |
| Claude → Gemini fallback | 1103–1115 | ✅ | Exception caught, tries Gemini |
| Gemini → "Offline mode." fallback | 1116 | ✅ | Final fallback string |
| Word limiter | 1119–1126 | ✅ | Applied to both responses |
| Child-friendly system prompt | 1129–1137 | ✅ | Consistent across both APIs |

**What's missing:**
- API keys are in `BuddyBotConfig` — if they are hardcoded strings (not secrets manager), they are exposed in the APK.
- No caching — identical questions hit the network every time.
- No offline mode detection — the app always tries the network even when clearly offline, adding latency.

---

## 14. Emergency Mode

**Supposed to do:** Stop all motors, play emergency audio, show red overlay.

**Status: ✅ WORKING**

| Sub-feature | Lines | Status | Notes |
|-------------|-------|--------|-------|
| `EMERGENCY_STOP` to Mega | 1289 | ✅ | Sent immediately |
| `isEmergency` state flag | 1288 | ✅ | Triggers `EmergencyOverlay` in UI |
| Emergency audio | 1290 | ✅ | `playAudioCommand("EMERGENCY")` |
| Triggered by SOS button | 635 | ✅ | `onEmergency` callback |
| Triggered by patrol obstacle | 446 | ✅ | `fDelta > SENSOR_DELTA_THRESHOLD` |
| Emergency reset / cancel | — | ❌ | **No way to clear `isEmergency = true`.** Once triggered, the red overlay is permanent for the session. |

---

## Summary Table

| Feature | Status | Biggest Gap |
|---------|--------|-------------|
| Voice commands | ⚠️ PARTIAL | No `AUTO:OFF`; no null guard on SpeechRecognizer |
| Text-to-speech | ⚠️ PARTIAL | Blind 1.5 s delay for TTS; shared audio file race |
| Face detection + send | ⚠️ PARTIAL | No confidence filter; tracking delegated to Mega |
| Object detection + send | ⚠️ PARTIAL | No in-app reaction; `HAZARD` asset is dead code |
| Gesture recognition | ⚠️ PARTIAL | CW and CCW identical; animation assets may be missing |
| AUTO:ON / AUTO:OFF | ⚠️ PARTIAL | `AUTO:OFF` missing; no state tracking |
| DOG mode | ⚠️ PARTIAL | `startDogBehavior()` is a stub; no follow motor logic |
| PARTY mode | ⚠️ PARTIAL | One-shot dance; no loop, no face animation |
| BODYGUARD mode | ⚠️ PARTIAL | `INTRUDER` audio dead; stale `recognizedPerson` risk |
| UNHINGED mode | ✅ WORKING | No face animation |
| NORMAL alive behaviour | 🔧 STUB | Loop runs but does nothing |
| Battery warning | ✅ WORKING | — |
| Obstacle reaction | ⚠️ PARTIAL | No avoidance motor commands from app |
| Tilt reaction | ⚠️ PARTIAL | Audio only; no motor stop |
| Hazard detection | ❌ BROKEN | `EVENT:HAZARD` never handled; asset is dead code |
| Orientation sensor | ⚠️ PARTIAL | Data forwarded to Mega only; no app-side logic |
| AI response engine | ✅ WORKING | API keys in config; no caching |
| Emergency mode | ⚠️ PARTIAL | No reset/cancel mechanism |
