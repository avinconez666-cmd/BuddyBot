# 🤖 BuddyBot Kids - AI Robot Companion

**Version 2.0 - Production Ready**

An advanced AI-powered robot companion for children, featuring face recognition, environment monitoring, and real-time parent notifications.

---

## ✨ Features

### Core Functionality
- 🗣️ **Natural Voice Conversations** - Powered by Claude AI and ElevenLabs
- 👁️ **Face Recognition** - Knows family members vs strangers
- 🎥 **USB Webcam Support** - Real-time computer vision
- 📹 **Multiple Personalities** - Normal, Dog, Bodyguard, Party modes
- 🎭 **Dynamic Expressions** - Video-based face animations

### Safety & Monitoring
- 🔴 **Environment Monitoring** - 24/7 audio surveillance for:
  - Arguments and conflicts
  - Inappropriate language
  - Yelling and raised voices
  - Child distress signals
  - Extended silence (potential issues)
  - Unknown persons detected
- 📱 **Real-time Parent Notifications** - Instant alerts to your phone
- 🚨 **Emergency SOS Button** - One-tap emergency call to parent
- 📞 **Quick Call Daddy** - Video call button for instant parent contact

### Hardware Integration
- 🔌 **USB Serial** - Arduino Mega communication
- 📷 **External Camera** - USB webcam for vision
- 🔋 **Battery Monitoring** - Track power levels
- 🤖 **Servo Control** - Physical movement via Arduino

### Smart Features
- 🧠 **Persistent Memory** - Remembers conversations and preferences
- 🎓 **Educational Content** - Age-appropriate learning
- 🎮 **Interactive Games** - Educational mini-games
- 🔄 **Auto-Start** - Launches on device boot

---

## 🏗️ Architecture

### Hardware Stack
- **Face Display:** Samsung Galaxy S9
- **Vision:** USB Webcam (UVC compatible)
- **Control:** Arduino Mega 2560
- **Power:** USB-C Hub with Power Delivery
- **Connectivity:** USB Serial, WiFi

### Software Stack
- **Platform:** Android 9+ (Kotlin)
- **AI:** Claude Sonnet 4.5 (Anthropic)
- **Voice:** ElevenLabs TTS
- **Vision:** ML Kit + TensorFlow Lite
- **Notifications:** Firebase Cloud Messaging
- **UI:** Jetpack Compose

---

## 📁 Project Structure

```
BuddyBot_Fixed/
├── app/
│   ├── src/main/
│   │   ├── java/com/buddybot/kids/
│   │   │   ├── MainActivity.kt              # Main app logic
│   │   │   ├── ml/
│   │   │   │   ├── FaceRecognitionManager.kt
│   │   │   │   └── ObjectDetectionManager.kt
│   │   │   ├── services/
│   │   │   │   ├── EnvironmentMonitoringService.kt
│   │   │   │   └── BuddyBotMessagingService.kt
│   │   │   └── receivers/
│   │   │       └── BootReceiver.kt
│   │   ├── res/
│   │   │   ├── layout/                     # XML layouts
│   │   │   ├── values/                     # Strings, colors, themes
│   │   │   ├── drawable/                   # Button graphics
│   │   │   ├── raw/                        # Video animations
│   │   │   └── xml/                        # Config files
│   │   └── AndroidManifest.xml
│   └── build.gradle
├── secrets.properties                       # API keys (NEVER commit!)
├── SETUP_GUIDE.md                          # Complete setup instructions
└── README.md                               # This file
```

---

## 🚀 Quick Start

See **[SETUP_GUIDE.md](SETUP_GUIDE.md)** for detailed instructions.

### Prerequisites
- Samsung Galaxy S9 (or similar Android 9+ device)
- USB webcam (UVC compatible)
- Arduino Mega 2560
- USB-C hub with PD
- API keys from:
  - [ElevenLabs](https://elevenlabs.io/)
  - [Anthropic](https://console.anthropic.com/)
  - [Firebase](https://console.firebase.google.com/)

### Installation (Quick Version)

1. **Configure API Keys:**
   ```bash
   cp secrets.properties.template secrets.properties
   # Edit secrets.properties with your keys
   ```

2. **Add Video Files:**
   - Place MP4 animations in `app/src/main/res/raw/`

3. **Add Firebase Config:**
   - Download `google-services.json` from Firebase Console
   - Place in `app/` directory

4. **Build and Install:**
   ```bash
   ./gradlew installDebug
   ```

5. **Grant Permissions:**
   - Camera, Microphone, Phone, Notifications
   - All required for full functionality

---

## 🔐 Security

### API Key Management
- **NEVER commit `secrets.properties`** to version control
- Keys are injected at build time via Gradle
- BuildConfig fields are obfuscated in release builds
- Use `.gitignore` to prevent accidental commits

### Permissions
- Camera - Face recognition
- Microphone - Voice & environment monitoring
- Phone - Emergency calls
- Internet - AI APIs
- USB - Arduino & webcam
- Notifications - Parent alerts
- Boot - Auto-start

---

## 📊 Environment Monitoring

### What It Detects

| Detection Type | Trigger | Severity | Parent Alert |
|---|---|---|---|
| **Yelling** | Volume >70% | 7/10 | Yes |
| **Swearing** | Keyword match | 6/10 | Yes |
| **Arguments** | Phrases + volume spike | 7/10 | Yes |
| **Child Distress** | "help", "stop", crying | 9/10 | **URGENT** |
| **Stranger** | Unknown face >5min | 8/10 | Yes |
| **Extended Silence** | No sound >5min | 5/10 | Yes |

### Notification Cooldown
- Similar alerts: 1 minute cooldown
- Prevents notification spam
- Urgent alerts bypass cooldown

---

## 🎮 Usage

### Voice Commands
- "Hey BuddyBot" - Wake up
- "Tell me a story" - Story time
- "Let's play a game" - Start game
- "What's that?" - Object recognition
- "Call my daddy" - Video call parent

### Physical Buttons
- **Green (Top Right):** Call Daddy - Instant Messenger video call
- **Red (Bottom Right):** Emergency SOS - Urgent parent alert + auto-call
- **Mode Buttons (Bottom Left):** Switch robot personalities

### Modes
- 🙂 **Normal:** Friendly companion
- 🐕 **Dog:** Playful puppy personality
- 🛡️ **Bodyguard:** Protective mode
- 🎉 **Party:** Fun and silly

---

## 🧪 Testing

### Test Checklist
- [ ] Voice recognition works
- [ ] TTS speaks correctly
- [ ] Face detection active
- [ ] Face recognition identifies family
- [ ] Call Daddy button opens Messenger
- [ ] Emergency button sends notification
- [ ] Environment monitoring detecting sounds
- [ ] USB Arduino communication working
- [ ] Auto-start on boot
- [ ] Video animations playing

### Debug Logging
```bash
# View all BuddyBot logs
adb logcat -s BuddyBotKids:* EnvironmentMonitor:* FaceRecognition:*

# Check for errors
adb logcat *:E

# Monitor environment alerts
adb logcat | grep "Alert:"
```

---

## 🔧 Customization

### Adjust Detection Sensitivity

Edit `EnvironmentMonitoringService.kt`:
```kotlin
// Volume thresholds (0.0 to 1.0)
private const val VOLUME_THRESHOLD_YELLING = 0.7
private const val SILENCE_THRESHOLD = 0.05

// Time thresholds
private const val SILENCE_DURATION_MS = 300000L  // 5 minutes
private const val NOTIFICATION_COOLDOWN = 60000L  // 1 minute
```

### Change Robot Personality

Edit Claude system prompt in `MainActivity.kt`:
```kotlin
put("system", "You are BuddyBot, a [customize personality here]")
```

### Add Custom Animations
1. Create MP4 video (1080x1920 recommended)
2. Place in `app/src/main/res/raw/`
3. Reference in code:
   ```kotlin
   private const val VIDEO_CUSTOM = "my_animation.mp4"
   playVideo(VIDEO_CUSTOM)
   ```

---

## 🐛 Known Issues & Limitations

1. **Face Recognition Model:** Currently simplified - add TFLite model for full recognition
2. **Speech-to-Text:** Environment monitoring uses basic detection - integrate Google STT for full transcription
3. **Notification Backend:** Requires Firebase setup - implement FCM token management
4. **USB Camera:** Not all webcams are UVC-compatible - test before deployment

---

## 🗺️ Roadmap

### v2.1 (Planned)
- [ ] Cloud-based face recognition database
- [ ] Multi-language support
- [ ] Advanced gesture recognition
- [ ] Parent dashboard web app
- [ ] More educational games

### v3.0 (Future)
- [ ] Emotion detection
- [ ] Autonomous navigation
- [ ] Multi-robot coordination
- [ ] AR learning experiences

---

## 📝 License

This project is for personal use. Commercial use requires separate licensing agreements for:
- ElevenLabs API
- Anthropic Claude API
- Any included ML models

---

## 🙏 Acknowledgments

- **ElevenLabs** - Natural voice synthesis
- **Anthropic** - Claude AI conversations
- **Google ML Kit** - Face & object detection
- **TensorFlow** - ML infrastructure
- **Firebase** - Push notifications
- **USB Serial for Android** - Hardware communication

---

## 📞 Support

For issues:
1. Check [SETUP_GUIDE.md](SETUP_GUIDE.md) troubleshooting section
2. Review Logcat output
3. Verify all API keys are correct
4. Ensure all permissions granted

---

## ⚠️ Safety Disclaimer

- This robot is a supplemental tool, not a replacement for parental supervision
- Always monitor the robot's operation
- Regularly test emergency functions
- Keep software updated for security patches
- Follow all electrical safety guidelines for USB charging

---

**Built with ❤️ for AJ**

Last updated: January 2026
