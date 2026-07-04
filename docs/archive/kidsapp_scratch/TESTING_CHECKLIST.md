# ✅ BuddyBot Kids - Testing & Validation Checklist

## Pre-Test Requirements

- [ ] Galaxy S9 connected via USB cable
- [ ] USB debugging enabled on device
- [ ] `adb` installed and working (`adb devices` shows your device)
- [ ] All API keys filled in `secrets.properties`
- [ ] Java 11+ installed (`java -version`)

---

## Build Validation

### Step 1: Clean Build
```bash
cd D:\PATHWITHNOSPACESFORBUDDYBOT\BuddyBot\KidsApp
./gradlew.bat clean
```
**Expected:** No errors, clean removes old build artifacts

### Step 2: Assemble Debug APK
```bash
./gradlew.bat assembleDebug
```
**Expected:** ✅ BUILD SUCCESSFUL  
**Location:** `app/build/outputs/apk/debug/app-debug.apk`

**If Failed:**
- [ ] Check for compilation errors in output
- [ ] Verify all imports are correct
- [ ] Confirm gradle.properties is valid
- [ ] Run `./gradlew.bat --info clean build` for detailed output

---

## Installation Testing

### Step 3: Uninstall Previous Version
```bash
adb uninstall com.buddybot.kids
```
**Expected:** App uninstalled (or "app not installed" message)

### Step 4: Install Fresh APK
```bash
adb install app/build/outputs/apk/debug/app-debug.apk
```
**Expected:** ✅ Success

**If Failed:**
- [ ] Check device storage space: `adb shell df -h`
- [ ] Ensure USB connection is stable
- [ ] Try: `adb install -r app/build/outputs/apk/debug/app-debug.apk` (force reinstall)

---

## Runtime Testing

### Step 5: Start Logging (Terminal 1)
```bash
adb logcat -s "BuddyBotMainActivity,FaceCoordinator,EnvironmentMonitor" -v time > app_log.txt
```
**Expected:** Logcat shows live logs as app runs

### Step 6: Launch App (Terminal 2)
```bash
adb shell am start -n com.buddybot.kids/.MainActivity
```
**Expected:** App appears on Galaxy S9 screen

### Step 7: Monitor Initial Startup
Watch for these log messages (in order):

**Phase 1: Permission Prompt** (Immediate)
```
[Any] Intent { act=android.intent.action.MAIN cat=[android.intent.category.LAUNCHER] }
```
- [ ] Device shows permission requests
- [ ] User taps "Allow All" or grants permissions individually

**Phase 2: Initialization** (5-10 seconds)
```
BuddyBotMainActivity: initializeApp: Starting initialization
BuddyBotMainActivity: initializeApp: TextToSpeech initialized
BuddyBotMainActivity: initializeApp: Camera executor initialized
BuddyBotMainActivity: initializeApp: FaceRecognitionManager initialized
BuddyBotMainActivity: initializeApp: ObjectDetectionManager initialized
BuddyBotMainActivity: initializeApp: ArduinoComms initialized
BuddyBotMainActivity: initializeApp: SecurityGatekeeper initialized
BuddyBotMainActivity: initializeApp: Sensor manager initialized
BuddyBotMainActivity: initializeApp: Speech recognizer initialized
BuddyBotMainActivity: initializeApp: Arduino communications initialized
BuddyBotMainActivity: initializeApp: Environment monitoring started
BuddyBotMainActivity: initializeApp: Complete
```
- [ ] All initialization steps logged
- [ ] No ERROR messages in this phase
- [ ] Screen shows "First Meeting" dialog

**Phase 3: First Meeting Dialog** (Visible on device)
```
Dialog: "First Meeting - Would you like to play the introduction video for AJ?"
Buttons: "Yes" "No"
```
- [ ] Dialog is visible
- [ ] Buttons are clickable

### Step 8: Test Dialog Response

#### Test Case A: Click "Yes"
```bash
# Device shows "First Meeting" dialog
# User clicks "Yes"
```

**Expected Logs:**
```
BuddyBotMainActivity: startMainProgram: Initializing components
BuddyBotMainActivity: startMainProgram: Listening started
BuddyBotMainActivity: startMainProgram: USB webcam initialized
BuddyBotMainActivity: startMainProgram: Setting robot mode to NORMAL
FaceCoordinator: setRobotMode: NORMAL -> NORMAL (transitioning=false)
FaceCoordinator: playCurrentLoop: normal_idle (speaking=false, mode=normal)
FaceCoordinator: playVideo: normal_idle (loop=true)
FaceCoordinator: Video playing: normal_idle
BuddyBotMainActivity: startMainProgram: Robot mode set successfully
BuddyBotMainActivity: startMainProgram: Alive behavior started
BuddyBotMainActivity: startMainProgram: State listener registered
BuddyBotMainActivity: startMainProgram: Main Program Initialization Complete
```

**Expected Visual Result:**
- [ ] Dialog dismisses
- [ ] **NOT a black screen** ✅
- [ ] Either:
  - ✅ Robot face video plays (animated face visible)
  - ✅ Or black screen with visible controls (buttons, HUD elements)

#### Test Case B: Click "No"
```bash
# Device shows "First Meeting" dialog
# User clicks "No"
```

**Expected:** Same logs and visual result as "Yes"

---

## Feature Testing

### Step 9: Test Voice Interaction
**Prerequisite:** App is past "First Meeting" and showing robot face

```bash
# On device, say "Hey Buddy"
```

**Expected Logs:**
```
BuddyBotMainActivity: Recognized: "hey buddy"
BuddyBotMainActivity: Wake word detected
BuddyBotMainActivity: startCommandListening
```

**Expected Visual:**
- [ ] Red border appears around screen (listening mode)
- [ ] Robot speaks "Yeah?"
- [ ] Red border disappears after speaking

### Step 10: Test Mode Switching
```bash
# In the app, access settings menu (top-left gear icon)
# Select a different mode (Dog, Bodyguard, Party, etc.)
```

**Expected Logs:**
```
BuddyBotMainActivity: setRobotMode: NORMAL -> DOG
FaceCoordinator: setRobotMode: NORMAL -> DOG (transitioning=true)
FaceCoordinator: playVideo: normal_to_dog (loop=false)
FaceCoordinator: Video playing: normal_to_dog
[After transition]
FaceCoordinator: playCurrentLoop: dog_idle (speaking=false, mode=dog)
FaceCoordinator: playVideo: dog_idle (loop=true)
FaceCoordinator: Video playing: dog_idle
```

**Expected Visual:**
- [ ] Video plays showing transition to new mode
- [ ] Robot face changes appearance

---

## Error Handling Testing

### Step 11: Simulate Missing Video (Advanced)
**Skip if unsure**

If you want to verify the fallback system works:
1. Temporarily rename a video file
2. Trigger that mode change
3. Verify fallback logs appear and app continues working

**Expected Logs:**
```
FaceCoordinator: Resource not found: [mode]_idle - trying fallback
FaceCoordinator: playVideo: normal_idle (loop=true)
FaceCoordinator: Video playing: normal_idle
```

---

## Success Criteria

### 🟢 GREEN (All Good)
```
✅ App launches without crashing
✅ "First Meeting" dialog appears
✅ Dialog dismisses after Yes/No
✅ Robot face displays (video plays OR black screen with UI elements visible)
✅ Logs show all initialization complete
✅ No FATAL or CRITICAL error messages in logs
✅ Voice interaction works (after "Hey Buddy")
✅ Mode switching works
```

### 🟡 YELLOW (Acceptable with Warnings)
```
⚠️  Some initialization steps show errors but marked as non-critical
⚠️  Fallback messages appear (e.g., "Resource not found - trying fallback")
⚠️  App continues functioning despite warnings
✅ No crashes or black screens with no UI visible
```

### 🔴 RED (Problem)
```
❌ App crashes after "First Meeting" dialog
❌ FATAL or CRITICAL messages in logs
❌ Pure black screen with NO UI elements visible
❌ App doesn't respond to touches
❌ Video never plays AND no error logs shown
```

---

## Debugging Guide

### If Tests Fail

**Step A: Capture Full Logs**
```bash
adb logcat > full_logs.txt &
# Let it run for 30 seconds
# Then Ctrl+C
```

**Step B: Check for Errors**
```bash
grep -i "error\|exception\|fatal\|critical" full_logs.txt
```

**Step C: Share Output**
Look for these specific lines:
- First line with ERROR or EXCEPTION
- Any FATAL or CRITICAL messages
- Last 20 lines of log before crash

**Step D: Check Specific Subsystems**
```bash
# Video playback errors
grep "FaceCoordinator" full_logs.txt

# Initialization errors
grep "initializeApp" full_logs.txt

# Startup errors
grep "startMainProgram" full_logs.txt

# Resource errors
grep "Resource not found" full_logs.txt
```

---

## Quick Reference: Log Levels

| Level | Tag | What It Means |
|-------|-----|--------------|
| **D** | DEBUG | Normal operation, informational |
| **W** | WARNING | Non-critical issue, but notable (recoverable) |
| **E** | ERROR | Something failed, but app continues |
| **F** | FATAL | App is crashing |
| **C** | CRITICAL | Disaster scenario (should rarely see this) |

**In Logcat:**
```
BuddyBotMainActivity: startMainProgram: Listening started        ← INFO (D)
BuddyBotMainActivity: Error starting listening                  ← ERROR (E)
FaceCoordinator: Resource not found: dog_idle - trying fallback  ← WARNING (W)
BuddyBotMainActivity: FATAL ERROR in startMainProgram           ← FATAL (F)
```

---

## Cleanup Between Tests

```bash
# Clear app data
adb shell pm clear com.buddybot.kids

# Clear logs
adb logcat -c

# Restart app
adb shell am start -n com.buddybot.kids/.MainActivity
```

---

## Expected Timeline

| Step | Duration | What Happens |
|------|----------|--------------|
| App Launch | 1s | Splash screen (if enabled) |
| Permissions | 2-5s | System shows permission prompts |
| Initialization | 5-10s | Components initialize, logs appear |
| First Meeting | 2s | Dialog appears on screen |
| After Dialog | 2-3s | Main UI initializes |
| Ready to Interact | - | App responds to touches |

**Total: ~15-25 seconds from app launch to ready**

---

## Next Steps After Testing

1. **If GREEN:** App is working! 🎉
   - Proceed with normal use
   - Monitor logs for any warnings

2. **If YELLOW:** App works but something needs attention
   - Identify which component is showing warnings
   - Check if fallback mechanisms are working
   - May be related to missing hardware (USB camera, Arduino)

3. **If RED:** Something is broken
   - Share full logs from step A above
   - Identify the exact error message
   - Check compilation errors
   - Rebuild clean APK

---

## Support Information

**Critical Files for Troubleshooting:**
- `app_log.txt` - Logcat output
- `full_logs.txt` - Complete logs
- `BLACK_SCREEN_FIX_SUMMARY.md` - Technical details of fixes
- `BUILD_AND_RUNTIME_FIXES.md` - Build & runtime guidance
- `AGENTS.md` - Architecture reference

**Modified Files:**
- `FaceCoordinator.kt` - Video playback with error handling
- `MainActivity.kt` - Startup sequence with error handling
- `gradle.properties` - Build configuration

---

**Date:** March 28, 2026  
**Version:** 2.0.1  
**Status:** Ready for Testing ✅

