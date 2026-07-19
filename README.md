# 🤖 BuddyBot — AI-Powered Robotic Companion

**BuddyBot** is a production-grade, kid-friendly AI robot with 3 brains working in harmony: a **Samsung S9 phone** (face, voice, vision, AI), a **Pico W** (dashboard display, WiFi bridge, audio), and a **Mega 2560** (motors, sensors, safety). Two Android apps provide kid interaction and parent monitoring.

---

## 📦 System Architecture

```
┌─────────────────────────────────────────────────────────┐
│                    SAMSUNG S9 (Head)                     │
│  ┌─────────────┐  ┌──────────┐  ┌───────────────────┐  │
│  │  ML Kit      │  │ ElevenLabs│  │  AI Router        │  │
│  │  Face/Obj    │  │  TTS     │  │  Groq→Gemini→     │  │
│  │  Detection   │  │  LipSync │  │  Claude→Offline   │  │
│  └──────┬──────┘  └────┬─────┘  └────────┬──────────┘  │
│         │              │                  │              │
│         └──────────────┴──────────────────┘              │
│                        │                                │
│              ┌─────────▼──────────┐                     │
│              │   USB Hub (OTG)    │                     │
│              └──┬─────────────┬───┘                     │
│                 │             │                          │
│         ┌───────▼──┐   ┌─────▼──────┐                  │
│         │  Webcam  │   │  Pico W   │                  │
│         │ (UVC)    │   │ (Bridge)  │                  │
│         └──────────┘   └─────┬──────┘                  │
│                              │ UART1 (GP4/GP5)          │
│                       ┌──────▼──────┐                  │
│                       │  Mega 2560  │                  │
│                       │  (V37)      │                  │
│                       │  Motors,    │                  │
│                       │  Sensors,   │                  │
│                       │  Safety     │                  │
│                       └─────────────┘                  │
└─────────────────────────────────────────────────────────┘
```

### Communication Flow
- **S9 ↔ Pico W**: USB CDC (Serial) @ 115200 baud
- **Pico W ↔ Mega**: UART1 (GP4 TX, GP5 RX) @ 115200 baud
- **Pico W WiFi**: CYW43439 (native) — softAP + STA concurrent
- **Webcam**: USB UVC via libausbc → ML Kit pipeline

---

## 🧠 Core Components

### 1. Android App — KidsApp (`android/BuddyBot/KidsApp/`)
The S9 runs the main BuddyBot app with:
- **AI Router** (`AIRouter.kt`): Fallback chain Groq → Gemini → Claude → Offline
- **Face Recognition** (`FaceRecognitionManager.kt`): ML Kit + FaceNet 128-d embeddings
- **Object Detection** (`ObjectDetectionManager.kt`): ML Kit object detection
- **ArduinoComms** (`ArduinoComms.kt`): USB serial + HTTP fallback to Pico W
- **MessageRouter** (`MessageRouter.kt`): Single-source-of-truth line parser for Mega V37 protocol
- **GuardianEngine** (`GuardianEngine.kt`): Audio-based aggression/emergency detection
- **HotwordService** (`HotwordService.kt`): Always-listening "Hey Buddy" wake word
- **CameraStreamManager** (`streaming/CameraStreamManager.kt`): MJPEG server for parent app
- **Compose UI**: Full overlay with telemetry, gesture indicators, lipsync mouth, settings

### 2. Pico W Dashboard (`firmware/BuddyBot_PicoW_Dash_V1.2/`)
- **TFT Display**: 320×480 portrait, TFT_eSPI
- **Touch**: FT6336U capacitive touch (I2C on GP26/GP27)
- **Serial Bridge** (`serial_bridge.h`): Bidirectional S9↔Mega line forwarding
- **WiFi**: Native CYW43439 — softAP (BuddyBot-RC) + STA for home network
- **Web Server**: HTTP control + JSON status endpoint on port 80
- **Audio**: Hardware-PWM tone engine on GP14 (SC8002B amplifier)
- **6 Games**: Mario, Pac-Man, Starship, Memory, Color Match, Math Quiz

### 3. Mega 2560 V37 (`firmware/BuddyBot_Mega_V37/`)
- **Motor Control**: TB6612 dual H-bridge, PWM speed profiles
- **Sensors**: DHT11, MQ-2 gas, flame, PIR, tilt, HC-SR04 ultrasonics, HMC5883L compass, NEO-6M GPS
- **Safety Watchdogs**: 10-second hardware watchdog, 2-second motor timeout, hazard override
- **Protocol**: Pipe-delimited STATUS|, STAT:, US:, IR:, HDG:, TELE: messages

---

## 🎭 Personalities (Modes)

| Mode | Description | Use Case |
|------|-------------|----------|
| **NORMAL** 😊 | Friendly, educational, patient | Daily interaction with AJ |
| **DOG** 🐕 | Protective, barking alerts, patrol | Security mode |
| **BODYGUARD** 🕶️ | Tactical, perimeter scanning, threat assessment | Serious protection |
| **UNHINGED** 😈 | Sarcastic, roasting, adult humor | Adults only |
| **PARTY** 🎉 | Dancing, lights, celebration | Fun time |

---

## 📹 Animated Face System

15 video states on the S9 display (1920×1080, H.264, 30fps):
- **Normal**: idle, talk, looking, surprised
- **Dog**: transition, barking, sniffing, looking, idle, alerted, searching
- **Bodyguard**: transition, looking
- **Unhinged**: idle
- **Special**: intro (3-minute first meeting)

---

## 🎮 Educational Games (Pico W Dashboard)

| Game | Skill | Age |
|------|-------|-----|
| Super Mario Bros | Platforming, reflexes | 3+ |
| Pac-Man Classic | Navigation, strategy | 3+ |
| Starship Commander | Hand-eye coordination | 3+ |
| Matrix Memory | Memory, matching | 3+ |
| Color Match Ultra | Color recognition | 2+ |
| Math Blast Terminal | Basic arithmetic | 4+ |

---

## 📡 Sensor Suite

| Sensor | Type | Interface |
|--------|------|-----------|
| DHT11 | Temp/Humidity | Digital |
| MQ-2 | Gas (smoke, LPG, propane) | Analog |
| Flame Sensor | Fire detection | Analog + Digital |
| HC-SR04 (×2) | Ultrasonic distance | Digital |
| PIR | Motion detection | Digital |
| Tilt Sensor | Rollover detection | Digital |
| HMC5883L | Magnetometer/compass | I2C |
| NEO-6M | GPS | Serial |
| APDS-9960 | Gesture/Proximity | I2C |
| Logitech C270 | USB Webcam (UVC) | USB |

---

## 🔧 Recent Stability Fixes (July 2026)

| Bug | Root Cause | Fix |
|-----|-----------|-----|
| **App crash on launch** | `viewModel` used in Compose UI before `initializeApp()` | Safe telemetry getter with default fallback |
| **Camera crash loop** | `SurfaceTexture.updateTexImage()` on disconnected UVC camera | try/catch + remove pending draw messages + error event |
| **PicoW serial flood** | `bridgeLoop()` called twice per loop iteration | Removed duplicate call — halved serial traffic |
| **PicoW touch lockup** | I2C hangs when CYW43 WiFi radio transmits | 5ms timeout in `readTouch()` |
| **PicoW WiFi init blocks** | `restoreTouchI2c()` blocks 55ms without draining serial | Mitigated by I2C timeout |

---

## 🚀 Quick Start

### Prerequisites
- Android Studio (latest)
- Arduino IDE 2.x with RP2040 and Mega boards
- Python 3.x (for tools)

### Build Android App
```bash
cd android/BuddyBot/KidsApp
./gradlew assembleDebug
adb install -r app/build/outputs/apk/debug/app-debug.apk
```

### Flash Pico W
1. Open `firmware/BuddyBot_PicoW_Dash_V1.2/BuddyBot_PicoW_Dash_V1.2.ino` in Arduino IDE
2. Select board: **Raspberry Pi Pico W**
3. Install libraries: TFT_eSPI, WiFi
4. Upload

### Flash Mega 2560
1. Open `firmware/BuddyBot_Mega_V37/BuddyBot_Mega_V37.ino` in Arduino IDE
2. Select board: **Arduino Mega or Mega 2560**
3. Upload

---

## 📁 Project Structure

```
BuddyBot/
├── android/
│   └── BuddyBot/
│       ├── KidsApp/          # Main kid-facing Android app
│       └── ParentApp/        # Parent monitoring app
├── firmware/
│   ├── BuddyBot_Mega_V37/    # Mega 2560 motor/sensor controller
│   ├── BuddyBot_PicoW_Dash_V1.2/  # Pico W dashboard + bridge
│   └── BuddyBot_ESP32_Remote/     # ESP32 remote control
├── docs/                     # Wiring guides, HTML controller
├── tools/                    # Diagnostic scripts, pin finders
├── old_firmware/             # Archived firmware versions
└── parent-app/               # Parent app documentation
```

---

## 📊 Technical Specs

| Parameter | Value |
|-----------|-------|
| S9 Processor | Snapdragon 845, 4GB RAM |
| Mega Flash | 256 KB |
| Pico W Flash | 2 MB |
| Serial Baud | 115200 (all links) |
| WiFi | 802.11 b/g/n (2.4 GHz) |
| Battery | 8.4V 2S Li-Ion |
| Dimensions | 20×20×48 cm |
| Weight | ~2-3 kg |

---

## 🔒 Safety Systems

- **Hazard detection**: Fire, gas, tilt, edge → immediate STOP
- **Watchdog**: 10-second hardware watchdog on Mega
- **Motor timeout**: Auto-stop after 2 seconds of no command
- **Battery protection**: Low voltage warning at 11V, critical shutdown at 10.5V
- **Overheating**: Fan activation + alert at high temperature

---

## 🤝 Contributing

1. Fork the repo
2. Create a feature branch (`git checkout -b feature/amazing`)
3. Commit changes (`git commit -m 'Add amazing feature'`)
4. Push (`git push origin feature/amazing`)
5. Open a Pull Request

---

## 📄 License

This project is open source. See `LICENSE` for details.

---

*Built with ❤️ for AJ — the world's most advanced kid-friendly robot companion.*