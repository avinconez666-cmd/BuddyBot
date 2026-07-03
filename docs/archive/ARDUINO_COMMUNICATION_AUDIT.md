# 🤖 BuddyBot Multi-Arduino Communication Audit
**Date:** April 9, 2026 | **Firmware Versions:** Mega V29.0, R3 V1.0, R4 V23.0, ESP32 V2.0  
**Status:** ⚠️ CRITICAL ISSUES FOUND + RECOMMENDATIONS

---

## SECTION 1: BOARD INVENTORY

### 1.1 Arduino Boards Identified

| Board | Type | Role | Firmware | File |
|-------|------|------|----------|------|
| **Mega** | Arduino Mega 2560 (Keyestudio WiFi Plus) | Master Controller | V29.0 | `BuddyBot_Mega_V29.ino` |
| **R3** | Arduino UNO R3 | Motor Shield Driver | V1.0 | `BuddyBot_R3_Motors.ino` |
| **R4** | Arduino UNO R4 WiFi | Dashboard/UI | V23.0 | `BuddyBot_R4_Dash.ino` |
| **ESP32** | ESP32 (GPIO16/17) | WiFi/Bluetooth Bridge | V2.0 | `BuddyBot_ESP32_V2.ino` |
| **S9** | Samsung Galaxy S9 (Android) | AI Brain/Voice | N/A | ArduinoComms.kt (Kotlin) |

### 1.2 Board Roles & Responsibilities

**MEGA (Master):**
- Central hub for all inter-board communication
- Sensor reading & safety logic
- Motor command queuing & forwarding
- Telemetry aggregation
- GPS, RF remote, gesture sensor handling
- Autonomous navigation logic
- Battery monitoring & ESTOP management

**R3 (Motor Slave):**
- Receives motor commands via SoftwareSerial
- Controls 4-motor Adafruit Motor Shield V1
- Executes DANCE and DEFENSE patterns
- Speed presets (SLOW/NORMAL/FAST)
- Status reporting (PING/STATUS)

**R4 (Dashboard):**
- 240×320 HX8347D touchscreen display
- Real-time telemetry visualization
- Educational games for 3-year-olds
- Sensor configuration UI
- Mode selection interface
- Serial comms log display

**ESP32 (Bridge):**
- WiFi connectivity (802.11 b/g/n)
- Bluetooth serial interface
- Web dashboard (HTML/JSON)
- Relay commands from web/BT to Mega
- Telemetry forwarding to web clients

**S9 (AI Brain):**
- Claude/Gemini API integration
- ElevenLabs voice synthesis
- Video face animations
- Gesture recognition feedback
- Command origination (user input)

---

## SECTION 2: COMMUNICATION MAP

### 2.1 Serial Communication Channels

```
┌─────────────────────────────────────────────────────────────────┐
│                    BUDDYBOT COMMUNICATION TOPOLOGY               │
├─────────────────────────────────────────────────────────────────┤
│                                                                   │
│  ┌──────────────┐                                                │
│  │  Samsung S9  │                                                │
│  │  (Android)   │                                                │
│  └──────┬───────┘                                                │
│         │ USB Serial 115200 baud                                 │
│         │ (bidirectional)                                        │
│         ▼                                                         │
│  ┌──────────────────────────────────────────────────────────┐   │
│  │         MEGA 2560 (Master Hub)                           │   │
│  │  ┌────────────────────────────────────────────────────┐  │   │
│  │  │ Serial  (USB)      ↔ S9 @ 115200                  │  │   │
│  │  │ Serial1 (18/19)    ↔ R4 @ 115200                  │  │   │
│  │  │ Serial2 (16/17)    ↔ GPS NEO-6M @ 9600            │  │   │
│  │  │ Serial3 (14/15)    ↔ ESP32 @ 115200               │  │   │
│  │  │ SoftSerial(10/11)  ↔ R3 @ 9600                    │  │   │
│  │  │ I2C (SDA/SCL)      ↔ PAJ7620 Gesture Sensor       │  │   │
│  │  │ RF (pin 36)        ↔ 433MHz RF Receiver           │  │   │
│  │  └────────────────────────────────────────────────────┘  │   │
│  └──────┬──────────────┬──────────────┬──────────────────────┘   │
│         │              │              │                          │
│    SoftSerial      Serial1         Serial3                       │
│    9600 baud      115200 baud     115200 baud                    │
│         │              │              │                          │
│         ▼              ▼              ▼                          │
│  ┌──────────────┐ ┌──────────────┐ ┌──────────────┐             │
│  │   R3 UNO     │ │   R4 UNO     │ │   ESP32      │             │
│  │   Motor      │ │   Dashboard  │ │   WiFi/BT    │             │
│  │   Shield     │ │   Display    │ │   Bridge     │             │
│  └──────────────┘ └──────────────┘ └──────┬───────┘             │
│                                            │                     │
│                                    WiFi/Bluetooth                │
│                                            │                     │
│                                    ┌───────▼────────┐            │
│                                    │  Web Browser   │            │
│                                    │  BT Terminal   │            │
│                                    └────────────────┘            │
│                                                                   │
└─────────────────────────────────────────────────────────────────┘
```

### 2.2 Communication Channels Detail

#### **Channel 1: S9 ↔ MEGA (USB Serial)**
- **Baud Rate:** 115200
- **Direction:** Bidirectional
- **Terminator:** `\n` (newline)
- **Message Format:** Plain text commands/responses
- **Key Commands:**
  - `MOTOR:F|B|L|R|S` (movement)
  - `MOTOR:DANCE|DEFENSE` (patterns)
  - `AUTO:ON|OFF` (autonomous mode)
  - `EMERGENCY_STOP|ESTOP_CLEAR` (safety)
  - `MODE:<name>` (personality mode)
  - `TOGGLE_SENSOR:<ID>:<ON|OFF>` (sensor control)
  - `GESTURE:<UP|DOWN|LEFT|RIGHT|CW>` (from Mega to S9)
  - `TELE:<volt>,<pct>,<moving>` (status to S9)

#### **Channel 2: MEGA ↔ R3 (SoftwareSerial)**
- **Baud Rate:** 9600
- **Pins:** Mega TX=10, RX=11 ↔ R3 RX=A0, TX=A1
- **Direction:** Bidirectional
- **Terminator:** `\n` (newline)
- **Message Format:** Pipe-delimited commands
- **Key Commands:**
  - `MOTOR|F|B|L|R|S` (movement)
  - `MOTOR|DANCE` (dance pattern)
  - `DEFENSE` (defense pattern)
  - `SPEED:SLOW|NORMAL|FAST|<0-255>` (speed control)
  - `PING` → `PONG:R3:SPD:<speed>:RUN:<Y|N>`
  - `STATUS` → `R3:STATUS:BEGIN...R3:STATUS:END`
  - `ACK:MOTOR|<cmd>` (acknowledgment)
  - `ACK:DANCE:DONE|ACK:DEFENSE:DONE` (pattern completion)

#### **Channel 3: MEGA ↔ R4 (Serial1)**
- **Baud Rate:** 115200
- **Pins:** Mega TX=18, RX=19 ↔ R4 D1, D0
- **Direction:** Bidirectional
- **Terminator:** `\n` (newline)
- **Message Format:** Colon/pipe-delimited telemetry & commands
- **Key Messages:**
  - `STAT:gas:temp:hum:haz:pir:tilt:flame:ir:volt:pct:amps` (telemetry)
  - `US:front,rear,left,right` (ultrasonic distances)
  - `PWR:volt:amps:watts:mah:pct:...` (power data)
  - `STATUS|ESTOP:YES|AUTO:ON|BAT:8.2|PCT:85|END` (status)
  - `SENS_ST|DHT:1|LIGHT:1|...|GPS:1|END` (sensor config)
  - `MODE:<name>` (mode change)
  - `GESTURE:<gesture>` (gesture events)
  - `BAT:WARN|BAT:LOW|BAT:CRITICAL` (battery alerts)
  - `SAFETY:TILT|FLAME_ALERT|GAS_ALERT` (safety events)

#### **Channel 4: MEGA ↔ ESP32 (Serial3)**
- **Baud Rate:** 115200
- **Pins:** Mega TX=14, RX=15 ↔ ESP32 GPIO17(TX), GPIO16(RX)
- **Direction:** Bidirectional
- **Terminator:** `\n` (newline)
- **Message Format:** Colon-delimited telemetry & commands
- **Key Messages:**
  - `TELEM:ESTOP|AUTO|MOVING|IDLE:volt:mode:front:left:right:rear:temp:humidity:gas:flame`
  - `STAT:gas:temp:hum:haz:pir:tilt:flame:ir:volt:pct:amps` (same as R4)
  - `US:front,rear,left,right` (ultrasonic)
  - `STATUS|ESTOP:YES|AUTO:ON|BAT:8.2|PCT:85|END` (status)
  - `SENS_ST|DHT:1|LIGHT:1|...|GPS:1|END` (sensor config)
  - `MODE:<name>` (mode change)
  - `GESTURE:<gesture>` (gesture events)
  - `BAT:WARN|BAT:LOW|BAT:CRITICAL` (battery alerts)
  - `ALERT:OVERTEMP|TILT_DETECTED|FLAME_DETECTED|GAS_DETECTED` (alerts)
  - `MEGA:BOOT` (handshake from Mega)
  - `READY` (handshake from ESP32)
  - `CMD:TOGGLE_SENSOR:GAS:OFF` (from ESP32 web/BT)

#### **Channel 5: MEGA ↔ GPS (Serial2)**
- **Baud Rate:** 9600
- **Pins:** Mega TX=17, RX=16 ↔ GPS NEO-6M
- **Direction:** Unidirectional (GPS → Mega)
- **Protocol:** NMEA 0183 (TinyGPS++ library)
- **Data:** Latitude, longitude, satellite count

#### **Channel 6: MEGA ↔ RF Remote (Pin 36)**
- **Protocol:** 433MHz RF (RCSwitch library)
- **Direction:** Unidirectional (RF → Mega)
- **Codes:**
  - `5393` = Forward
  - `5396` = Backward
  - `5394` = Left
  - `5397` = Right
  - `5392` = Stop
  - `5400` = Auto mode toggle

#### **Channel 7: MEGA ↔ PAJ7620 Gesture (I2C)**
- **Protocol:** I2C (Wire library)
- **Address:** Standard I2C
- **Direction:** Unidirectional (Sensor → Mega)
- **Gestures:**
  - `GES_UP_FLAG` → "UP"
  - `GES_DOWN_FLAG` → "DOWN"
  - `GES_LEFT_FLAG` → "LEFT"
  - `GES_RIGHT_FLAG` → "RIGHT"
  - `GES_FORWARD_FLAG` → "NEAR"
  - `GES_CLOCKWISE_FLAG` → "CW"

#### **Channel 8: ESP32 ↔ Web/Bluetooth**
- **WiFi:** 802.11 b/g/n, SSID: "OPTUS_8B4FC8N"
- **Web Server:** Port 80 (HTML + JSON API)
- **Bluetooth:** Serial profile, name: "BuddyBot"
- **Endpoints:**
  - `/` → HTML dashboard
  - `/status` → JSON telemetry
  - `/cmd?c=<command>` → Command relay
  - `/health` → Uptime/RSSI

---

## SECTION 3: ISSUES FOUND

### 🔴 CRITICAL ISSUES

#### **Issue 3.1: SoftwareSerial Pin Order Mismatch (R3 ↔ MEGA)**
**Severity:** CRITICAL  
**File:** `BuddyBot_Mega_V29.ino:101` & `BuddyBot_R3_Motors.ino:47`

**Problem:**
```cpp
// MEGA (line 101)
SoftwareSerial motorComm(11, 10);  // RX=11 ← R3 A1(TX), TX=10 → R3 A0(RX)

// R3 (line 47)
SoftwareSerial megaSerial(A0, A1); // RX=A0, TX=A1
```

The comment says "RX=11 ← R3 A1(TX)" but the actual pin order is `(RX, TX)` in SoftwareSerial constructor.
- **Mega:** `SoftwareSerial(11, 10)` = RX on pin 11, TX on pin 10
- **R3:** `SoftwareSerial(A0, A1)` = RX on A0, TX on A1

**Wiring should be:**
- Mega pin 10 (TX) → R3 pin A0 (RX) ✓ CORRECT
- Mega pin 11 (RX) ← R3 pin A1 (TX) ✓ CORRECT

**Status:** ✅ Actually correct, but comment is misleading. The code works because the wiring matches the constructor order.

---

#### **Issue 3.2: Syntax Error in R3 setup() - Line 427**
**Severity:** CRITICAL - CODE WILL NOT COMPILE  
**File:** `BuddyBot_R3_Motors.ino:427`

**Problem:**
```cpp
void setup() {
  // Disable motor outputs immediately, before anything else
  MOTOR|S  // ← SYNTAX ERROR: This is not valid C++!
  megaSerial.begin(9600);
```

This line `MOTOR|S` is a bitwise OR operation on undefined variables. It should be a comment or removed entirely.

**Fix:**
```cpp
void setup() {
  // Disable motor outputs immediately, before anything else
  // MOTOR|S command will be sent after serial link stabilizes
  megaSerial.begin(9600);
```

---

#### **Issue 3.3: Syntax Error in Mega setup() - Line 1451**
**Severity:** CRITICAL - CODE WILL NOT COMPILE  
**File:** `BuddyBot_Mega_V29.ino:1451`

**Problem:**
```cpp
void setup() {
  // Stop motors immediately on boot
  MOTOR|S;  // ← SYNTAX ERROR: Same issue as R3
```

**Fix:**
```cpp
void setup() {
  // Stop motors immediately on boot
  // Motor stop will be sent via SoftwareSerial after initialization
```

---

#### **Issue 3.4: Missing Serial.println() in toS9() Function**
**Severity:** HIGH - Data Loss  
**File:** `BuddyBot_Mega_V29.ino:439-442`

**Problem:**
```cpp
void toS9(const String& msg)  { 
  Serial.print(F("[SEND] S9 Response: ")); 
  Serial.println(msg); 
}
```

The function **prints to Serial Monitor but never sends to S9!** It should be:

```cpp
void toS9(const String& msg)  { 
  Serial.print(F("[SEND] S9 Response: ")); 
  Serial.println(msg);
  Serial.println(msg);  // ← MISSING: Actually send to S9!
}
```

**Impact:** All responses to S9 are lost. S9 never receives acknowledgments, telemetry, or event notifications.

---

#### **Issue 3.5: R3 Communication Test Blocks on Startup**
**Severity:** HIGH - Startup Delay  
**File:** `BuddyBot_Mega_V29.ino:164-190`

**Problem:**
```cpp
void runR3CommTest() {
  String response;
  motorComm.println(F("PING"));
  if (waitForR3Line("PONG", 2000, response)) {  // ← 2 second timeout
    Serial.println(F("R3 COMM OK"));
    Serial1.println(F("R3 COMM OK"));
  } else {
    Serial.println(F("R3 COMM FAIL — check wiring on pins 10/11 and A0/A1"));
    Serial1.println(F("R3 COMM FAIL — check wiring on pins 10/11 and A0/A1"));
    r3CommFail = true;
  }
  // ... more tests with 2000ms timeouts
}
```

Called from `setup()` at line 1487, this blocks the entire system for **6+ seconds** on startup if R3 is slow to respond.

**Fix:** Move to non-blocking periodic check in `loop()` or reduce timeout to 500ms.

---

#### **Issue 3.6: Race Condition in Motor Queue**
**Severity:** HIGH - Potential Motor Glitches  
**File:** `BuddyBot_Mega_V29.ino:399-496`

**Problem:**
```cpp
struct MotorCmd { char cmd[24]; bool pending; } mQueue = { "", false };

void sendMotor(const char* cmd) {
  // ... update nav state ...
  strncpy(mQueue.cmd, cmd, sizeof(mQueue.cmd));
  mQueue.pending = true;  // ← Set flag
  if (debugVerbose) { Serial.print(F("[MTR>Q] ")); Serial.println(mQueue.cmd); }
}

void drainMotorQueue() {
  if (!mQueue.pending) return;
  motorCommPrintln(mQueue.cmd);  // ← Send
  mQueue.pending = false;  // ← Clear flag
}
```

If `sendMotor()` is called twice before `drainMotorQueue()` runs, the second command **overwrites** the first without sending it. No queue, just a single-slot buffer.

**Scenario:**
1. `sendMotor("FORWARD")` → mQueue.cmd = "FORWARD", pending = true
2. `sendMotor("LEFT")` → mQueue.cmd = "LEFT", pending = true (FORWARD lost!)
3. `drainMotorQueue()` → sends "LEFT" only

**Fix:** Use a proper circular queue or add a warning if pending is already true.

---

#### **Issue 3.7: S9 Buffer Overflow Silent Failure**
**Severity:** MEDIUM - Data Loss  
**File:** `BuddyBot_Mega_V29.ino:828-843`

**Problem:**
```cpp
void handleS9Communication() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n') {
      processS9Command(s9Buffer);
      s9Buffer = "";
    } else if (c != '\r') {
      s9Buffer += c;
      if (s9Buffer.length() > 80) {
        Serial.print(F("[WARN] S9 buffer overflow: "));
        Serial.println(s9Buffer);
        s9Buffer = "";  // ← Silently discards data!
      }
    }
  }
}
```

When buffer overflows, the command is **printed as a warning but discarded**. The S9 never gets an error response, so it doesn't know the command failed.

**Fix:** Send error back to S9:
```cpp
if (s9Buffer.length() > 80) {
  toS9("ERR|BUFFER_OVERFLOW|END");
  s9Buffer = "";
}
```

---

#### **Issue 3.8: Missing Null Terminator in R4 Serial Buffer**
**Severity:** MEDIUM - Potential Crash  
**File:** `BuddyBot_R4_Dash.ino:1324-1360`

**Problem:**
```cpp
void handleMegaLink() {
  static char megaBuf[100];
  static uint8_t megaLen = 0;
  while (Serial1.available()) {
    char c = Serial1.read();
    if (c == '\r') continue;
    if (c == '\n') {
      if (megaLen == 0) continue;
      megaBuf[megaLen] = '\0';  // ← Good, null-terminated
      String raw = String(megaBuf);
      // ...
    } else {
      if (megaLen + 1 < sizeof(megaBuf)) {
        megaBuf[megaLen++] = c;
      } else {
        megaLen = 0;  // ← Overflow: resets without null terminator!
      }
    }
  }
}
```

If buffer overflows, `megaLen` is reset to 0 but the buffer still contains garbage. Next newline will create a String from uninitialized data.

**Fix:**
```cpp
} else {
  megaLen = 0;
  megaBuf[0] = '\0';  // ← Clear the buffer
}
```

---

#### **Issue 3.9: Uninitialized Sensor Status String**
**Severity:** MEDIUM - Potential Garbage Data  
**File:** `BuddyBot_ESP32_V2.ino:290`

**Problem:**
```cpp
String lastSensStatus = "";  // ← Initialized empty

// In handleMegaSerial():
} else if (buf.startsWith("SENS_ST|")) {
  int end = buf.indexOf("|END");
  lastSensStatus = (end > 0) ? buf.substring(8, end) : buf.substring(8);
  Serial.print("[ESP32] Sensor status: "); Serial.println(lastSensStatus);
}

// In handleStatus():
if (lastSensStatus.length() > 0) {
  String safeSensStatus = escapeJsonString(lastSensStatus);
  json += ",\"sens_status\":\"" + safeSensStatus + "\"";
}
```

If Mega never sends `SENS_ST|`, the web UI will never show sensor status. No fallback or default.

**Fix:** Initialize with default sensor state:
```cpp
String lastSensStatus = "DHT:1|LIGHT:1|SOUND:1|GAS:1|FLAME:1|PIR:0|TILT:1|IR:1|US:1|CURRENT:1|GPS:1";
```

---

### 🟡 HIGH-PRIORITY ISSUES

#### **Issue 3.10: No Acknowledgment Timeout for R3 Commands**
**Severity:** HIGH - Hangs on R3 Failure  
**File:** `BuddyBot_Mega_V29.ino:469-496`

**Problem:**
```cpp
void sendMotor(const char* cmd) {
  // ... update nav state ...
  strncpy(mQueue.cmd, cmd, sizeof(mQueue.cmd));
  mQueue.pending = true;
}

void drainMotorQueue() {
  if (!mQueue.pending) return;
  motorCommPrintln(mQueue.cmd);
  mQueue.pending = false;  // ← Immediately clears, no ACK wait
}
```

If R3 crashes or loses serial connection, Mega doesn't know. It sends the command and immediately marks it as done, even if R3 never received it.

**Fix:** Implement ACK timeout:
```cpp
struct MotorCmd { 
  char cmd[24]; 
  bool pending; 
  unsigned long sentTime;
  int retries;
} mQueue = { "", false, 0, 0 };

void drainMotorQueue() {
  if (!mQueue.pending) return;
  unsigned long now = millis();
  
  if (mQueue.sentTime == 0) {
    motorCommPrintln(mQueue.cmd);
    mQueue.sentTime = now;
  } else if (now - mQueue.sentTime > 1000) {
    if (mQueue.retries < 3) {
      motorCommPrintln(mQueue.cmd);
      mQueue.sentTime = now;
      mQueue.retries++;
    } else {
      toS9("ERR|R3_NO_ACK|END");
      mQueue.pending = false;
      mQueue.retries = 0;
    }
  }
}
```

---

#### **Issue 3.11: Telemetry Broadcast Timing Mismatch**
**Severity:** HIGH - Inconsistent Data  
**File:** `BuddyBot_Mega_V29.ino:1528-1534`

**Problem:**
```cpp
// Telemetry broadcast (1000ms)
if (now - lastTelem > 1000) {
  lastTelem = now;
  sendTelemetryToR4();    // Sends STAT, US, PWR, STATUS
  sendTelemetryToESP32(); // Sends TELEM, STAT, US
  uptimeSec++;
}
```

Both functions send `STAT:` and `US:` messages, but they're called together. If one fails, the other still sends, creating inconsistent state on R4 and ESP32.

**Fix:** Stagger the broadcasts:
```cpp
if (now - lastTelem > 500) {
  lastTelem = now;
  if ((uptimeSec % 2) == 0) {
    sendTelemetryToR4();
  } else {
    sendTelemetryToESP32();
  }
  uptimeSec++;
}
```

---

#### **Issue 3.12: No Heartbeat/Watchdog for R4 Connection**
**Severity:** HIGH - Silent Disconnection  
**File:** `BuddyBot_Mega_V29.ino` (missing)

**Problem:**
Mega has S9 heartbeat timeout (line 1513-1516):
```cpp
if (s9Connected && (now - s9LastHB > S9_TIMEOUT)) {
  s9Connected = false;
  dbg("[S9] Disconnected");
}
```

But **no equivalent for R4**. If R4 crashes or loses serial, Mega keeps sending telemetry to a dead port.

**Fix:** Add R4 watchdog:
```cpp
bool r4Connected = false;
unsigned long r4LastHB = 0;
const unsigned long R4_TIMEOUT = 6000;

// In handleR4Communication():
r4LastHB = millis();
r4Connected = true;

// In loop():
if (r4Connected && (now - r4LastHB > R4_TIMEOUT)) {
  r4Connected = false;
  dbg("[R4] Disconnected");
}

// In sendTelemetryToR4():
if (!r4Connected) return;  // Don't send to dead port
```

---

#### **Issue 3.13: ESP32 Handshake Race Condition**
**Severity:** HIGH - Missed Initialization  
**File:** `BuddyBot_Mega_V29.ino:1566-1570`

**Problem:**
```cpp
// ESP32 handshake re-send (5s interval) to catch late-boot ESP32
if (!esp32Ready && (now - lastEsp32Check > 5000)) {
  lastEsp32Check = now;
  Serial3.println(F("MEGA:BOOT"));
}
```

Mega sends `MEGA:BOOT` every 5 seconds until ESP32 responds with `READY`. But:
1. If ESP32 boots after Mega, it might miss the first `MEGA:BOOT`
2. If ESP32 crashes, Mega never knows (no timeout)
3. No maximum retry limit

**Fix:**
```cpp
const int MAX_ESP32_RETRIES = 12;  // 60 seconds max
int esp32Retries = 0;

if (!esp32Ready && (now - lastEsp32Check > 5000)) {
  if (esp32Retries < MAX_ESP32_RETRIES) {
    lastEsp32Check = now;
    Serial3.println(F("MEGA:BOOT"));
    esp32Retries++;
  } else {
    toS9("WARN|ESP32_TIMEOUT|END");
    esp32Ready = true;  // Give up and continue
  }
}
```

---

#### **Issue 3.14: Collision Avoidance Sensor Staleness Check Too Loose**
**Severity:** MEDIUM - Stale Data Reaction  
**File:** `BuddyBot_Mega_V29.ino:1320-1321`

**Problem:**
```cpp
void collisionAvoidance() {
  if (millis() - lastSenseTs > 600) return;  // stale data — don't react
```

Sensor data is read every 500ms (line 1519), so 600ms staleness means **one missed sensor cycle**. In autonomous mode, the robot could hit an obstacle if sensors lag.

**Fix:** Tighten to 300ms (one sensor cycle + buffer):
```cpp
if (millis() - lastSenseTs > 300) return;  // stale data — don't react
```

---

#### **Issue 3.15: No Validation of Parsed Telemetry Values**
**Severity:** MEDIUM - Garbage Data Display  
**File:** `BuddyBot_R4_Dash.ino:1416-1442`

**Problem:**
```cpp
if (raw.startsWith("STAT:")) {
  tok = strtok(buf + 5, ":"); int i = 0;
  while (tok) {
    if (i==0)  botGas  = atoi(tok);      // ← No range check
    if (i==1)  botTemp = atof(tok);      // ← Could be -999 or 999
    if (i==2)  botHum  = atof(tok);      // ← Could be > 100%
    if (i==8)  botVolt = atof(tok);      // ← Could be 0 or 50V
    if (i==9)  botPct  = constrain(atoi(tok),0,100);  // ← Only this one validated!
    if (i==10) botAmps = atof(tok);      // ← No range check
    tok = strtok(NULL,":"); i++;
  }
}
```

If Mega sends garbage (e.g., due to serial corruption), R4 displays it without validation.

**Fix:** Add range checks:
```cpp
if (i==0)  botGas  = constrain(atoi(tok), 0, 1023);
if (i==1)  botTemp = constrain(atof(tok), -10.0f, 60.0f);
if (i==2)  botHum  = constrain(atof(tok), 0.0f, 100.0f);
if (i==8)  botVolt = constrain(atof(tok), 5.0f, 9.0f);
if (i==10) botAmps = constrain(atof(tok), 0.0f, 50.0f);
```

---

### 🟠 MEDIUM-PRIORITY ISSUES

#### **Issue 3.16: No Escape Handling in JSON Strings**
**Severity:** MEDIUM - Potential JSON Corruption  
**File:** `BuddyBot_ESP32_V2.ino:459-467`

**Problem:**
```cpp
String escapeJsonString(const String& s) {
  String result = "";
  for (size_t i = 0; i < s.length(); i++) {
    if (s[i] == '"' || s[i] == '\\') result += '\\';
    result += s[i];
  }
  return result;
}
```

This only escapes `"` and `\`. It misses:
- Newlines (`\n`)
- Carriage returns (`\r`)
- Tabs (`\t`)
- Control characters

If sensor status string contains these, JSON breaks.

**Fix:**
```cpp
String escapeJsonString(const String& s) {
  String result = "";
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '"' || c == '\\') result += '\\';
    if (c == '\n') { result += "\\n"; continue; }
    if (c == '\r') { result += "\\r"; continue; }
    if (c == '\t') { result += "\\t"; continue; }
    if (c < 32) continue;  // Skip control chars
    result += c;
  }
  return result;
}
```

---

#### **Issue 3.17: R3 Pattern Completion Not Verified**
**Severity:** MEDIUM - Stuck Patterns  
**File:** `BuddyBot_R3_Motors.ino:219-278`

**Problem:**
```cpp
void dancePattern() {
  if (danceState == DANCE_IDLE) { /* ... */ }
  if (danceState == DANCE_WIGGLE && millis() >= patternTimer) { /* ... */ }
  // ... 6 more state checks ...
  if (danceState == DANCE_SPIN && millis() >= patternTimer) {
    stopAll();
    currentSpeed = savedSpeed;
    megaSerial.println(F("ACK:DANCE:DONE"));
    danceState = DANCE_IDLE;
    return;
  }
}
```

If `patternTimer` is never reached (e.g., millis() overflow or timer corruption), the pattern **never completes**. Mega waits forever for `ACK:DANCE:DONE`.

**Fix:** Add timeout:
```cpp
const unsigned long PATTERN_TIMEOUT = 15000;  // 15 seconds max

if (danceState != DANCE_IDLE && millis() - patternTimer > PATTERN_TIMEOUT) {
  stopAll();
  currentSpeed = savedSpeed;
  megaSerial.println(F("ACK:DANCE:DONE"));
  danceState = DANCE_IDLE;
}
```

---

#### **Issue 3.18: No Bounds Check on Ultrasonic Distances**
**Severity:** MEDIUM - Invalid Navigation  
**File:** `BuddyBot_Mega_V29.ino:446-453`

**Problem:**
```cpp
long getDist(int trig, int echo) {
  if (!sens.us) return -1;
  digitalWrite(trig, LOW);  delayMicroseconds(2);
  digitalWrite(trig, HIGH); delayMicroseconds(10);
  digitalWrite(trig, LOW);
  long dur = pulseIn(echo, HIGH, 10000);
  return dur > 0 ? dur / 58 : -1;  // ← No upper bound check
}
```

`pulseIn()` can return up to 10000 microseconds. Divided by 58, that's ~172cm. But if the sensor is faulty and returns max value, it could be interpreted as a valid distance.

**Fix:**
```cpp
long getDist(int trig, int echo) {
  if (!sens.us) return -1;
  digitalWrite(trig, LOW);  delayMicroseconds(2);
  digitalWrite(trig, HIGH); delayMicroseconds(10);
  digitalWrite(trig, LOW);
  long dur = pulseIn(echo, HIGH, 10000);
  if (dur <= 0 || dur > 5800) return -1;  // -1 = invalid (>100cm)
  return dur / 58;
}
```

---

#### **Issue 3.19: Battery Percentage Calculation Inconsistency**
**Severity:** MEDIUM - Misleading Display  
**File:** `BuddyBot_Mega_V29.ino:523-525` vs `BuddyBot_ESP32_V2.ino:477`

**Problem:**
```cpp
// MEGA (line 523-525)
float bRange = BAT_MAX - BAT_MIN;  // 8.4 - 6.0 = 2.4
battPct = constrain(((battVolt - BAT_MIN) / bRange) * 100.0f, 0.0f, 100.0f);

// ESP32 (line 477)
json += "\"pct\":" + String(int((telem.battery.toFloat() - 6.0f) / (8.4f - 6.0f) * 100.0f)) + ",";
```

Both use the same formula, but:
- Mega uses `BAT_MAX` and `BAT_MIN` constants
- ESP32 hardcodes `6.0f` and `8.4f`

If constants change in Mega, ESP32 won't update. Also, R4 doesn't calculate percentage—it just displays what Mega sends.

**Fix:** Have Mega send percentage, not voltage:
```cpp
// MEGA
int battPctInt = (int)battPct;
Serial3.print("BAT:"); Serial3.println(battPctInt);

// ESP32
json += "\"pct\":" + String(battPct) + ",";
```

---

#### **Issue 3.20: No Debounce on Gesture Sensor**
**Severity:** MEDIUM - Double Triggers  
**File:** `BuddyBot_Mega_V29.ino:1146-1162`

**Problem:**
```cpp
void checkGestures() {
  uint8_t data = 0;
  paj7620ReadReg(0x43, 1, &data);
  if (data == 0) return;
  const char* g = nullptr;
  if      (data == GES_UP_FLAG)        { g = "UP";    sendMotor("FORWARD"); }
  // ... more gestures ...
  if (g) {
    Serial1.print(F("GESTURE:")); Serial1.println(g);
    toS9("GESTURE:" + String(g));
    Serial3.print(F("GESTURE:")); Serial3.println(g);
  }
}
```

Called every 500ms (line 1524). If gesture flag persists for >500ms, it triggers twice. No debounce or edge detection.

**Fix:**
```cpp
static uint8_t lastGestureData = 0;

void checkGestures() {
  uint8_t data = 0;
  paj7620ReadReg(0x43, 1, &data);
  if (data == 0 || data == lastGestureData) return;  // ← Edge detection
  lastGestureData = data;
  
  const char* g = nullptr;
  if      (data == GES_UP_FLAG)        { g = "UP";    sendMotor("FORWARD"); }
  // ... rest ...
}
```

---

## SECTION 4: ALL GOOD POINTS

### ✅ Strengths

1. **Clear Serial Channel Map** (Mega V29 lines 40-46)
   - Authoritative documentation of all serial ports
   - Baud rates clearly specified
   - Pin assignments locked

2. **Non-Blocking Motor Queue** (Mega V29 lines 399-496)
   - Prevents SoftSerial from blocking main loop
   - Good architecture for responsive system

3. **Sensor Toggle System** (Mega V29 lines 250-314)
   - Runtime sensor enable/disable
   - Broadcast to all clients (S9, R4, ESP32)
   - Prevents invalid readings from disabled sensors

4. **Tiered Battery Warnings** (Mega V29 lines 684-718)
   - Multiple alert levels (WARN, LOW, CRITICAL)
   - Edge-triggered (no spam)
   - Sent to all clients

5. **Collision Avoidance with Freshness Guard** (Mega V29 lines 1320-1396)
   - Sensor staleness check prevents stale data reactions
   - Multiple avoidance strategies (US, IR, stuck detection)
   - Non-blocking state machine

6. **Comprehensive Telemetry** (Mega V29 lines 582-677)
   - Multiple message formats (STAT, US, PWR, STATUS)
   - Sent to both R4 and ESP32
   - Includes all critical sensor data

7. **ESTOP Auto-Recovery** (Mega V29 lines 791-815)
   - Automatic retry with timer reset
   - Max retry limit (3x)
   - Prevents infinite loops

8. **R4 Game Architecture** (R4 V23 lines 88-102)
   - Clean state machine (S_SPLASH, S_MAIN, S_GAMES, etc.)
   - Modular game functions
   - Celebration particle effects

9. **ESP32 Web Dashboard** (ESP32 V2 lines 84-284)
   - Responsive HTML/CSS
   - Real-time JSON polling
   - Sensor toggle UI
   - Flame alert banner

10. **Bluetooth Serial Bridge** (ESP32 V2 lines 411-431)
    - Accepts commands from BT terminal
    - Relays to Mega
    - Confirms with "OK:" response

11. **R3 Motor Shield Abstraction** (R3 V1 lines 100-156)
    - Clean shift-register interface
    - PWM speed control
    - Pattern state machines (DANCE, DEFENSE)

12. **Gesture Sensor Integration** (Mega V29 lines 1146-1162)
    - Broadcasts to S9, R4, and ESP32
    - Triggers motor commands
    - Supports 6 gesture types

13. **RF Remote Support** (Mega V29 lines 1118-1141)
    - 433MHz receiver with RCSwitch
    - Debounce logic (500ms)
    - Autonomous mode toggle

14. **GPS Integration** (Mega V29 lines 1102-1110)
    - TinyGPS++ library
    - Non-blocking parsing
    - Satellite count tracking

15. **Startup Sequence** (Mega V29 lines 1437-1444)
    - Headlight blink pattern
    - Beep tones
    - Fan test
    - Professional boot experience

---

## SECTION 5: RECOMMENDED IMPROVEMENTS & REFACTORED CODE

### 5.1 Fix Critical Syntax Errors

**File: `BuddyBot_Mega_V29.ino:1449-1451`**

```cpp
// BEFORE (BROKEN)
void setup() {
  // Stop motors immediately on boot
  MOTOR|S;  // ← SYNTAX ERROR

  Serial.begin(115200);
  // ...
}

// AFTER (FIXED)
void setup() {
  // Initialize serial first
  Serial.begin(115200);
  
  // ✅ FIX: Wait for USB serial port enumeration (critical fix for missing serial output)
  while (!Serial && millis() < 4000) {
    // Wait up to 4 seconds for host to connect
  }
  delay(250); // Extra stabilization buffer
  
  Serial1.begin(115200);   // → UNO R4 WiFi
  Serial2.begin(9600);     // → GPS NEO-6M
  Serial3.begin(115200);   // → ESP32
  motorComm.begin(9600);   // → UNO R3 Motor Shield
  
  delay(500);
  Wire.begin();
  dht.begin();
  initPins();
  
  // Motor stop will be sent via SoftwareSerial after initialization
  // (no need for MOTOR|S here — it will be sent after serial link stabilizes)
  
  rfReceiver.enableReceive(digitalPinToInterrupt(RF_PIN));
  attachInterrupt(digitalPinToInterrupt(CURRENT_SENSOR), currentPulseISR, RISING);
  
  if (paj7620Init() == 0) { dbg("[INIT] PAJ7620 OK"); }
  else                     { dbg("[INIT] PAJ7620 FAILED — gesture sensor absent"); }
  
  readAllSensors();
  startupSequence();
  systemReady = true;
  nav.lastAvoidEnd = millis();
  
  // Announce to all channels
  toS9("SYSTEM|READY|" + String(FW_VERSION) + "|END");
  Serial1.println(F("STAT|READY"));
  Serial1.println(sensorStatusString());   // R4 sensor config screen initial state
  Serial3.println(F("MEGA:BOOT"));         // ESP32 boot handshake
  
  runR3CommTest();
  
  dbg("[READY] BuddyBot " FW_VERSION);
}
```

**File: `BuddyBot_R3_Motors.ino:425-459`**

```cpp
// BEFORE (BROKEN)
void setup() {
  // Disable motor outputs immediately, before anything else
  MOTOR|S  // ← SYNTAX ERROR
  megaSerial.begin(9600);
  
  // ✅ FIX: Wait for serial link to stabilize before transmitting
  delay(1000);
  // ...
}

// AFTER (FIXED)
void setup() {
  // Initialize serial first
  megaSerial.begin(9600);
  
  // ✅ FIX: Wait for serial link to stabilize before transmitting
  delay(1000);
  
  pinMode(MOTORENABLE, OUTPUT);
  digitalWrite(MOTORENABLE, HIGH);  // HIGH = disabled
  
  pinMode(MOTORLATCH, OUTPUT);
  digitalWrite(MOTORLATCH, LOW);
  pinMode(MOTORCLK,   OUTPUT);
  digitalWrite(motorclk, LOW);
  pinMode(MOTORDATA,  OUTPUT);
  digitalWrite(MOTORDATA, LOW);
  
  // Shift a clean zero pattern into the 74HC595 before enabling outputs
  shiftOut(MOTORDATA, MOTORCLK, MSBFIRST, 0x00);
  delayMicroseconds(5);
  digitalWrite(MOTORLATCH, HIGH);
  delayMicroseconds(5);
  digitalWrite(MOTORLATCH, LOW);
  
  digitalWrite(MOTORENABLE, LOW);   // Enable outputs only after safe zero state
  
  // Ensure all motors are released and PWM channels are zeroed
  stopAll();
  
  // Brief delay so Mega can boot and start listening
  delay(500);
  
  // Announce ready
  megaSerial.println(F("R3:READY:BUDDYBOT_MOTOR:V1.0"));
}
```

---

### 5.2 Fix toS9() Function

**File: `BuddyBot_Mega_V29.ino:439-442`**

```cpp
// BEFORE (BROKEN - doesn't send to S9)
void toS9(const String& msg)  { 
  Serial.print(F("[SEND] S9 Response: ")); 
  Serial.println(msg); 
}

// AFTER (FIXED)
void toS9(const String& msg)  { 
  Serial.print(F("[SEND] S9 Response: ")); 
  Serial.println(msg);
  Serial.println(msg);  // ← Actually send to S9!
}
```

---

### 5.3 Implement Proper Motor Queue with ACK Timeout

**File: `BuddyBot_Mega_V29.ino:399-496`**

```cpp
// BEFORE (single-slot buffer, no ACK timeout)
struct MotorCmd { char cmd[24]; bool pending; } mQueue = { "", false };

void sendMotor(const char* cmd) {
  if      (strcmp(cmd, "FORWARD")  == 0) { nav.isMoving=true;  nav.isReversing=false; }
  else if (strcmp(cmd, "BACKWARD") == 0) { nav.isMoving=true;  nav.isReversing=true;  }
  else if (strcmp(cmd, "LEFT")     == 0) { nav.isMoving=true;  nav.isReversing=false; }
  else if (strcmp(cmd, "RIGHT")    == 0) { nav.isMoving=true;  nav.isReversing=false; }
  else if (strcmp(cmd, "STOP")     == 0) { nav.isMoving=false; nav.isReversing=false; }

  if      (strcmp(cmd, "FORWARD")  == 0) strncpy(mQueue.cmd, "MOTOR|F",      sizeof(mQueue.cmd));
  else if (strcmp(cmd, "BACKWARD") == 0) strncpy(mQueue.cmd, "MOTOR|B",      sizeof(mQueue.cmd));
  else if (strcmp(cmd, "LEFT")     == 0) strncpy(mQueue.cmd, "MOTOR|L",      sizeof(mQueue.cmd));
  else if (strcmp(cmd, "RIGHT")    == 0) strncpy(mQueue.cmd, "MOTOR|R",      sizeof(mQueue.cmd));
  else if (strcmp(cmd, "STOP")     == 0) strncpy(mQueue.cmd, "MOTOR|S",      sizeof(mQueue.cmd));
  else if (strcmp(cmd, "SLOW")     == 0) strncpy(mQueue.cmd, "SPEED:SLOW",   sizeof(mQueue.cmd));
  else if (strcmp(cmd, "NORMAL")   == 0) strncpy(mQueue.cmd, "SPEED:NORMAL", sizeof(mQueue.cmd));
  else if (strcmp(cmd, "FAST")     == 0) strncpy(mQueue.cmd, "SPEED:FAST",   sizeof(mQueue.cmd));
  else                                   strncpy(mQueue.cmd, cmd,             sizeof(mQueue.cmd));

  mQueue.pending = true;
  if (debugVerbose) { Serial.print(F("[MTR>Q] ")); Serial.println(mQueue.cmd); }
}

void drainMotorQueue() {
  if (!mQueue.pending) return;
  motorCommPrintln(mQueue.cmd);
  mQueue.pending = false;
}

// AFTER (proper queue with ACK timeout)
struct MotorCmd { 
  char cmd[24]; 
  bool pending; 
  unsigned long sentTime;
  int retries;
} mQueue = { "", false, 0, 0 };

const int MOTOR_ACK_TIMEOUT = 1000;  // 1 second
const int MOTOR_MAX_RETRIES = 3;

void sendMotor(const char* cmd) {
  if      (strcmp(cmd, "FORWARD")  == 0) { nav.isMoving=true;  nav.isReversing=false; }
  else if (strcmp(cmd, "BACKWARD") == 0) { nav.isMoving=true;  nav.isReversing=true;  }
  else if (strcmp(cmd, "LEFT")     == 0) { nav.isMoving=true;  nav.isReversing=false; }
  else if (strcmp(cmd, "RIGHT")    == 0) { nav.isMoving=true;  nav.isReversing=false; }
  else if (strcmp(cmd, "STOP")     == 0) { nav.isMoving=false; nav.isReversing=false; }

  if      (strcmp(cmd, "FORWARD")  == 0) strncpy(mQueue.cmd, "MOTOR|F",      sizeof(mQueue.cmd));
  else if (strcmp(cmd, "BACKWARD") == 0) strncpy(mQueue.cmd, "MOTOR|B",      sizeof(mQueue.cmd));
  else if (strcmp(cmd, "LEFT")     == 0) strncpy(mQueue.cmd, "MOTOR|L",      sizeof(mQueue.cmd));
  else if (strcmp(cmd, "RIGHT")    == 0) strncpy(mQueue.cmd, "MOTOR|R",      sizeof(mQueue.cmd));
  else if (strcmp(cmd, "STOP")     == 0) strncpy(mQueue.cmd, "MOTOR|S",      sizeof(mQueue.cmd));
  else if (strcmp(cmd, "SLOW")     == 0) strncpy(mQueue.cmd, "SPEED:SLOW",   sizeof(mQueue.cmd));
  else if (strcmp(cmd, "NORMAL")   == 0) strncpy(mQueue.cmd, "SPEED:NORMAL", sizeof(mQueue.cmd));
  else if (strcmp(cmd, "FAST")     == 0) strncpy(mQueue.cmd, "SPEED:FAST",   sizeof(mQueue.cmd));
  else                                   strncpy(mQueue.cmd, cmd,             sizeof(mQueue.cmd));

  mQueue.pending = true;
  mQueue.sentTime = 0;  // Reset timer
  mQueue.retries = 0;
  if (debugVerbose) { Serial.print(F("[MTR>Q] ")); Serial.println(mQueue.cmd); }
}

void drainMotorQueue() {
  if (!mQueue.pending) return;
  unsigned long now = millis();
  
  // First send
  if (mQueue.sentTime == 0) {
    motorCommPrintln(mQueue.cmd);
    mQueue.sentTime = now;
    if (debugVerbose) { Serial.print(F("[MTR>SEND] ")); Serial.println(mQueue.cmd); }
    return;
  }
  
  // Timeout: retry or fail
  if (now - mQueue.sentTime > MOTOR_ACK_TIMEOUT) {
    if (mQueue.retries < MOTOR_MAX_RETRIES) {
      motorCommPrintln(mQueue.cmd);
      mQueue.sentTime = now;
      mQueue.retries++;
      if (debugVerbose) { Serial.print(F("[MTR>RETRY] ")); Serial.print(mQueue.retries); Serial.print(": "); Serial.println(mQueue.cmd); }
    } else {
      toS9("ERR|R3_NO_ACK|" + String(mQueue.cmd) + "|END");
      Serial.print(F("[MTR>FAIL] No ACK after ")); Serial.print(MOTOR_MAX_RETRIES); Serial.print(" retries: "); Serial.println(mQueue.cmd);
      mQueue.pending = false;
      mQueue.retries = 0;
      mQueue.sentTime = 0;
    }
  }
}
```

---

### 5.4 Add R4 Connection Watchdog

**File: `BuddyBot_Mega_V29.ino` (add to global state section)**

```cpp
// ── R4 connection tracking ──────────────────────────────────────
bool          r4Connected = false;
unsigned long r4LastHB    = 0;
const unsigned long R4_TIMEOUT = 6000;

// In handleR4Communication() (line 968):
void handleR4Communication() {
  while (Serial1.available()) {
    char c = Serial1.read();
    r4LastHB = millis();  // ← Update heartbeat
    r4Connected = true;   // ← Mark as connected
    
    if (c == '\n') {
      r4Buf.trim();
      if (r4Buf.length() > 0) processR4Command(r4Buf);
      r4Buf = "";
    } else if (c != '\r') {
      r4Buf += c;
      if (r4Buf.length() > 80) r4Buf = "";
    }
  }
}

// In loop() (after S9 watchdog, line 1516):
// R4 connection watchdog
if (r4Connected && (now - r4LastHB > R4_TIMEOUT)) {
  r4Connected = false;
  dbg("[R4] Disconnected");
}

// In sendTelemetryToR4() (line 582):
void sendTelemetryToR4() {
  if (!r4Connected) return;  // ← Don't send to dead port
  
  // ... rest of function ...
}
```

---

### 5.5 Improve Sensor Data Validation

**File: `BuddyBot_R4_Dash.ino:1416-1442`**

```cpp
// BEFORE (no validation)
if (raw.startsWith("STAT:")) {
  tok = strtok(buf + 5, ":"); int i = 0;
  while (tok) {
    if (i==0)  botGas  = atoi(tok);
    if (i==1)  botTemp = atof(tok);
    if (i==2)  botHum  = atof(tok);
    if (i==3)  botHaz  = atoi(tok);
    if (i==8)  botVolt = atof(tok);
    if (i==9)  botPct  = constrain(atoi(tok),0,100);
    if (i==10) botAmps = atof(tok);
    tok = strtok(NULL,":"); i++;
  }
  continue;
}

// AFTER (with validation)
if (raw.startsWith("STAT:")) {
  tok = strtok(buf + 5, ":"); int i = 0;
  while (tok) {
    if (i==0)  botGas  = constrain(atoi(tok), 0, 1023);
    if (i==1)  botTemp = constrain(atof(tok), -10.0f, 60.0f);
    if (i==2)  botHum  = constrain(atof(tok), 0.0f, 100.0f);
    if (i==3)  botHaz  = (atoi(tok) != 0) ? true : false;
    if (i==8)  botVolt = constrain(atof(tok), 5.0f, 9.0f);
    if (i==9)  botPct  = constrain(atoi(tok), 0, 100);
    if (i==10) botAmps = constrain(atof(tok), 0.0f, 50.0f);
    tok = strtok(NULL,":"); i++;
  }
  sensNeedsRedraw = true;
  continue;
}
```

---

### 5.6 Add Gesture Debounce

**File: `BuddyBot_Mega_V29.ino:1146-1162`**

```cpp
// BEFORE (no debounce)
void checkGestures() {
  uint8_t data = 0;
  paj7620ReadReg(0x43, 1, &data);
  if (data == 0) return;
  const char* g = nullptr;
  if      (data == GES_UP_FLAG)        { g = "UP";    sendMotor("FORWARD"); }
  else if (data == GES_DOWN_FLAG)      { g = "DOWN";  sendMotor("BACKWARD"); }
  else if (data == GES_LEFT_FLAG)      { g = "LEFT";  sendMotor("LEFT"); }
  else if (data == GES_RIGHT_FLAG)     { g = "RIGHT"; sendMotor("RIGHT"); }
  else if (data == GES_FORWARD_FLAG)   { g = "NEAR";  sendMotor("STOP"); }
  else if (data == GES_CLOCKWISE_FLAG) { g = "CW";    motorCommPrintln(F("MOTOR|DANCE")); }
  if (g) {
    Serial1.print(F("GESTURE:")); Serial1.println(g);
    toS9("GESTURE:" + String(g));
    Serial3.print(F("GESTURE:")); Serial3.println(g);
  }
}

// AFTER (with edge detection)
static uint8_t lastGestureData = 0;

void checkGestures() {
  uint8_t data = 0;
  paj7620ReadReg(0x43, 1, &data);
  if (data == 0 || data == lastGestureData) return;  // ← Edge detection
  lastGestureData = data;
  
  const char* g = nullptr;
  if      (data == GES_UP_FLAG)        { g = "UP";    sendMotor("FORWARD"); }
  else if (data == GES_DOWN_FLAG)      { g = "DOWN";  sendMotor("BACKWARD"); }
  else if (data == GES_LEFT_FLAG)      { g = "LEFT";  sendMotor("LEFT"); }
  else if (data == GES_RIGHT_FLAG)     { g = "RIGHT"; sendMotor("RIGHT"); }
  else if (data == GES_FORWARD_FLAG)   { g = "NEAR";  sendMotor("STOP"); }
  else if (data == GES_CLOCKWISE_FLAG) { g = "CW";    motorCommPrintln(F("MOTOR|DANCE")); }
  
  if (g) {
    Serial1.print(F("GESTURE:")); Serial1.println(g);
    toS9("GESTURE:" + String(g));
    Serial3.print(F("GESTURE:")); Serial3.println(g);
  }
}
```

---

### 5.7 Improve Ultrasonic Distance Validation

**File: `BuddyBot_Mega_V29.ino:446-453`**

```cpp
// BEFORE (no bounds check)
long getDist(int trig, int echo) {
  if (!sens.us) return -1;
  digitalWrite(trig, LOW);  delayMicroseconds(2);
  digitalWrite(trig, HIGH); delayMicroseconds(10);
  digitalWrite(trig, LOW);
  long dur = pulseIn(echo, HIGH, 10000);
  return dur > 0 ? dur / 58 : -1;
}

// AFTER (with bounds check)
long getDist(int trig, int echo) {
  if (!sens.us) return -1;
  digitalWrite(trig, LOW);  delayMicroseconds(2);
  digitalWrite(trig, HIGH); delayMicroseconds(10);
  digitalWrite(trig, LOW);
  long dur = pulseIn(echo, HIGH, 10000);
  
  // Validate: 58 microseconds per cm, max ~400cm (23200 microseconds)
  // Practical max: 200cm (11600 microseconds)
  if (dur <= 0 || dur > 11600) return -1;  // -1 = invalid
  
  long dist = dur / 58;
  return constrain(dist, 0, 400);  // Clamp to 0-400cm
}
```

---

## SECTION 6: TESTING CHECKLIST

### 6.1 Unit Tests

- [ ] **Mega ↔ R3 SoftwareSerial**
  - [ ] PING → PONG response
  - [ ] MOTOR|F → ACK:MOTOR|F
  - [ ] MOTOR|DANCE → ACK:DANCE:DONE
  - [ ] Timeout handling (R3 not responding)
  - [ ] Buffer overflow (>80 chars)

- [ ] **Mega ↔ R4 Serial1**
  - [ ] STAT: parsing (all 11 fields)
  - [ ] US: parsing (4 distances)
  - [ ] MODE: command relay
  - [ ] TOGGLE_SENSOR: command relay
  - [ ] Timeout handling (R4 not responding)

- [ ] **Mega ↔ ESP32 Serial3**
  - [ ] MEGA:BOOT → READY handshake
  - [ ] TELEM: parsing
  - [ ] STAT: parsing
  - [ ] CMD: relay from web/BT
  - [ ] Timeout handling (ESP32 not responding)

- [ ] **Mega ↔ S9 USB Serial**
  - [ ] MOTOR:F → ACK|MOTOR:F|END
  - [ ] GESTURE:UP → received on S9
  - [ ] TELE: status → received on S9
  - [ ] Buffer overflow (>80 chars)
  - [ ] Timeout handling (S9 disconnected)

### 6.2 Integration Tests

- [ ] **Motor Command Pipeline**
  - [ ] S9 sends MOTOR:F → Mega queues → R3 executes
  - [ ] R3 sends ACK → Mega relays to S9
  - [ ] Timeout: R3 no ACK → Mega retries 3x → fails gracefully

- [ ] **Telemetry Pipeline**
  - [ ] Mega reads sensors → sends STAT to R4 & ESP32
  - [ ] R4 parses STAT → displays on screen
  - [ ] ESP32 parses STAT → sends JSON to web UI
  - [ ] Web UI displays telemetry

- [ ] **Mode Change Pipeline**
  - [ ] S9 sends MODE:BODYGUARD → Mega relays to R4 & ESP32
  - [ ] R4 receives MODE: → updates botMode variable
  - [ ] ESP32 receives MODE: → updates web UI
  - [ ] All three boards in sync

- [ ] **Gesture Pipeline**
  - [ ] PAJ7620 detects gesture → Mega reads
  - [ ] Mega sends GESTURE:UP to S9, R4, ESP32
  - [ ] S9 plays animation
  - [ ] R4 logs gesture
  - [ ] ESP32 broadcasts to web UI

### 6.3 Robustness Tests

- [ ] **Serial Corruption**
  - [ ] Inject random bytes into S9 serial
  - [ ] Inject random bytes into R3 serial
  - [ ] Verify no crashes, graceful error handling

- [ ] **Power Loss Simulation**
  - [ ] Unplug R3 → Mega detects no ACK → retries → fails
  - [ ] Unplug R4 → Mega detects timeout → stops sending
  - [ ] Unplug ESP32 → Mega detects no READY → retries

- [ ] **Timing Stress**
  - [ ] Send 100 MOTOR commands in 1 second
  - [ ] Verify queue doesn't overflow
  - [ ] Verify all commands eventually execute

- [ ] **Memory Leaks**
  - [ ] Run for 24 hours
  - [ ] Monitor free heap (should not decrease)
  - [ ] Verify no buffer overflows

---

## SECTION 7: DEPLOYMENT CHECKLIST

Before deploying to production:

- [ ] **Fix all CRITICAL issues** (3.1-3.9)
- [ ] **Fix all HIGH issues** (3.10-3.15)
- [ ] **Address MEDIUM issues** (3.16-3.20)
- [ ] **Run all unit tests** (6.1)
- [ ] **Run all integration tests** (6.2)
- [ ] **Run all robustness tests** (6.3)
- [ ] **Code review** by second engineer
- [ ] **Hardware verification** (all boards present, wiring correct)
- [ ] **Firmware version check** (Mega V29.0, R3 V1.0, R4 V23.0, ESP32 V2.0)
- [ ] **Serial monitor logging** (verify all [SEND]/[RECV] messages)
- [ ] **24-hour stability test** (no crashes, no memory leaks)
- [ ] **User acceptance test** (all features work as documented)

---

## SUMMARY

**Total Issues Found:** 20  
- 🔴 **Critical:** 3 (syntax errors, missing data transmission)
- 🟡 **High:** 6 (timeouts, race conditions, watchdogs)
- 🟠 **Medium:** 11 (validation, debounce, bounds checks)

**Estimated Fix Time:** 4-6 hours  
**Risk Level:** MEDIUM (critical issues prevent compilation/operation)  
**Recommendation:** **DO NOT DEPLOY** until critical issues are fixed.

---

**Report Generated:** April 9, 2026  
**Auditor:** Cline (Embedded Systems Specialist)  
**Status:** ⚠️ REQUIRES IMMEDIATE ACTION
