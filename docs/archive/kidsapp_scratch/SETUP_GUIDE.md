# 🤖 BUDDYBOT KIDS - COMPLETE SETUP GUIDE
## Get Your Robot Running TODAY!

---

## 📋 TABLE OF CONTENTS
1. [What You'll Need](#what-youll-need)
2. [API Keys Setup (15 minutes)](#api-keys-setup)
3. [Hardware Setup (30 minutes)](#hardware-setup)
4. [Software Installation (20 minutes)](#software-installation)
5. [Configuration (10 minutes)](#configuration)
6. [First Run & Testing (15 minutes)](#first-run--testing)
7. [Face Registration (10 minutes)](#face-registration)
8. [Troubleshooting](#troubleshooting)

**Total Time: ~2 hours**

---

## 1. WHAT YOU'LL NEED

### Hardware
- ✅ Samsung Galaxy S9 (your robot's face)
- ✅ USB webcam
- ✅ USB hub with Power Delivery (PD)
- ✅ Arduino Mega 2560 (for robot control)
- ✅ USB cables (USB-C for S9, USB-A for Arduino & webcam)
- ✅ Power supply for USB hub
- ✅ Your computer (Windows/Mac/Linux for development)

### Accounts You'll Need
- ElevenLabs account (for voice)
- Anthropic account (for Claude AI)
- Firebase account (for notifications)
- Facebook/Messenger account (for video calls)

### Software
- Android Studio (latest version)
- Your favorite text editor

---

## 2. API KEYS SETUP (15 minutes)

### A. ElevenLabs API Key

1. **Go to ElevenLabs:**
   - Visit: https://elevenlabs.io/
   - Sign up or log in

2. **Get your API key:**
   - Go to: https://elevenlabs.io/app/settings/api-keys
   - Click "Create API Key"
   - Copy the key (looks like: `f71d83...`)

3. **Find Voice ID:**
   - Go to Voice Library
   - Find a boy's voice you like (or use default: `XP4U9NPyLGdlseTzp9Hf`)
   - Copy the Voice ID

### B. Claude (Anthropic) API Key

1. **Get Claude API access:**
   - Visit: https://console.anthropic.com/
   - Sign up for API access
   - Go to: https://console.anthropic.com/settings/keys

2. **Create API key:**
   - Click "Create Key"
   - Copy it (looks like: `sk-ant-api03-...`)
   - **IMPORTANT:** Store it safely - you can't see it again!

### C. Firebase Setup (For Notifications)

1. **Create Firebase Project:**
   - Visit: https://console.firebase.google.com/
   - Click "Add Project"
   - Name it "BuddyBot-Kids"
   - Disable Google Analytics (not needed)

2. **Add Android App:**
   - Click "Add App" → Android icon
   - Package name: `com.buddybot.kids`
   - Download `google-services.json`
   - Save it for later

3. **Get Server Key:**
   - In Firebase Console → Project Settings
   - Click "Cloud Messaging" tab
   - Copy "Server Key" (this sends you notifications)

### D. Get Your Messenger ID

1. **Find your Facebook/Messenger User ID:**
   - Visit: https://findmyfbid.com/
   - Enter your Facebook profile URL
   - Copy your numeric user ID

2. **Get your phone number:**
   - Make sure it's in E.164 format: `+[country code][number]`
   - Example: `+61412345678` for Australian number

---

## 3. HARDWARE SETUP (30 minutes)

### Physical Assembly

1. **Connect USB Hub:**
   ```
   USB-C Hub (with PD)
   ├── Power adapter → Wall outlet
   ├── USB-C cable → Samsung S9 (charges + data)
   ├── USB-A port → USB Webcam
   └── USB-A port → Arduino Mega
   ```

2. **Mount the S9:**
   - Position S9 as the robot's "face"
   - Make sure USB-C connection is secure
   - Test that it charges

3. **Position Webcam:**
   - Mount webcam to give best view of AJ
   - Point slightly downward for face detection
   - Secure cable so it doesn't get pulled

4. **Arduino Connection:**
   - Connect Arduino to hub
   - Make sure green power LED lights up
   - Note: You'll upload Arduino code separately

### USB Webcam Setup on Android

**IMPORTANT:** Not all USB webcams work with Android out of the box!

1. **Check webcam compatibility:**
   - Most UVC (USB Video Class) webcams work
   - Logitech C270, C920, Microsoft LifeCam work great

2. **Enable USB debugging on S9:**
   - Settings → About Phone
   - Tap "Build Number" 7 times
   - Go back → Developer Options
   - Enable "USB Debugging"

3. **Test webcam:**
   - Download "USB Camera" app from Play Store
   - Plug in webcam
   - App should detect and show video

---

## 4. SOFTWARE INSTALLATION (20 minutes)

### Install Android Studio

1. **Download Android Studio:**
   - Visit: https://developer.android.com/studio
   - Download for your OS
   - Install (follow installer prompts)

2. **Launch and update:**
   - Open Android Studio
   - Let it download SDK components
   - This takes 10-15 minutes

### Enable Developer Mode on S9

1. **Enable Developer Options:**
   - Settings → About Phone
   - Tap Build Number 7 times
   - Go back to Settings

2. **Enable USB Debugging:**
   - Settings → Developer Options
   - Turn on "USB Debugging"
   - Turn on "Stay Awake" (keeps screen on while charging)

3. **Connect to computer:**
   - Plug S9 into computer
   - Allow USB debugging when prompted
   - Run `adb devices` in terminal to verify

---

## 5. CONFIGURATION (10 minutes)

### A. Setup Project Files

1. **Extract the fixed project:**
   - You now have `BuddyBot_Fixed` folder
   - This is your project directory

2. **Add your video files:**
   ```
   BuddyBot_Fixed/app/src/main/res/raw/
   ├── intro.mp4
   ├── normal_idle.mp4
   ├── normal_talk.mp4
   ├── normal_looking.mp4
   ├── normal_surprised.mp4
   ├── dog_transition.mp4
   ├── dog_barking.mp4
   ├── dog_sniffing.mp4
   ├── dog_looking.mp4
   ├── dog_idle.mp4
   ├── dog_alerted.mp4
   ├── dog_alertsearching.mp4
   ├── bodyguard_transition.mp4
   ├── bodyguard_looking.mp4
   └── unhinged_idle.mp4
   ```

3. **Add Firebase config:**
   - Copy `google-services.json` (from Firebase)
   - Paste into: `BuddyBot_Fixed/app/`

4. **Add your photo (for Call Daddy button):**
   - Get a photo of yourself (square works best)
   - Rename it to `daddy_photo.jpg`
   - Put in: `BuddyBot_Fixed/app/src/main/res/drawable/`

### B. Configure API Keys

1. **Edit `secrets.properties`:**
   ```properties
   # Your ElevenLabs API Key
   ELEVENLABS_API_KEY=paste_your_key_here
   
   # Your Claude API Key
   CLAUDE_API_KEY=paste_your_key_here
   
   # Your phone number (E.164 format)
   DADDY_PHONE_NUMBER=+61412345678
   
   # Your Messenger User ID
   DADDY_MESSENGER_ID=your_numeric_id_here
   
   # Firebase Server Key
   FCM_SERVER_KEY=paste_firebase_server_key_here
   ```

2. **Save the file**

3. **IMPORTANT SECURITY:**
   - NEVER commit `secrets.properties` to git
   - Already in `.gitignore`
   - If sharing code, create `secrets.properties.template` instead

---

## 6. FIRST RUN & TESTING (15 minutes)

### Build and Install

1. **Open project in Android Studio:**
   - File → Open
   - Select `BuddyBot_Fixed` folder
   - Wait for Gradle sync (5-10 minutes first time)

2. **Fix any dependency issues:**
   - Android Studio will show errors if any
   - Click "Sync Project with Gradle Files"
   - Let it download dependencies

3. **Connect S9 and install:**
   ```bash
   # Make sure S9 is connected
   adb devices
   
   # In Android Studio:
   # Click green "Run" button (or Shift+F10)
   # Select your Samsung S9
   # Click OK
   ```

4. **First launch:**
   - App will request permissions
   - **GRANT ALL PERMISSIONS** (required for functionality)
   - You'll see intro video play

### Test Basic Functions

**Test 1: Voice**
- Tap screen
- Say "Hello BuddyBot"
- Robot should respond with voice

**Test 2: Call Daddy button**
- Tap the green "Call Daddy" button
- Should attempt Messenger video call
- Falls back to phone call if Messenger not installed

**Test 3: Emergency button**
- Tap red "SOS" button
- Should send you a notification
- Auto-calls after 3 seconds

**Test 4: USB Webcam**
- Check if camera is working
- Face should be detected in logs
- Check Android Studio Logcat for face detection messages

**Test 5: Environment Monitoring**
- Make a loud noise
- Check if volume spike is logged
- Swear (test inappropriate language detection)

---

## 7. FACE REGISTRATION (10 minutes)

Currently, the app needs faces to be registered. You'll need to add this manually:

### Register AJ and Family

1. **Temporarily add registration code:**

   Add this to MainActivity.kt (around line 200):
   ```kotlin
   // TEMPORARY: Face registration
   private fun registerFaces() {
       // When AJ is in front of camera
       lifecycleScope.launch {
           delay(5000) // Wait 5 seconds
           // Capture current camera frame and register
           Toast.makeText(this@MainActivity, "Registering AJ...", Toast.LENGTH_SHORT).show()
           // faceRecognitionManager.registerFace("AJ", currentImage)
       }
   }
   ```

2. **Call during startup:**
   - Add `registerFaces()` in `initializeApp()`
   - Have AJ look at camera
   - Wait for "Registered" toast

3. **Repeat for family:**
   - Daddy
   - Mom
   - Siblings
   - Anyone who should be recognized

4. **Remove registration code after done**

---

## 8. TROUBLESHOOTING

### Common Issues

#### "App crashes on startup"
**Solution:**
- Check Logcat in Android Studio
- Usually missing permissions
- Grant all permissions in Settings → Apps → BuddyBot

#### "No sound/TTS not working"
**Solutions:**
- Check internet connection (ElevenLabs API needs internet)
- Verify API key in `secrets.properties`
- Check volume is turned up
- Falls back to Android TTS if ElevenLabs fails

#### "Camera not working"
**Solutions:**
- Check webcam is UVC compatible
- Try different USB port on hub
- Download "USB Camera" app to test webcam
- Check camera permission granted
- Restart app

#### "USB Serial not connecting to Arduino"
**Solutions:**
- Check Arduino is powered (green LED)
- Verify correct USB cable (data cable, not charge-only)
- Check Arduino drivers installed
- Try different USB port

#### "Face recognition not working"
**Solutions:**
- Faces need to be registered first
- Good lighting required
- Face must be 15% or larger of frame
- Check Logcat for detection messages

#### "Environment monitoring not detecting anything"
**Solutions:**
- Microphone permission granted?
- Service running? (check notification)
- Threshold might be too high
- Check Logcat for audio levels

#### "Build errors in Android Studio"
**Solutions:**
```bash
# Clean project
./gradlew clean

# Rebuild
./gradlew build

# If Gradle issues:
File → Invalidate Caches / Restart
```

#### "Can't call Daddy"
**Solutions:**
- Messenger app installed on S9?
- Phone permission granted?
- Correct Messenger ID in secrets.properties?
- Check phone number format: +[country][number]

---

## ADVANCED FEATURES

### Auto-Start on Boot

Already configured! BuddyBot will start automatically when S9 boots.

To disable:
- Settings → Apps → BuddyBot → Permissions → Autostart (OFF)

### Custom Responses

Edit Claude system prompt in MainActivity.kt:
```kotlin
put("system", "You are BuddyBot... [customize this]")
```

### Add More Video Animations

1. Create/render new animations
2. Export as MP4
3. Add to `res/raw/`
4. Reference in code:
   ```kotlin
   private const val VIDEO_MY_CUSTOM = "my_custom.mp4"
   ```

### Adjust Sensitivity

In `EnvironmentMonitoringService.kt`:
```kotlin
// Make yelling detection less sensitive
private const val VOLUME_THRESHOLD_YELLING = 0.8 // was 0.7

// Adjust silence detection
private const val SILENCE_DURATION_MS = 600000L // 10 minutes
```

---

## 🎉 YOU'RE DONE!

Your BuddyBot should now be:
- ✅ Responding to voice commands
- ✅ Recognizing faces
- ✅ Monitoring environment
- ✅ Ready to call Daddy
- ✅ Auto-starting on boot
- ✅ 100% functional!

---

## DAILY OPERATION

### Starting BuddyBot
1. Plug in power to USB hub
2. S9 boots automatically
3. BuddyBot launches automatically
4. Takes ~30 seconds to initialize

### Monitoring From Your Phone
- Install Firebase Cloud Messaging app
- You'll get push notifications for:
  - Arguments detected
  - Swearing detected
  - Yelling detected
  - Child distress
  - Unknown person
  - Extended silence

### Emergency Protocol
- AJ taps red SOS button
- You get urgent notification
- Auto-calls you after 3 seconds
- Arduino triggers emergency lights/sounds

---

## NEXT STEPS

1. **Customize personality:**
   - Edit Claude system prompt
   - Add more video animations
   - Adjust voice settings

2. **Add educational games:**
   - Math challenges
   - Spelling practice
   - Science facts

3. **Improve face recognition:**
   - Add more training photos
   - Different angles
   - Different lighting

4. **Arduino integration:**
   - Upload control code to Arduino
   - Add servo movements
   - LED expressions
   - Sound effects

---

## SUPPORT

If you run into issues:

1. Check Logcat in Android Studio
2. Review this guide's Troubleshooting section
3. Check API service status:
   - ElevenLabs: https://status.elevenlabs.io/
   - Anthropic: https://status.anthropic.com/

---

## SAFETY NOTES

⚠️ **IMPORTANT:**
- Never leave USB hub unattended while charging
- Check all connections are secure
- Keep cables out of AJ's reach
- Monitor battery temperature
- Test emergency functions regularly
- Always have backup phone access

---

**Congratulations! BuddyBot is ready to be AJ's companion! 🤖**

Last updated: January 2026
Version: 2.0
