# BuddyBot Architecture

Source of truth for boards, links, and ownership. Read this before changing firmware or Android comms.

Last aligned to repo tree + README + `serial_bridge.h` + `MessageRouter.kt` + `docs/PicoW_Wiring_And_AT_Guide.md` (Sep 2026).

## Brains

| Role | Hardware | Code | Owns |
|---|---|---|---|
| Head / AI | Samsung S9 | `android/BuddyBot/KidsApp/` | Vision (ML Kit + FaceNet), TTS/lipsync, AI router (Groq→Gemini→Claude→offline), wake word, GuardianEngine, USB serial + HTTP fallback |
| Parent monitor | Android | `android/BuddyBot/ParentApp/` + `parent-app/` | MJPEG view, remote status |
| Browser companion | Any browser | `web-kidsapp/` | Talk + games + simulated V37 HUD. No USB camera, no motors, no wake word |
| Bridge + HMI | Pico W | `firmware/BuddyBot_PicoW_Dash_V1.2/` | USB CDC ↔ UART1 to Mega, TFT dashboard, touch, softAP+STA, HTTP :80, PWM audio GP14 |
| Body / safety | Mega 2560 V37 | `firmware/BuddyBot_Mega_V37/` | Motors (TB6612), sensors, ESTOP/hazards, 10s watchdog, 2s motor timeout |
| Optional RC | ESP32 | `firmware/BuddyBot_ESP32_Remote/` | Handheld / extra remote. Do not reintroduce as the WiFi bridge |

Archived boards live in `old_firmware/`. Do not compile those as current.

## Data path (production)

```
S9 KidsApp  --USB CDC 115200-->  Pico W Serial (USB)
                                 |  serial_bridge.h
                                 |  PICO:* stays on Pico
                                 |  CMD: stripped before Mega
                                 v
                               Serial2 UART1 115200
                               GP4 TX -> Mega D19 RX1
                               GP5 RX <- Mega D18 TX1  (5V->3.3V divider)
                                 |
                                 v
                            Mega Serial1
```

Fallback: S9 `ArduinoComms` can talk HTTP to Pico `:80` if USB drops. Motors still require Mega.

Webcam is USB UVC on the S9 hub, **not** on the Pico.

## Pin / link rules that agents must not "simplify"

- All serial links: **115200**, newline-terminated text.
- **Never wire Pico GP0/GP1.** Those are internal to the onboard radio.
- Mega TX1 (D18, 5V) **must** go through 1k / 2k divider into Pico GP5.
- Pico GP4 (3.3V) may go direct to Mega D19.
- Common GND required.
- Mega GPS is hardware Serial3 (D14 TX3 / D15 RX3), not SoftwareSerial D10/D11.
- TFT (ST7796 / TFT_eSPI): GP16–22. Touch FT6336U on I2C1 GP26/GP27.
- Audio PWM: GP14 → SC8002B (5V VCC on amp, not 3.3V).
- Battery pack: 2S ~8.4V. Warn ~11V pack-side in docs vs 10.5V critical — do not change cutoffs without measuring the actual divider.

## Boot order

1. Pack power / Mega comes up, hardware watchdog armed.
2. Mega prints `SYSTEM|READY|<fw>|END`.
3. Pico `bridgeSetup()` then `bridgeLoop()` **once per loop** (duplicate call floods UART — already burned July 2026).
4. S9 USB enumerates Pico CDC; `MessageRouter` is the only line parser.
5. Motor commands only after link is up. Mega auto-stops motors if no command for **2 seconds**.

## Safety ownership (Mega is law)

These override personalities, games, and LLM output:

- Fire / gas / tilt / edge / ESTOP → immediate STOP
- 10 s hardware watchdog on Mega
- 2 s motor command timeout
- Hazard flags in `STAT:` / `STATUS|`

Android / Pico / web UI must **never** assume they can keep driving through a hazard.

## Personality vs motion

Personalities (NORMAL, DOG, BODYGUARD, UNHINGED, PARTY) are **S9 behaviour + face videos + TTS tone**. Mega may receive `MODE:` / `REQ_MODE:` but motion profiles stay conservative. UNHINGED is adults-only on the phone; web KidsApp must not expose it.

## AI layers (do not conflate)

1. **Coding agent** (Cursor / Claude / Copilot) edits this repo.
2. **On-phone AI router** chooses a chat model; output is toddler-capped and mapped to existing commands.
3. **On-Mega intelligence** is a deterministic state machine. Do not put an LLM on the Mega.

Correct "smarter robot" path:

```
Speech/text → S9 LLM → validated command line → Pico bridge → Mega state machine
```

## Where to work

| Change | Folder |
|---|---|
| Line protocol / telemetry parse | `MessageRouter.kt` + Mega V37 + this doc + `PROTOCOL.md` together |
| USB/UART forwarding | `serial_bridge.h` only; do not add a second bridge in the .ino |
| Dashboard / games / WiFi | Pico W sketch + headers |
| Motors / sensors / watchdog | Mega V37 only |
| Face / vision / TTS | KidsApp Kotlin |
| Parent view | ParentApp |
| No-hardware UI | `web-kidsapp/` |

## Known landmines (July 2026)

- Compose `viewModel` before `initializeApp()` crashed launch — keep safe telemetry defaults.
- UVC `updateTexImage()` after disconnect — catch + drop pending draws.
- `bridgeLoop()` twice per iteration — do not bring that back.
- Touch I2C vs CYW43 TX — keep the short timeout in `readTouch()`.
- Dual `onMessageReceived` hooks — **MessageRouter is the only parser**.

## Agent working set

Do not index `android_logs.txt` (~10MB), `old_firmware/`, or `terminals/` unless debugging a specific log. Prefer:

- `README.md`
- `docs/ARCHITECTURE.md`
- `docs/PROTOCOL.md`
- `docs/PicoW_Wiring_And_AT_Guide.md`
- `firmware/BuddyBot_PicoW_Dash_V1.2/serial_bridge.h`
- `android/.../MessageRouter.kt`
- The single firmware .ino you were asked to change
