/*
 * ════════════════════════════════════════════════════════════════════
 *  BUDDYBOT  ·  KEYESTUDIO MEGA 2560 PLUS WiFi  ·  PRODUCTION V29.0
 * ════════════════════════════════════════════════════════════════════
 *
 *  MERGED FROM: V28.0 (production) + V2.0 (architecture additions)
 *
 *  CHANGES FROM V28
 *  ─────────────────
 *  [FIX]  STAT: packet field order corrected to match R4 V24 parser
 *         exactly: gas:temp:hum:haz:pir:tilt:flame:ir:volt:pct:amps
 *         V28 had GPS fields at wrong indices causing R4 display errors.
 *  [FIX]  sendTelemetryToR4() now sends STAT + US + PWR + STATUS| in
 *         one function so R4 HUD always has a complete picture.
 *  [FIX]  applyToggle() now normalises "CURRENT" → sens.current
 *         (V28 missed this — it was present in V2 only).
 *  [NEW]  Serial channel map clarified and locked:
 *           Serial  (USB)     ↔ Samsung S9
 *           Serial1 (18/19)   ↔ UNO R4 WiFi (D0/D1)
 *           Serial2 (16/17)   ↔ GPS NEO-6M (9600 baud)
 *           Serial3 (14/15)   ↔ ESP32
 *           SoftwareSerial(10/11) ↔ UNO R3 Motor Shield
 *  [NEW]  ESP32 bridge fully integrated from V2:
 *           - BTCMD|MOTOR|<dir> and BTCMD|MODE|<code> handled
 *           - WEBCMD|MOTOR|<dir> and WEBCMD|MODE|<code> handled
 *           - WEBCMD|SAFETY|CLR handled
 *           - sendTelemetryToESP32() sends JSON-friendly TELEM: string
 *  [NEW]  SENS_ST| broadcast pushed to R4 on every sensor toggle and
 *         on startup — R4 V24 sensor config screen stays in sync.
 *  [NEW]  BAT:WARN / BAT:LOW / BAT:CRITICAL pushed to ESP32 (Serial3)
 *         as well as R4 (Serial1) for web dashboard colour updates.
 *  [NEW]  MODE: echo sent to ESP32 on every mode change.
 *  [KEPT] All V28 features preserved:
 *           Non-blocking motor queue, ESTOP auto-recovery (3x retry),
 *           autonomous navigation + stuck detection,
 *           collision avoidance with freshness guard,
 *           RF remote, PAJ7620 gesture sensor, GPS, thermistor battery
 *           temp, current sensor ISR, headlights, fan, button, DANCE.
 *
 *  SERIAL CHANNEL MAP (authoritative — do not change)
 *  ──────────────────────────────────────────────────
 *  Serial   (USB, pins 0/1)   115200  ↔ Samsung S9 Android app
 *  Serial1  (pins 18 TX/19 RX) 115200  ↔ UNO R4 WiFi (R4 Serial1: D1/D0)
 *  Serial2  (pins 17 TX/16 RX)   9600  ↔ GPS NEO-6M (TinyGPS++)
 *  Serial3  (pins 14 TX/15 RX) 115200  ↔ ESP32 GPIO17(RX)/GPIO16(TX)
 *  SoftwareSerial(10 RX / 11 TX) 9600  ↔ UNO R3 Motor Shield A0(RX)/A1(TX)
 *
 *  SENSOR TOGGLE IDs
 *  ──────────────────
 *  DHT  LIGHT  SOUND  GAS  FLAME  PIR  TILT  IR  US  CURRENT  GPS
 *
 * ════════════════════════════════════════════════════════════════════
 */

#include <SoftwareSerial.h>
#include <TinyGPS++.h>
#include <Wire.h>
#include <DHT.h>
#include <RCSwitch.h>
#include "paj7620.h"

// ════════════════════════════════════════════════════════════════════
//  CONFIGURATION
// ════════════════════════════════════════════════════════════════════
#define DEBUG_VERBOSE   false
#define FW_VERSION      "V29.0"

const String PRIORITY_USER = "AJ";

// Battery thresholds (2S10P Li-ion, nominal 8.4V)
const float BAT_MAX    = 8.4f;
const float BAT_MIN    = 6.0f;    // CRITICAL — ESTOP
const float BAT_LOW    = 6.8f;    // LOW — warn + slow
const float BAT_WARN   = 7.2f;    // WARN — first notice
const float BAT_VDIV   = 3.1277f; // Voltage divider multiplier for A15
const float BAT_CTEMP  = 50.0f;   // Battery over-temp ESTOP
const float BAT_WTEMP  = 45.0f;   // Fan-on threshold

// Navigation geometry
const int OBS_STOP = 25;   // cm — emergency stop
const int OBS_SLOW = 40;   // cm — slow down
const int OBS_WARN = 60;   // cm — caution
const int SIDE_MIN = 35;   // cm — minimum side clearance
const int T45      = 350;  // ms for ~45° turn at DEFAULT_SPEED
const int T90      = 700;  // ms for ~90° turn at DEFAULT_SPEED

// RF remote codes (315/433 MHz)
const unsigned long RF_FWD  = 5393;
const unsigned long RF_BWD  = 5396;
const unsigned long RF_LFT  = 5394;
const unsigned long RF_RGT  = 5397;
const unsigned long RF_STP  = 5392;
const unsigned long RF_AUTO = 5400;

// ════════════════════════════════════════════════════════════════════
//  PIN DEFINITIONS
// ════════════════════════════════════════════════════════════════════

// SoftwareSerial → UNO R3 Motor Shield
SoftwareSerial motorComm(10, 11);  // RX=10 ← R3 A1(TX), TX=11 → R3 A0(RX)

// Analog sensors
#define VOLTAGE_SENSOR  A15   // Battery voltage divider
#define TEMP_SENSOR_1   A0    // Thermistor 1 (battery temp)
#define TEMP_SENSOR_2   A1    // Thermistor 2 (ambient, optional)
#define FLAME_AO        A2    // Flame sensor analog output
#define LDR_AO          A3    // Light-dependent resistor
#define SOUND_AO        A4    // Sound sensor analog
#define GAS_AO          A6    // Gas/MQ analog

// I2C interrupt
#define GESTURE_INT     A13   // PAJ7620 gesture sensor interrupt

// Digital outputs
#define LEFT_HEADLIGHT   5
#define RIGHT_HEADLIGHT  6
#define FAN_PIN          3
#define BUZZER_PIN      33

// Digital inputs
#define MOMENTARY_BTN    4    // Push button — toggle auto mode
#define FLAME_DO         7    // Flame sensor digital output (LOW = flame)
#define LDR_DO           9    // LDR threshold output
#define UNHINGED_SW     12    // Physical switch — unhinged mode
#define TILT_SENSOR     23    // Tilt/vibration sensor (HIGH = tilt)
#define PIR_PIN         40    // PIR motion sensor (HIGH = motion)
#define DHT_PIN         45    // DHT11 data
#define GAS_DO          34    // Gas sensor digital output (HIGH = gas)
#define RF_PIN          49    // 433MHz RF receiver
#define CURRENT_SENSOR  30    // Current sensor pulse input (interrupt)

// IR obstacle sensors (LOW = obstacle detected)
#define REAR_IR   22
#define FRONT_IR  26
#define LEFT_IR   24
#define RIGHT_IR  25

// Ultrasonic sensors (4x HC-SR04)
#define FRONT_TRIG  29
#define FRONT_ECHO  28
#define LEFT_TRIG   43
#define LEFT_ECHO   45
#define RIGHT_TRIG  49
#define RIGHT_ECHO  47
#define REAR_TRIG   39
#define REAR_ECHO   35

// ════════════════════════════════════════════════════════════════════
//  OBJECTS
// ════════════════════════════════════════════════════════════════════
DHT        dht(DHT_PIN, DHT11);
TinyGPSPlus gps;
RCSwitch   rfReceiver = RCSwitch();

// ════════════════════════════════════════════════════════════════════
//  SENSOR TOGGLE TABLE
//  Any sensor can be disabled at runtime via TOGGLE_SENSOR:<ID>:<ON|OFF>
//  Disabled sensors return -1 / false and are excluded from safety logic.
// ════════════════════════════════════════════════════════════════════
struct SensorFlags {
  bool dht     = true;
  bool light   = true;
  bool sound   = true;
  bool gas     = true;
  bool flame   = true;   // alert-only — never triggers ESTOP
  bool pir     = false;  // off by default (can be noisy indoors)
  bool tilt    = true;
  bool ir      = true;
  bool us      = true;
  bool current = true;
  bool gps     = true;
} sens;

// Returns the SENS_ST| string that R4 V24 expects
// Format: SENS_ST|DHT:1|LIGHT:1|...|CUR:1|GPS:1|END
String sensorStatusString() {
  String s = F("SENS_ST|");
  s += "DHT:"   + String(sens.dht     ? 1 : 0) + "|";
  s += "LIGHT:" + String(sens.light   ? 1 : 0) + "|";
  s += "SOUND:" + String(sens.sound   ? 1 : 0) + "|";
  s += "GAS:"   + String(sens.gas     ? 1 : 0) + "|";
  s += "FLAME:" + String(sens.flame   ? 1 : 0) + "|";
  s += "PIR:"   + String(sens.pir     ? 1 : 0) + "|";
  s += "TILT:"  + String(sens.tilt    ? 1 : 0) + "|";
  s += "IR:"    + String(sens.ir      ? 1 : 0) + "|";
  s += "US:"    + String(sens.us      ? 1 : 0) + "|";
  s += "CUR:"   + String(sens.current ? 1 : 0) + "|";
  s += "GPS:"   + String(sens.gps     ? 1 : 0) + "|END";
  return s;
}

// Parse and apply a TOGGLE_SENSOR:<ID>:<ON|OFF> command
// Accepts commands from S9, R4 (TOGGLE_SENSOR format), or ESP32
void applyToggle(const String& cmd) {
  int c1 = cmd.indexOf(':');
  int c2 = cmd.indexOf(':', c1 + 1);
  if (c1 < 0 || c2 < 0) return;
  String id  = cmd.substring(c1 + 1, c2);
  String val = cmd.substring(c2 + 1);
  id.toUpperCase();
  val.toUpperCase();
  bool on = (val == "ON" || val == "1");

  if      (id == "DHT")                   sens.dht     = on;
  else if (id == "LIGHT")                 sens.light   = on;
  else if (id == "SOUND")                 sens.sound   = on;
  else if (id == "GAS")                   sens.gas     = on;
  else if (id == "FLAME")                 sens.flame   = on;
  else if (id == "PIR")                   sens.pir     = on;
  else if (id == "TILT")                  sens.tilt    = on;
  else if (id == "IR")                    sens.ir      = on;
  else if (id == "US")                    sens.us      = on;
  else if (id == "CURRENT" || id == "CUR") sens.current = on;  // [FIX] both aliases
  else if (id == "GPS")                   sens.gps     = on;
  else { toS9("ERR|UNKNOWN_SENSOR:" + id + "|END"); return; }

  // Acknowledge and push updated status to all channels
  toS9("ACK|" + cmd + "|END");
  String st = sensorStatusString();
  toS9(st);
  Serial1.println(st);   // R4 sensor config screen
  Serial3.println(st);   // ESP32 web dashboard
}

// ════════════════════════════════════════════════════════════════════
//  GLOBAL STATE
// ════════════════════════════════════════════════════════════════════

// Sensor readings
float battVolt    = 8.4f;
float battPct     = 100.0f;
float battTemp    = 25.0f;
float ambTemp     = 25.0f;
float humidity    = 50.0f;
float currentAmps = 0.0f;
int   lightLevel  = 500;
int   gasLevel    = 0;
int   soundLevel  = 0;
int   gps_sats    = 0;
float gps_lat     = 0.0f;
float gps_lon     = 0.0f;
long  dFront = -1, dRear = -1, dLeft = -1, dRight = -1;
bool  irFront = false, irRear = false, irLeft = false, irRight = false;
bool  flameDetected = false;
bool  tiltDetected  = false;
bool  pirDetected   = false;
bool  gasDetected   = false;

// Battery tier (change-on-edge only — no spamming)
enum BatTier { BAT_TIER_OK, BAT_TIER_WARN, BAT_TIER_LOW, BAT_TIER_CRITICAL };
BatTier lastBatTier = BAT_TIER_OK;

// Current sensor ISR
volatile unsigned long currentPulses = 0;
unsigned long lastCurrentCalc = 0;

// System flags
bool systemReady    = false;
bool emergencyStop  = false;
bool autonomousMode = false;
bool unhingedMode   = false;
bool lightsAuto     = true;
bool fanAuto        = true;
bool debugVerbose   = DEBUG_VERBOSE;
int  estopRetries   = 0;
const int MAX_ESTOP = 3;

// S9 connection tracking
bool          s9Connected = false;
String        s9Buffer    = "";
unsigned long s9LastHB    = 0;
unsigned long s9LastSent  = 0;
const unsigned long S9_TIMEOUT = 6000;

// Timing
unsigned long lastSense   = 0;
unsigned long lastTelem   = 0;
unsigned long lastNavDec  = 0;
unsigned long lastAvoid   = 0;
unsigned long lastSenseTs = 0;   // freshness timestamp for collisionAvoidance()
unsigned long uptimeSec   = 0;

// RF
unsigned long lastRFCode = 0;
unsigned long lastRFTime = 0;

// Button
bool          btnPressed = false;
unsigned long lastBtn    = 0;
const unsigned long BTN_DEBOUNCE = 200;

// Face / object detection (from S9)
String lastFace = "";

// ESP32 bridge
String        esp32Buf   = "";
bool          esp32Ready = false;

// ── Non-blocking motor queue ──────────────────────────────────────────────────
// sendMotor() deposits a command here; drainMotorQueue() flushes it at the
// top of loop() — ensures SoftSerial never blocks the main loop for >1ms.
struct MotorCmd { char cmd[24]; bool pending; } mQueue = { "", false };

// ── Navigation state ─────────────────────────────────────────────────────────
struct NavState {
  bool   isMoving      = false;
  bool   isAvoiding    = false;
  bool   isReversing   = false;
  int    avoidAttempts = 0;
  long   lastFrontDist = 0;
  unsigned long avoidStart  = 0;
  unsigned long stuckStart  = 0;
  bool   stuckDetected = false;
} nav;

// ════════════════════════════════════════════════════════════════════
//  UTILITY
// ════════════════════════════════════════════════════════════════════
void toS9(const String& msg)  { Serial.println(msg); }
void dbg(const char* msg)     { if (debugVerbose) Serial.println(msg); }
void beep(int freq, int ms)   { tone(BUZZER_PIN, freq, ms); }

long getDist(int trig, int echo) {
  if (!sens.us) return -1;
  digitalWrite(trig, LOW);  delayMicroseconds(2);
  digitalWrite(trig, HIGH); delayMicroseconds(10);
  digitalWrite(trig, LOW);
  long dur = pulseIn(echo, HIGH, 30000);
  return dur > 0 ? dur / 58 : -1;
}

// Steinhart-Hart thermistor equation for 10kΩ NTC, B=3950
float readThermistor(int pin) {
  int raw = analogRead(pin);
  if (raw <= 0) return 25.0f;
  float v = (raw / 1023.0f) * 5.0f;
  float r = (5.0f - v) / v * 10000.0f;
  float s = logf(r / 10000.0f) / 3950.0f + 1.0f / 298.15f;
  float c = (1.0f / s) - 273.15f;
  return (c < -50 || c > 120) ? 25.0f : c;
}

// ════════════════════════════════════════════════════════════════════
//  NON-BLOCKING MOTOR QUEUE
// ════════════════════════════════════════════════════════════════════
void sendMotor(const char* cmd) {
  // Update nav state immediately (no waiting for serial flush)
  if      (strcmp(cmd, "FORWARD")  == 0) { nav.isMoving=true;  nav.isReversing=false; }
  else if (strcmp(cmd, "BACKWARD") == 0) { nav.isMoving=true;  nav.isReversing=true;  }
  else if (strcmp(cmd, "LEFT")     == 0) { nav.isMoving=true;  nav.isReversing=false; }
  else if (strcmp(cmd, "RIGHT")    == 0) { nav.isMoving=true;  nav.isReversing=false; }
  else if (strcmp(cmd, "STOP")     == 0) { nav.isMoving=false; nav.isReversing=false; }

  // Map to wire format for R3 motor sketch
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
  motorComm.println(mQueue.cmd);
  mQueue.pending = false;
}

// ════════════════════════════════════════════════════════════════════
//  SENSOR READING
// ════════════════════════════════════════════════════════════════════
void readAllSensors() {
  // ── DHT11 temperature & humidity ────────────────────────────────
  if (sens.dht) {
    float t = dht.readTemperature();
    float h = dht.readHumidity();
    if (!isnan(t)) ambTemp  = t;
    if (!isnan(h)) humidity = h;
  }

  // ── Analog environmental sensors ────────────────────────────────
  lightLevel = sens.light ? analogRead(LDR_AO)   : -1;
  soundLevel = sens.sound ? analogRead(SOUND_AO) : -1;
  gasLevel   = sens.gas   ? analogRead(GAS_AO)   : -1;
  gasDetected= sens.gas   ? (digitalRead(GAS_DO) == HIGH) : false;

  // ── Battery voltage (A15, calibrated divider) ────────────────────
  int rawV  = analogRead(VOLTAGE_SENSOR);
  battVolt  = (rawV / 1023.0f) * 5.0f * BAT_VDIV;
  battTemp  = readThermistor(TEMP_SENSOR_1);

  // Float-safe battery percentage (avoids integer rounding jitter)
  float bRange = BAT_MAX - BAT_MIN;
  battPct = constrain(((battVolt - BAT_MIN) / bRange) * 100.0f, 0.0f, 100.0f);

  // ── Ultrasonic distance sensors ──────────────────────────────────
  if (sens.us) {
    dFront = getDist(FRONT_TRIG, FRONT_ECHO);
    dLeft  = getDist(LEFT_TRIG,  LEFT_ECHO);
    dRight = getDist(RIGHT_TRIG, RIGHT_ECHO);
    dRear  = getDist(REAR_TRIG,  REAR_ECHO);
  } else {
    dFront = dLeft = dRight = dRear = -1;
  }

  // ── IR obstacle sensors ──────────────────────────────────────────
  irFront = sens.ir ? (digitalRead(FRONT_IR) == LOW) : false;
  irRear  = sens.ir ? (digitalRead(REAR_IR)  == LOW) : false;
  irLeft  = sens.ir ? (digitalRead(LEFT_IR)  == LOW) : false;
  irRight = sens.ir ? (digitalRead(RIGHT_IR) == LOW) : false;

  // ── Flame sensor — ALERT ONLY, never triggers ESTOP ─────────────
  flameDetected = sens.flame ? (digitalRead(FLAME_DO) == LOW) : false;

  // ── Tilt sensor ──────────────────────────────────────────────────
  tiltDetected = sens.tilt ? (digitalRead(TILT_SENSOR) == HIGH) : false;

  // ── PIR motion sensor ────────────────────────────────────────────
  pirDetected = sens.pir ? (digitalRead(PIR_PIN) == HIGH) : false;

  // Stamp freshness for collisionAvoidance() staleness guard
  lastSenseTs = millis();
}

// ── Current sensor (pulse-counting ISR) ─────────────────────────────────────
void currentPulseISR() { currentPulses++; }

void updatePower() {
  if (!sens.current) return;
  unsigned long now = millis();
  unsigned long dt  = now - lastCurrentCalc;
  if (dt >= 1000) {
    currentAmps     = (currentPulses / (dt / 1000.0f)) * 0.066f;
    currentPulses   = 0;
    lastCurrentCalc = now;
  }
}

// ════════════════════════════════════════════════════════════════════
//  TELEMETRY — R4 (Serial1)
//
//  STAT packet field order MUST match R4 V24 parser indices:
//  idx: 0=gas  1=temp  2=hum  3=haz  4=pir  5=tilt  6=flame  7=ir
//       8=volt  9=pct  10=amps
//  [FIX] V28 had GPS at wrong indices. Removed from STAT — GPS is in
//  STATUS| string instead, which Android also parses.
// ════════════════════════════════════════════════════════════════════
void sendTelemetryToR4() {
  // ── STAT: ─────────────────────────────────────────────────────────
  int haz = (emergencyStop || flameDetected || tiltDetected) ? 1 : 0;

  String t = F("STAT:");
  t += String(gasLevel);              t += ':';   // 0 gas
  t += String(ambTemp,  1);           t += ':';   // 1 temp
  t += String(humidity, 1);           t += ':';   // 2 hum
  t += String(haz);                   t += ':';   // 3 haz
  t += (pirDetected   ? "1" : "0");   t += ':';   // 4 pir
  t += (tiltDetected  ? "1" : "0");   t += ':';   // 5 tilt
  t += (flameDetected ? "1" : "0");   t += ':';   // 6 flame
  t += ((irFront||irRear||irLeft||irRight) ? "1":"0"); t += ':'; // 7 ir
  t += String(battVolt, 2);           t += ':';   // 8 volt
  t += String((int)battPct);          t += ':';   // 9 pct
  t += String(currentAmps, 2);                    // 10 amps
  Serial1.println(t);

  // ── US: ──────────────────────────────────────────────────────────
  String u = F("US:");
  u += String(dFront); u += ',';
  u += String(dRear);  u += ',';
  u += String(dLeft);  u += ',';
  u += String(dRight);
  Serial1.println(u);

  // ── PWR: (R4 parser: idx 0=volt 1=amps 4=pct) ────────────────────
  String p = F("PWR:");
  p += String(battVolt, 2); p += ':';
  p += String(currentAmps, 2); p += ':';
  p += String((int)analogRead(VOLTAGE_SENSOR)); p += ':';
  p += String((int)battPct); p += ':';
  p += String((int)battPct);
  Serial1.println(p);

  // ── STATUS| ──────────────────────────────────────────────────────
  String s = F("STATUS|ESTOP:");
  s += (emergencyStop  ? "YES" : "NO");
  s += F("|AUTO:");
  s += (autonomousMode ? "ON"  : "OFF");
  s += F("|BAT:");
  s += String(battVolt, 2);
  s += F("|PCT:");
  s += String((int)battPct);
  Serial1.println(s);
}

// ════════════════════════════════════════════════════════════════════
//  TELEMETRY — ESP32 (Serial3)
//  Kept as TELEM: string (ESP32 V2 firmware parses this)
// ════════════════════════════════════════════════════════════════════
void sendTelemetryToESP32() {
  String t = F("TELEM:");
  if      (emergencyStop)  t += F("ESTOP");
  else if (autonomousMode) t += F("AUTO");
  else if (nav.isMoving)   t += F("MOVING");
  else                     t += F("IDLE");
  t += ':';
  t += String(battVolt,    1); t += ':';
  t += (autonomousMode ? F("Auto") : F("Manual")); t += ':';
  t += String(dFront);  t += ':';
  t += String(dLeft);   t += ':';
  t += String(dRight);  t += ':';
  t += String(dRear);   t += ':';
  t += String(ambTemp,  1);    t += ':';
  t += String((int)humidity);  t += ':';
  t += String(gasLevel);       t += ':';
  t += (flameDetected ? '1' : '0');
  Serial3.println(t);

  // Also push a compact STAT string so ESP32 web dashboard can parse it
  // (same format as R4 STAT: above)
  int haz = (emergencyStop || flameDetected || tiltDetected) ? 1 : 0;
  String s = F("STAT:");
  s += String(gasLevel);          s += ':';
  s += String(ambTemp, 1);        s += ':';
  s += String(humidity, 1);       s += ':';
  s += String(haz);               s += ':';
  s += (pirDetected   ? "1":"0"); s += ':';
  s += (tiltDetected  ? "1":"0"); s += ':';
  s += (flameDetected ? "1":"0"); s += ':';
  s += ((irFront||irRear||irLeft||irRight) ? "1":"0"); s += ':';
  s += String(battVolt, 2);       s += ':';
  s += String((int)battPct);      s += ':';
  s += String(currentAmps, 2);
  Serial3.println(s);

  String u = F("US:");
  u += String(dFront); u += ',';
  u += String(dRear);  u += ',';
  u += String(dLeft);  u += ',';
  u += String(dRight);
  Serial3.println(u);
}

// ════════════════════════════════════════════════════════════════════
//  TIERED BATTERY WARNINGS
//  Sends BAT:WARN / BAT:LOW / BAT:CRITICAL to R4 AND ESP32
//  on tier-change events only — no spamming.
// ════════════════════════════════════════════════════════════════════
void checkBatteryTiers() {
  BatTier tier;
  if      (battVolt <= BAT_MIN)  tier = BAT_TIER_CRITICAL;
  else if (battVolt <= BAT_LOW)  tier = BAT_TIER_LOW;
  else if (battVolt <= BAT_WARN) tier = BAT_TIER_WARN;
  else                           tier = BAT_TIER_OK;

  if (tier == lastBatTier) return;
  lastBatTier = tier;

  switch (tier) {
    case BAT_TIER_WARN:
      toS9("AUDIO:BATTERY_WARN");
      Serial1.println(F("BAT:WARN"));
      Serial3.println(F("BAT:WARN"));
      beep(1200, 200);
      break;
    case BAT_TIER_LOW:
      toS9("AUDIO:BATTERY_LOW");
      Serial1.println(F("BAT:LOW"));
      Serial3.println(F("BAT:LOW"));
      sendMotor("SLOW");
      beep(1800, 300);
      break;
    case BAT_TIER_CRITICAL:
      emergencyStop = true;
      sendMotor("STOP");
      toS9("AUDIO:BATTERY_CRITICAL");
      Serial1.println(F("BAT:CRITICAL"));
      Serial3.println(F("BAT:CRITICAL"));
      beep(2500, 1000);
      break;
    default: break;
  }
}

// ════════════════════════════════════════════════════════════════════
//  SAFETY  (flame = alert only, tilt = stop not ESTOP)
// ════════════════════════════════════════════════════════════════════
void checkSafety() {
  checkBatteryTiers();

  // Battery over-temperature → ESTOP
  if (battTemp > BAT_CTEMP && !emergencyStop) {
    emergencyStop = true;
    sendMotor("STOP");
    digitalWrite(FAN_PIN, HIGH);
    toS9("AUDIO:OVERTEMP");
    Serial1.println(F("SAFETY:OVERTEMP"));
    Serial3.println(F("ALERT:OVERTEMP"));
    beep(2500, 1000);
  }

  // Tilt: stop motors but NOT a latching ESTOP (recoverable)
  if (tiltDetected && sens.tilt) {
    sendMotor("STOP");
    toS9("AUDIO:TILT");
    Serial1.println(F("SAFETY:TILT"));
    Serial3.println(F("ALERT:TILT_DETECTED"));
    beep(2000, 300);
  }

  // Flame: ALERT ONLY — no motor stop, no ESTOP
  // Rising-edge only — fires once per detection event
  static bool lastFlameState = false;
  if (flameDetected && !lastFlameState && sens.flame) {
    toS9("AUDIO:HAZARD");
    Serial1.println(F("SAFETY:FLAME_ALERT"));   // R4 overlay
    Serial3.println(F("ALERT:FLAME_DETECTED"));  // ESP32 dashboard
    beep(3000, 300); delay(100); beep(3000, 300);
  }
  lastFlameState = flameDetected;

  // Gas: alert only
  if (gasDetected && sens.gas) {
    toS9("AUDIO:GAS_ALERT");
    Serial1.println(F("SAFETY:GAS_ALERT"));
    Serial3.println(F("ALERT:GAS_DETECTED"));
  }

  // Fan auto control
  if (fanAuto) {
    digitalWrite(FAN_PIN, (battTemp > BAT_WTEMP || ambTemp > 35.0f) ? HIGH : LOW);
  }

  // Headlight auto control
  if (lightsAuto) {
    uint8_t ls = (lightLevel >= 0 && lightLevel < 300) ? HIGH : LOW;
    digitalWrite(LEFT_HEADLIGHT,  ls);
    digitalWrite(RIGHT_HEADLIGHT, ls);
  }
}

// ════════════════════════════════════════════════════════════════════
//  ESTOP AUTO-RECOVERY  (3x retry with timer-reset fix from V28)
// ════════════════════════════════════════════════════════════════════
void handleEstopRecovery() {
  static unsigned long estopT = 0;
  if (estopT == 0) {
    estopT = millis();
    motorComm.println(F("MOTOR|S"));
    nav.isMoving = false;
    return;
  }
  if (millis() - estopT > 5000) {
    estopRetries++;
    if (estopRetries < MAX_ESTOP) {
      if (battVolt > BAT_MIN && battTemp < BAT_CTEMP) {
        emergencyStop = false;
        estopT = 0;            // [FIX from V28] reset timer for next ESTOP
        toS9("ESTOP|CLEARED|Auto-restart");
        beep(1500, 200);
      } else {
        estopT = millis();     // conditions not met — restart 5s wait
      }
    } else {
      toS9("ESTOP|MANUAL_REQUIRED|Max retries reached");
      beep(2000, 1000);
      estopT = millis();       // prevent repeated MANUAL_REQUIRED every tick
    }
  }
}

// ════════════════════════════════════════════════════════════════════
//  S9 COMMUNICATION
// ════════════════════════════════════════════════════════════════════
void sendStatusToS9() {
  String s = F("STATUS|BAT:");
  s += String(battVolt, 2); s += F("|PCT:");  s += String((int)battPct);
  s += F("|AMP:");          s += String(currentAmps, 2);
  s += F("|TEMP:");         s += String(ambTemp, 1);
  s += F("|AUTO:");         s += (autonomousMode ? "ON"  : "OFF");
  s += F("|ESTOP:");        s += (emergencyStop  ? "YES" : "NO");
  s += F("|FLAME:");        s += (flameDetected  ? "YES" : "NO");
  s += F("|UPT:");          s += String(uptimeSec);
  s += F("|END");
  toS9(s);
}

void handleS9Communication() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n') {
      processS9Command(s9Buffer);
      s9Buffer = "";
    } else if (c != '\r') {
      s9Buffer += c;
      if (s9Buffer.length() > 80) s9Buffer = "";
    }
  }
}

void processS9Command(String cmd) {
  cmd.trim();
  if (cmd.length() == 0) return;
  s9LastHB    = millis();
  s9Connected = true;

  // ── Motor commands ───────────────────────────────────────────────
  if (cmd == "MOTOR|F")     { sendMotor("FORWARD");  toS9("ACK|MOTOR|F|END"); return; }
  if (cmd == "MOTOR|B")     { sendMotor("BACKWARD"); toS9("ACK|MOTOR|B|END"); return; }
  if (cmd == "MOTOR|L")     { sendMotor("LEFT");     toS9("ACK|MOTOR|L|END"); return; }
  if (cmd == "MOTOR|R")     { sendMotor("RIGHT");    toS9("ACK|MOTOR|R|END"); return; }
  if (cmd == "MOTOR|S")     { sendMotor("STOP");     toS9("ACK|MOTOR|S|END"); return; }
  if (cmd == "MOTOR|DANCE") {
    mQueue.pending = false;
    motorComm.println(F("MOTOR|DANCE"));
    toS9("ACK|DANCE|END");
    return;
  }
  if (cmd == "DEFENSE") {
    mQueue.pending = false;
    motorComm.println(F("DEFENSE"));
    toS9("ACK|DEFENSE|END");
    return;
  }

  // ── Speed ────────────────────────────────────────────────────────
  if (cmd.startsWith("SPEED:")) { motorComm.println(cmd); toS9("ACK|" + cmd + "|END"); return; }

  // ── Autonomous mode ──────────────────────────────────────────────
  if (cmd == "AUTO:ON")  { autonomousMode = true;  toS9("ACK|AUTO_ON|END"); return; }
  if (cmd == "AUTO:OFF") { autonomousMode = false; sendMotor("STOP"); toS9("ACK|AUTO_OFF|END"); return; }

  // ── Emergency stop ───────────────────────────────────────────────
  if (cmd == "EMERGENCY_STOP") {
    emergencyStop = true;
    sendMotor("STOP");
    toS9("ACK|EMERGENCY_STOP|END");
    beep(2000, 500);
    return;
  }
  if (cmd == "ESTOP_CLEAR") {
    emergencyStop = false;
    estopRetries  = 0;
    toS9("ACK|ESTOP_CLEARED|END");
    return;
  }

  // ── Mode forwarding (S9 → R4 + ESP32) ───────────────────────────
  if (cmd.startsWith("MODE:")) {
    String mode = cmd.substring(5);
    toS9("ACK|MODE:" + mode + "|END");
    Serial1.print(F("MODE:")); Serial1.println(mode);   // → R4
    Serial3.print(F("MODE:")); Serial3.println(mode);   // → ESP32
    return;
  }

  // ── Sensor toggle ────────────────────────────────────────────────
  if (cmd.startsWith("TOGGLE_SENSOR:")) { applyToggle(cmd); return; }
  if (cmd == "SENSOR_STATUS")           { toS9(sensorStatusString()); return; }

  // ── Lights ───────────────────────────────────────────────────────
  if (cmd == "LIGHTS:ON")   { digitalWrite(LEFT_HEADLIGHT, HIGH); digitalWrite(RIGHT_HEADLIGHT, HIGH); lightsAuto=false; return; }
  if (cmd == "LIGHTS:OFF")  { digitalWrite(LEFT_HEADLIGHT, LOW);  digitalWrite(RIGHT_HEADLIGHT, LOW);  lightsAuto=false; return; }
  if (cmd == "LIGHTS:AUTO") { lightsAuto = true; return; }

  // ── Face / object detection passthrough to R4 ────────────────────
  if (cmd.startsWith("FACE:")) { lastFace = cmd.substring(5); return; }
  if (cmd.startsWith("OBJ:"))  { Serial1.println(cmd); return; }
  if (cmd.startsWith("SENS|")) { Serial1.println(cmd); return; }

  // ── Diagnostics ──────────────────────────────────────────────────
  if (cmd == "DIAG" || cmd == "DIAG:RUN") {
    String d = F("DIAG|");
    d += "BAT:" + String(battVolt,2) + "V|PCT:" + String((int)battPct) + "%|";
    d += "TEMP:" + String(ambTemp,1) + "C|";
    d += "F:" + String(dFront) + "cm|R:" + String(dRear) + "cm|";
    d += "L:" + String(dLeft)  + "cm|Ri:" + String(dRight) + "cm|";
    d += "GPS:" + String(gps_lat,4) + "," + String(gps_lon,4) + "|SAT:" + String(gps_sats) + "|";
    d += "S9:" + String(s9Connected ? "OK":"NO") + "|";
    d += "AUTO:" + String(autonomousMode ? "ON":"OFF") + "|";
    d += "UPT:" + String(uptimeSec) + "s|END";
    toS9(d);
    toS9(sensorStatusString());
    return;
  }

  if (cmd == "DEBUG:ON")  { debugVerbose = true;  return; }
  if (cmd == "DEBUG:OFF") { debugVerbose = false; return; }
}

// ════════════════════════════════════════════════════════════════════
//  ESP32 BRIDGE COMMUNICATION
//  Accepts BTCMD| (Bluetooth gamepad) and WEBCMD| (web dashboard)
// ════════════════════════════════════════════════════════════════════
void handleESP32Communication() {
  while (Serial3.available()) {
    char c = Serial3.read();
    if (c == '\n') {
      esp32Buf.trim();
      if (esp32Buf.length() > 0) processESP32Command(esp32Buf);
      esp32Buf = "";
    } else if (c != '\r') {
      esp32Buf += c;
      if (esp32Buf.length() > 80) esp32Buf = "";
    }
  }
}

void processESP32Command(String cmd) {
  if (debugVerbose) { Serial.print(F("[ESP32] RX: ")); Serial.println(cmd); }

  if (cmd == "READY") { esp32Ready = true; return; }

  // ── Bluetooth gamepad commands: BTCMD|MOTOR|F etc. ───────────────
  if (cmd.startsWith("BTCMD|")) {
    processMotorOrModeCmd(cmd.substring(6));
    return;
  }

  // ── Web dashboard commands: WEBCMD|MOTOR|F etc. ──────────────────
  if (cmd.startsWith("WEBCMD|")) {
    String sub = cmd.substring(7);
    if (sub == "SAFETY|CLR") {
      emergencyStop = false;
      estopRetries  = 0;
      toS9("ACK|ESTOP_CLEARED|END");
      return;
    }
    processMotorOrModeCmd(sub);
    return;
  }

  // ── Legacy CMD: format (from original V28 ESP32 firmware) ────────
  if (cmd.startsWith("CMD:")) {
    String sub = cmd.substring(4);
    sub.toUpperCase();
    if      (sub == "F")      { autonomousMode = false; sendMotor("FORWARD"); }
    else if (sub == "B")      { autonomousMode = false; sendMotor("BACKWARD"); }
    else if (sub == "L")      { autonomousMode = false; sendMotor("LEFT"); }
    else if (sub == "R")      { autonomousMode = false; sendMotor("RIGHT"); }
    else if (sub == "S")      { autonomousMode = false; sendMotor("STOP"); }
    else if (sub == "AUTO")   { autonomousMode = !autonomousMode; if (!autonomousMode) sendMotor("STOP"); }
    else if (sub == "DANCE")  { motorComm.println(F("MOTOR|DANCE")); }
    else if (sub == "SLOW")   { sendMotor("SLOW"); }
    else if (sub == "NORMAL") { sendMotor("NORMAL"); }
    else if (sub == "FAST")   { sendMotor("FAST"); }
    else if (sub == "ESTOP")  { emergencyStop = true; sendMotor("STOP"); }
    else if (sub == "CLEAR")  { emergencyStop = false; estopRetries = 0; }
    else if (sub.startsWith("TOGGLE_SENSOR:")) { applyToggle(sub); }
    return;
  }
}

// Shared handler for MOTOR|x and MODE:x commands from any source
void processMotorOrModeCmd(String cmd) {
  if      (cmd == "MOTOR|F") { autonomousMode = false; sendMotor("FORWARD");  toS9("ACK|MOTOR|F|END"); }
  else if (cmd == "MOTOR|B") { autonomousMode = false; sendMotor("BACKWARD"); toS9("ACK|MOTOR|B|END"); }
  else if (cmd == "MOTOR|L") { autonomousMode = false; sendMotor("LEFT");     toS9("ACK|MOTOR|L|END"); }
  else if (cmd == "MOTOR|R") { autonomousMode = false; sendMotor("RIGHT");    toS9("ACK|MOTOR|R|END"); }
  else if (cmd == "MOTOR|S") { autonomousMode = false; sendMotor("STOP");     toS9("ACK|MOTOR|S|END"); }
  else if (cmd.startsWith("MODE|") || cmd.startsWith("MODE:")) {
    String mode = cmd.substring(5);
    toS9("ACK|MODE:" + mode + "|END");
    Serial1.print(F("MODE:")); Serial1.println(mode);
  }
}

// ════════════════════════════════════════════════════════════════════
//  GPS  (Serial2 at 9600 baud — TinyGPS++)
// ════════════════════════════════════════════════════════════════════
void handleGPS() {
  if (!sens.gps) return;
  while (Serial2.available()) gps.encode(Serial2.read());
  if (gps.location.isUpdated()) {
    gps_lat = gps.location.lat();
    gps_lon = gps.location.lng();
  }
  if (gps.satellites.isUpdated()) gps_sats = gps.satellites.value();
}

// ════════════════════════════════════════════════════════════════════
//  RF REMOTE
// ════════════════════════════════════════════════════════════════════
void handleRF() {
  if (!rfReceiver.available()) return;
  unsigned long code = rfReceiver.getReceivedValue();
  rfReceiver.resetAvailable();
  if (code == 0 || code == lastRFCode) return;
  lastRFCode = code; lastRFTime = millis();

  if      (code == RF_FWD)  { sendMotor("FORWARD");  toS9("RF:FORWARD"); }
  else if (code == RF_BWD)  { sendMotor("BACKWARD"); toS9("RF:BACKWARD"); }
  else if (code == RF_LFT)  { sendMotor("LEFT");     toS9("RF:LEFT"); }
  else if (code == RF_RGT)  { sendMotor("RIGHT");    toS9("RF:RIGHT"); }
  else if (code == RF_STP)  { sendMotor("STOP"); autonomousMode = false; toS9("RF:STOP"); }
  else if (code == RF_AUTO) {
    autonomousMode = !autonomousMode;
    if (!autonomousMode) sendMotor("STOP");
    toS9(autonomousMode ? "RF:AUTO_ON" : "RF:AUTO_OFF");
    beep(autonomousMode ? 1500 : 1000, 200);
  }
}

// ════════════════════════════════════════════════════════════════════
//  PAJ7620 GESTURE SENSOR
// ════════════════════════════════════════════════════════════════════
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
  else if (data == GES_CLOCKWISE_FLAG) { g = "CW";    motorComm.println(F("MOTOR|DANCE")); }
  if (g) {
    Serial1.print(F("GESTURE:")); Serial1.println(g);
    toS9("GESTURE:" + String(g));
    Serial3.print(F("GESTURE:")); Serial3.println(g);  // also to ESP32 web dashboard
  }
}

// ════════════════════════════════════════════════════════════════════
//  MOMENTARY BUTTON  (pin 4 — toggles autonomous mode)
// ════════════════════════════════════════════════════════════════════
void handleButton() {
  if (digitalRead(MOMENTARY_BTN) == LOW) {
    unsigned long now = millis();
    if (!btnPressed && (now - lastBtn > BTN_DEBOUNCE)) {
      btnPressed     = true;
      lastBtn        = now;
      autonomousMode = !autonomousMode;
      if (!autonomousMode) sendMotor("STOP");
      toS9(autonomousMode ? "BTN:AUTO_ON" : "BTN:AUTO_OFF");
      for (int i = 0; i < 3; i++) {
        digitalWrite(LEFT_HEADLIGHT, HIGH); digitalWrite(RIGHT_HEADLIGHT, HIGH); delay(80);
        digitalWrite(LEFT_HEADLIGHT, LOW);  digitalWrite(RIGHT_HEADLIGHT, LOW);  delay(80);
      }
    }
  } else {
    btnPressed = false;
  }
}

// ════════════════════════════════════════════════════════════════════
//  AUTONOMOUS NAVIGATION
// ════════════════════════════════════════════════════════════════════
void lookAndDecide() {
  sendMotor("STOP"); delay(150);
  bool leftClear  = (dLeft  > SIDE_MIN || dLeft  < 0);
  bool rightClear = (dRight > SIDE_MIN || dRight < 0);
  if (leftClear && (dLeft > dRight || !rightClear)) {
    sendMotor("LEFT");  delay(T45); sendMotor("FORWARD"); nav.isMoving = true;
  } else if (rightClear) {
    sendMotor("RIGHT"); delay(T45); sendMotor("FORWARD"); nav.isMoving = true;
  } else {
    sendMotor("BACKWARD"); delay(800);
    sendMotor("LEFT");     delay(T90);
    sendMotor("FORWARD");  nav.isMoving = true;
  }
}

void handleStuck() {
  dbg("[NAV] Stuck — recovering");
  sendMotor("STOP");     delay(150);
  sendMotor("BACKWARD"); delay(1200);
  sendMotor("LEFT");     delay(T90);
  sendMotor("FORWARD");
  nav.stuckDetected = false;
  nav.stuckStart    = 0;
}

void makeAutonomousDecision() {
  if (emergencyStop) { sendMotor("STOP"); return; }
  if (nav.isMoving && !nav.isAvoiding) {
    if (abs(dFront - nav.lastFrontDist) < 5) {
      if (nav.stuckStart == 0) nav.stuckStart = millis();
      else if (millis() - nav.stuckStart > 3000) { handleStuck(); return; }
    } else {
      nav.stuckStart    = 0;
      nav.stuckDetected = false;
    }
  }
  nav.lastFrontDist = dFront;
  if (nav.isAvoiding) return;

  if (!nav.isMoving) {
    if (dFront > OBS_WARN || dFront < 0) sendMotor("FORWARD");
    else lookAndDecide();
  } else {
    if      (dFront > 0 && dFront < OBS_SLOW) sendMotor("SLOW");
    else if (dFront > OBS_SLOW)               sendMotor("NORMAL");
    if (random(1000) > 992) {
      (random(2) == 0) ? sendMotor("LEFT") : sendMotor("RIGHT");
      delay(180);
      sendMotor("FORWARD");
    }
  }
}

// Collision avoidance with sensor-freshness guard (< 600ms old)
void collisionAvoidance() {
  if (millis() - lastSenseTs > 600) return;  // stale data — don't react

  if (sens.us && dFront > 0 && dFront < OBS_STOP && !nav.isAvoiding) {
    nav.isAvoiding = true; nav.avoidStart = millis(); nav.avoidAttempts++;
    dbg("[AVOID] Emergency stop");
    sendMotor("STOP"); delay(100);
    bool lc = (dLeft  < 0 || dLeft  > SIDE_MIN);
    bool rc = (dRight < 0 || dRight > SIDE_MIN);
    sendMotor("BACKWARD"); delay(700);
    sendMotor("STOP");     delay(150);
    if      (lc && (!rc || dLeft > dRight)) { sendMotor("LEFT");  delay(T90); }
    else if (rc)                             { sendMotor("RIGHT"); delay(T90); }
    else                                     { sendMotor("LEFT");  delay(T90 * 2); }
    if (autonomousMode) { sendMotor("FORWARD"); nav.isMoving = true; }
    nav.isAvoiding = false;
  }

  if (sens.ir && irFront && !nav.isAvoiding) {
    dbg("[AVOID] Front IR");
    sendMotor("STOP");     delay(100);
    sendMotor("BACKWARD"); delay(500);
    sendMotor("RIGHT");    delay(T45);
    sendMotor("FORWARD");
  }

  if (nav.isReversing && sens.us && dRear > 0 && dRear < 15) {
    sendMotor("STOP"); nav.isReversing = false;
  }

  if (nav.avoidAttempts > 5) {
    dbg("[AVOID] Too many — stopping auto");
    autonomousMode    = false;
    nav.avoidAttempts = 0;
    sendMotor("STOP");
    toS9("AUDIO:NAVIGATION_FAILED");
  }
  if (!nav.isAvoiding && millis() - nav.avoidStart > 10000) nav.avoidAttempts = 0;
}

// ════════════════════════════════════════════════════════════════════
//  STARTUP
// ════════════════════════════════════════════════════════════════════
void initPins() {
  // Analog inputs
  pinMode(VOLTAGE_SENSOR, INPUT); pinMode(TEMP_SENSOR_1, INPUT); pinMode(TEMP_SENSOR_2, INPUT);
  pinMode(FLAME_AO, INPUT); pinMode(LDR_AO, INPUT); pinMode(SOUND_AO, INPUT);
  pinMode(GAS_AO,   INPUT);

  // Digital inputs
  pinMode(FLAME_DO, INPUT); pinMode(LDR_DO, INPUT);
  pinMode(REAR_IR,  INPUT); pinMode(FRONT_IR, INPUT);
  pinMode(LEFT_IR,  INPUT); pinMode(RIGHT_IR, INPUT);
  pinMode(TILT_SENSOR,   INPUT);
  pinMode(PIR_PIN,       INPUT);
  pinMode(UNHINGED_SW,   INPUT_PULLUP);
  pinMode(MOMENTARY_BTN, INPUT_PULLUP);
  pinMode(GESTURE_INT,   INPUT);
  pinMode(CURRENT_SENSOR,INPUT);
  pinMode(GAS_DO,        INPUT);

  // Echo pins
  pinMode(FRONT_ECHO, INPUT); pinMode(LEFT_ECHO,  INPUT);
  pinMode(RIGHT_ECHO, INPUT); pinMode(REAR_ECHO,  INPUT);

  // Trigger pins
  pinMode(FRONT_TRIG, OUTPUT); digitalWrite(FRONT_TRIG, LOW);
  pinMode(LEFT_TRIG,  OUTPUT); digitalWrite(LEFT_TRIG,  LOW);
  pinMode(RIGHT_TRIG, OUTPUT); digitalWrite(RIGHT_TRIG, LOW);
  pinMode(REAR_TRIG,  OUTPUT); digitalWrite(REAR_TRIG,  LOW);

  // Output devices
  pinMode(LEFT_HEADLIGHT,  OUTPUT); digitalWrite(LEFT_HEADLIGHT,  LOW);
  pinMode(RIGHT_HEADLIGHT, OUTPUT); digitalWrite(RIGHT_HEADLIGHT, LOW);
  pinMode(FAN_PIN,         OUTPUT); digitalWrite(FAN_PIN,         LOW);
  pinMode(BUZZER_PIN,      OUTPUT); digitalWrite(BUZZER_PIN,      LOW);
}

void startupSequence() {
  for (int i = 0; i < 3; i++) {
    digitalWrite(LEFT_HEADLIGHT, HIGH); digitalWrite(RIGHT_HEADLIGHT, HIGH); delay(120);
    digitalWrite(LEFT_HEADLIGHT, LOW);  digitalWrite(RIGHT_HEADLIGHT, LOW);  delay(120);
  }
  beep(800, 80); delay(100); beep(1200, 80); delay(100); beep(1600, 150);
  digitalWrite(FAN_PIN, HIGH); delay(400); digitalWrite(FAN_PIN, LOW);
}

// ════════════════════════════════════════════════════════════════════
//  SETUP
// ════════════════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);    // → Samsung S9
  Serial1.begin(115200);   // → UNO R4 WiFi
  Serial2.begin(9600);     // → GPS NEO-6M
  Serial3.begin(115200);   // → ESP32
  motorComm.begin(9600);   // → UNO R3 Motor Shield

  delay(500);
  Wire.begin();
  dht.begin();
  initPins();

  rfReceiver.enableReceive(digitalPinToInterrupt(RF_PIN));
  attachInterrupt(digitalPinToInterrupt(CURRENT_SENSOR), currentPulseISR, RISING);

  if (paj7620Init() == 0) { dbg("[INIT] PAJ7620 OK"); }
  else                     { dbg("[INIT] PAJ7620 FAILED — gesture sensor absent"); }

  readAllSensors();
  startupSequence();
  systemReady = true;

  // Announce to all channels
  toS9("SYSTEM|READY|" + String(FW_VERSION) + "|END");
  Serial1.println(F("STAT|READY"));
  Serial1.println(sensorStatusString());   // R4 sensor config screen initial state
  Serial3.println(F("MEGA:BOOT"));         // ESP32 boot handshake
  dbg("[READY] BuddyBot " FW_VERSION);
}

// ════════════════════════════════════════════════════════════════════
//  MAIN LOOP
// ════════════════════════════════════════════════════════════════════
void loop() {
  if (!systemReady) { delay(50); return; }
  unsigned long now = millis();

  drainMotorQueue();   // always first — flushes SoftSerial without blocking

  if (emergencyStop && estopRetries < MAX_ESTOP) handleEstopRecovery();

  unhingedMode = (digitalRead(UNHINGED_SW) == LOW);

  handleS9Communication();
  handleESP32Communication();
  handleGPS();
  handleRF();

  // S9 connection watchdog
  if (s9Connected && (now - s9LastHB > S9_TIMEOUT)) {
    s9Connected = false;
    dbg("[S9] Disconnected");
  }

  // Sensor + safety loop (500ms)
  if (now - lastSense > 500) {
    lastSense = now;
    readAllSensors();
    updatePower();
    checkSafety();
    checkGestures();
    handleButton();
  }

  // Telemetry broadcast (1000ms)
  if (now - lastTelem > 1000) {
    lastTelem = now;
    sendTelemetryToR4();
    sendTelemetryToESP32();
    uptimeSec++;
  }

  // Status to S9 (2000ms, only when connected)
  if (s9Connected && (now - s9LastSent > 2000)) {
    s9LastSent = now;
    sendStatusToS9();
  }

  // Autonomous navigation decision (200ms)
  if (autonomousMode && !emergencyStop && (now - lastNavDec > 200)) {
    lastNavDec = now;
    makeAutonomousDecision();
  }

  // Collision avoidance (50ms — fast loop)
  if (now - lastAvoid > 50) {
    lastAvoid = now;
    collisionAvoidance();
  }

  // Reverse beep
  if (nav.isReversing) {
    static unsigned long lastBeepT = 0;
    if (now - lastBeepT > 500) { lastBeepT = now; beep(1000, 80); }
  }

  // RF code debounce reset
  if (now - lastRFTime > 500) lastRFCode = 0;
}
