/*
 * ════════════════════════════════════════════════════════════════════
 *  BUDDYBOT  ·  KEYESTUDIO MEGA 2560 PLUS WiFi  ·  PRODUCTION V31.0
 * ════════════════════════════════════════════════════════════════════
 *  
 *  DO NOT CHANGE PIN DEFINITIONS WITHOUT EXPLICIT PERMISSION!!!
 *  
 *  CHANGES FROM V30
 *  ─────────────────
 *  [REMOVED] Boost converter voltage sensor (BOOST_VOLT_SENSOR / A9,
 *            BOOST_VDIV constant, boostVolt global) — battery is now
 *            measured directly via A14 only.
 *  [REMOVED] Flame sensor — FLAME_AO (A3), FLAME_DO (7), sens.flame,
 *            flameDetected global, all flame safety/beep logic, and
 *            flame field from STAT telemetry. Pins A3 and 7 are free.
 *  [REMOVED] Headlights — LEFT_HEADLIGHT (8), RIGHT_HEADLIGHT (9),
 *            lightsAuto flag, LIGHTS:ON/OFF/AUTO commands, auto-control
 *            block, startup flash, and handleButton() blink all removed.
 *            Pins 8 and 9 are free.
 *  [NEW]     THREE FANS replacing single FAN_PIN:
 *              FAN_BODY_PIN     34  body extract  (battery/ambient temp)
 *              FAN_HEAD_BLOW_PIN 35  head blower   (head temp ≥ 35 °C)
 *              FAN_HEAD_EXT_PIN  37  head extract   (head temp ≥ 35 °C)
 *            updateFans() manages all three independently each 500 ms.
 *            Commands (S9/ESP32): FAN_HEAD:ON/OFF/AUTO
 *                                 FAN_BODY:ON/OFF/AUTO
 *                                 FAN_ALL:ON   FAN_ALL:OFF
 *  [NEW]     HEAD TEMPERATURE SENSOR placeholder on A3 (freed from flame).
 *            readHeadTemp() currently returns 25.0 °C stub — replace
 *            body with readThermistor(HEAD_TEMP_SENSOR) once physical
 *            sensor is installed.
 *  [NEW]     UV LIGHT STRIP on pin 40.
 *            Default: OFF.  PIR safety interlock — UV will NOT activate
 *            while PIR detects presence, regardless of command.
 *            Commands: UV:ON / UV:OFF / UV:AUTO
 *            Auto mode: on when dark (LDR < 300) AND PIR clear.
 *  [NOTE]  ⚠ STAT: field indices changed — flame (was idx 6) removed.
 *            Pico STAT parser must be updated to new field order:
 *            0=gas 1=temp 2=hum 3=haz 4=pir 5=tilt 6=ir 7=volt 8=pct 9=amps
 *
 *  SERIAL CHANNEL MAP (authoritative — do not change)
 *  ──────────────────────────────────────────────────
 *  Serial   (USB, pins 0/1)      115200  ↔ Samsung S9 Android app
 *  Serial1  (pins 18 TX / 19 RX) 115200  ↔ Raspberry Pi Pico 2 (GP0/GP1)
 *  Serial2  (pins 16 RX / 17 TX)   9600  ↔ UNO R3 Motor Shield A0(RX)/A1(TX)
 *  Serial3  (pins 14 TX / 15 RX) 115200  ↔ ESP32 GPIO16(RX)/GPIO17(TX)
 *  SoftwareSerial(10 RX / 11 TX)   9600  ↔ GPS NEO-6M (TinyGPS++)
 *
 *  SENSOR TOGGLE IDs
 *  ──────────────────
 *  DHT  LIGHT  SOUND  GAS  PIR  TILT  IR  US  CURRENT  GPS
 *
 * ════════════════════════════════════════════════════════════════════
 */

#include <SoftwareSerial.h>
#include <TinyGPS++.h>
#include <Wire.h>
#include <DHT.h>
#include <RCSwitch.h>
#include "paj7620.h"
#include <util/atomic.h>

bool r3CommFail = false;

// ════════════════════════════════════════════════════════════════════
//  CONFIGURATION
// ════════════════════════════════════════════════════════════════════
#define DEBUG_VERBOSE   false
#define FW_VERSION      "V31.0"

const String PRIORITY_USER = "AJ";

// Battery thresholds (2S10P Li-ion, nominal 8.4 V)
const float BAT_MAX   = 8.4f;
const float BAT_MIN   = 6.0f;    // CRITICAL — ESTOP
const float BAT_LOW   = 6.6f;    // LOW — warn + slow
const float BAT_WARN  = 7.0f;    // WARN — first notice
const float BAT_VDIV  = 4.75f;   // Voltage-divider multiplier for A14
const float BAT_CTEMP = 50.0f;   // Battery over-temp ESTOP threshold
const float BAT_WTEMP = 45.0f;   // Body fan-on threshold (battery temp)

// Head fan threshold
const float HEAD_FAN_TEMP = 35.0f;  // °C — head blower + extract come on

// Navigation geometry
const int OBS_STOP = 40;   // cm — emergency stop
const int OBS_SLOW = 50;   // cm — slow down
const int OBS_WARN = 60;   // cm — caution
const int SIDE_MIN = 35;   // cm — minimum side clearance
const int T45      = 350;  // ms for ~45° turn at DEFAULT_SPEED
const int T90      = 700;  // ms for ~90° turn at DEFAULT_SPEED

// Gas sensor alert threshold (analog 0-1023)
// Tune this to your sensor — most MQ sensors read ~100-200 clean air, spike above 400 on gas
const int GAS_ALERT_THRESHOLD = 400;

// RF remote codes (315 / 433 MHz)
const unsigned long RF_FWD  = 5393;
const unsigned long RF_BWD  = 5396;
const unsigned long RF_LFT  = 5394;
const unsigned long RF_RGT  = 5397;
const unsigned long RF_STP  = 5392;
const unsigned long RF_AUTO = 5400;

// ════════════════════════════════════════════════════════════════════
//  PIN DEFINITIONS
// ════════════════════════════════════════════════════════════════════

// Hardware Serial2 → UNO R3 Motor Shield (promoted for reliability)
#define motorComm Serial2

// SoftwareSerial → GPS NEO-6M (low baud, RX-only — ideal for SW serial)
SoftwareSerial gpsSerial(10, 11);  // RX=10 ← GPS TX,  TX=11 (unused)

// ── SoftwareSerial guard wrappers ────────────────────────────────────────────
void motorCommPrintln(const __FlashStringHelper *msg) { if (!r3CommFail) motorComm.println(msg); }
void motorCommPrintln(const String &msg)              { if (!r3CommFail) motorComm.println(msg); }
void motorCommPrintln(const char *msg)                { if (!r3CommFail) motorComm.println(msg); }

// ── Analog sensors ───────────────────────────────────────────────────────────
#define VOLTAGE_SENSOR    A6   // Battery voltage divider (direct battery)
#define TEMP_SENSOR_1     A2    // Thermistor 1 — battery pack temperature
#define HEAD_TEMP_SENSOR  A13    // Head temperature sensor (placeholder — install soon)
#define LDR_AO            A4    // Light-dependent resistor
#define SOUND_AO          -1    // Sound sensor analog
#define GAS_AO            A14   // 
// ── I2C interrupt ────────────────────────────────────────────────────────────
#define GESTURE_INT       -1   // PAJ7620 gesture sensor interrupt

// ── Digital outputs ──────────────────────────────────────────────────────────
#define FAN_BODY_PIN      12    // Body extract fan     (battery / ambient temp)
#define FAN_HEAD_BLOW_PIN 9    // Head blower fan      (head temp ≥ HEAD_FAN_TEMP)
#define FAN_HEAD_EXT_PIN  8    // Head extract fan     (head temp ≥ HEAD_FAN_TEMP)
#define UV_LIGHT_PIN      4    // UV light strip       (manual / auto, PIR interlock) — moved from 40 (was RIGHT_ECHO conflict)
#define BUZZER_PIN        23   // Piezo buzzer — moved from 2 (INT4 now used by RF)

// ── Digital inputs ───────────────────────────────────────────────────────────
#define MOMENTARY_BTN     24   // Push button — toggle autonomous mode
#define LDR_DO            5     // LDR threshold output
#define UNHINGED_SW       A1    // Physical switch — unhinged mode
#define TILT_SENSOR       52    // Tilt / vibration sensor (HIGH = tilt)
#define PIR_PIN           -1    // PIR motion sensor — set real pin when fitted
#define DHT_PIN           33    // DHT11 data
#define GAS_DO            -1    // Gas sensor digital output (HIGH = gas)
#define RF_PIN            2     // 433 MHz RF receiver — INT4 (interrupt-capable on Mega)
#define CURRENT_SENSOR    3     // Current sensor pulse input — INT5 (interrupt-capable on Mega)
#define CHARGE_DETECT_PIN 27    // Barrel jack third wire: LOW=charging, HIGH=not charging (via 10kΩ/6.8kΩ divider)

// ── IR obstacle sensors (LOW = obstacle detected) ────────────────────────────
#define REAR_IR   25
#define FRONT_IR  A8
#define LEFT_IR   -1
#define RIGHT_IR  -1

// ── Ultrasonic sensors (4× HC-SR04) ─────────────────────────────────────────
#define FRONT_TRIG  49
#define FRONT_ECHO  4
#define LEFT_TRIG   28
#define LEFT_ECHO   29
#define RIGHT_TRIG  38
#define RIGHT_ECHO  40
#define REAR_TRIG   51
#define REAR_ECHO   50

// ════════════════════════════════════════════════════════════════════
//  OBJECTS
// ════════════════════════════════════════════════════════════════════
DHT         dht(DHT_PIN, DHT11);
TinyGPSPlus gps;
RCSwitch    rfReceiver = RCSwitch();

// ════════════════════════════════════════════════════════════════════
//  SENSOR TOGGLE TABLE
//  Any sensor can be disabled at runtime via TOGGLE_SENSOR:<ID>:<ON|OFF>
// ════════════════════════════════════════════════════════════════════
struct SensorFlags {
    bool dht     = true;
    bool light   = true;
    bool sound   = true;
    bool gas     = false; // off by default until sensor physically connected (floating pin causes false alerts)
    bool pir     = false;  // off by default (can be noisy indoors)
    bool tilt    = true;
    bool ir      = true;
    bool us      = true;
    bool current = true;
    bool gps     = true;
} sens;

// Returns the SENS_ST| string that Pico expects
// Format: SENS_ST|DHT:1|LIGHT:1|...|CUR:1|GPS:1|END
String sensorStatusString() {
  String s = F("SENS_ST|");
  s += "DHT:"   + String(sens.dht     ? 1 : 0) + "|";
  s += "LIGHT:" + String(sens.light   ? 1 : 0) + "|";
  s += "SOUND:" + String(sens.sound   ? 1 : 0) + "|";
  s += "GAS:"   + String(sens.gas     ? 1 : 0) + "|";
  s += "PIR:"   + String(sens.pir     ? 1 : 0) + "|";
  s += "TILT:"  + String(sens.tilt    ? 1 : 0) + "|";
  s += "IR:"    + String(sens.ir      ? 1 : 0) + "|";
  s += "US:"    + String(sens.us      ? 1 : 0) + "|";
  s += "CUR:"   + String(sens.current ? 1 : 0) + "|";
  s += "GPS:"   + String(sens.gps     ? 1 : 0) + "|END";
  return s;
}

// Parse and apply a TOGGLE_SENSOR:<ID>:<ON|OFF> command
void applyToggle(const String& cmd) {
  int c1 = cmd.indexOf(':');
  int c2 = cmd.indexOf(':', c1 + 1);
  if (c1 < 0 || c2 < 0) return;
  String id  = cmd.substring(c1 + 1, c2);
  String val = cmd.substring(c2 + 1);
  id.toUpperCase();
  val.toUpperCase();
  bool on = (val == "ON" || val == "1");

  if      (id == "DHT")                    sens.dht     = on;
  else if (id == "LIGHT")                  sens.light   = on;
  else if (id == "SOUND")                  sens.sound   = on;
  else if (id == "GAS")                    sens.gas     = on;
  else if (id == "PIR")                    sens.pir     = on;
  else if (id == "TILT")                   sens.tilt    = on;
  else if (id == "IR")                     sens.ir      = on;
  else if (id == "US")                     sens.us      = on;
  else if (id == "CURRENT" || id == "CUR") sens.current = on;
  else if (id == "GPS")                    sens.gps     = on;
  else { toS9("ERR|UNKNOWN_SENSOR:" + id + "|END"); return; }

  toS9("ACK|" + cmd + "|END");
  String st = sensorStatusString();
  toS9(st);
  Serial1.println(st);
  Serial3.println(st);
}

// ════════════════════════════════════════════════════════════════════
//  GLOBAL STATE
// ════════════════════════════════════════════════════════════════════
bool lc = false;   // left clear
bool rc = false;   // right clear

// ── Sensor readings ──────────────────────────────────────────────────────────
float battVolt    = 8.4f;
float battPct     = 100.0f;
float battTemp    = 25.0f;
float ambTemp     = 25.0f;
float headTemp    = 25.0f;   // Head enclosure temperature (from HEAD_TEMP_SENSOR)
float humidity    = 50.0f;
float currentAmps = 0.0f;
int   lightLevel  = 500;
int   gasLevel    = 0;   // raw analog 0-1023 from GAS_AO
int   soundLevel  = 0;
int   gps_sats    = 0;
float gps_lat     = 0.0f;
float gps_lon     = 0.0f;
long  dFront = -1, dRear = -1, dLeft = -1, dRight = -1;
bool  irFront = false, irRear = false, irLeft = false, irRight = false;
bool  tiltDetected  = false;
bool  pirDetected   = false;

// ── Battery tier (edge-triggered — no spamming) ──────────────────────────────
enum BatTier { BAT_TIER_OK, BAT_TIER_WARN, BAT_TIER_LOW, BAT_TIER_CRITICAL };
BatTier lastBatTier = BAT_TIER_OK;

// ── Current sensor ISR ───────────────────────────────────────────────────────
volatile unsigned long currentPulses = 0;
unsigned long lastCurrentCalc = 0;

// ── Fan control ──────────────────────────────────────────────────────────────
bool fanBodyAuto = true;   // body extract fan — auto mode
bool fanHeadAuto = true;   // head blow + extract — auto mode
bool fanBodyOn   = false;  // manual override state
bool fanHeadOn   = false;  // manual override state

// ── UV light strip ───────────────────────────────────────────────────────────
bool uvManualOn = false;   // user commanded on
bool uvAuto     = false;   // auto mode (dark + PIR clear)
bool uvActive   = false;   // actual output state

// ── System flags ─────────────────────────────────────────────────────────────
bool systemReady    = false;
bool emergencyStop  = false;
bool autonomousMode = false;
bool unhingedMode   = false;
bool fanAuto        = true;   // legacy alias (body fan auto) — kept for compatibility
bool debugVerbose   = DEBUG_VERBOSE;
int  estopRetries   = 0;
const int MAX_ESTOP = 3;
unsigned long estopT = 0;

// ── Pico link tracking ─────────────────────────────────────────────────────────
unsigned long picoLastPingMs = 0;
uint8_t       picoPingSeq    = 0;
bool          picoLinked     = false;

// ── S9 connection tracking ───────────────────────────────────────────────────
bool          s9Connected = false;
String        s9Buffer    = "";
unsigned long s9LastHB    = 0;
unsigned long s9LastSent  = 0;
const unsigned long S9_TIMEOUT = 6000;

// ── Timing ───────────────────────────────────────────────────────────────────
unsigned long lastSense      = 0;
unsigned long lastTelem      = 0;
unsigned long lastNavDec     = 0;
unsigned long lastAvoid      = 0;
unsigned long lastSenseTs    = 0;
unsigned long lastEsp32Check = 0;
unsigned long bootStartTime  = 0;
const unsigned long BOOT_LOCK_TIME = 5000;
unsigned long uptimeSec      = 0;

// ── RF ───────────────────────────────────────────────────────────────────────
unsigned long lastRFCode = 0;
unsigned long lastRFTime = 0;

// ── Button ───────────────────────────────────────────────────────────────────
bool          btnPressed = false;
unsigned long lastBtn    = 0;
const unsigned long BTN_DEBOUNCE = 200;

// ── Face / object detection (from S9) ────────────────────────────────────────
String lastFace = "";

// ── ESP32 bridge ─────────────────────────────────────────────────────────────
String esp32Buf   = "";
bool   esp32Ready = false;

// ── Manual charge detection ───────────────────────────────────────────────────
bool isCharging  = false;   // true when barrel jack charger connected
bool wasCharging = false;   // edge-detection flag

// ── Pico / R3 buffers ──────────────────────────────────────────────────────────
String picoBuf = "";
String r3Buf = "";

// ── Non-blocking motor queue ─────────────────────────────────────────────────
struct MotorCmd { char cmd[24]; bool pending; } mQueue = { "", false };

// ── Navigation state ─────────────────────────────────────────────────────────
struct NavState {
    bool   isMoving      = false;
    bool   isAvoiding    = false;
    bool   isReversing   = false;
    int    avoidAttempts = 0;
    long   lastFrontDist = 0;
    unsigned long avoidStart   = 0;
    unsigned long lastAvoidEnd = 0;
    unsigned long stuckStart   = 0;
    bool   stuckDetected = false;
} nav;

enum LookState   { LOOK_IDLE,   LOOK_STOP,  LOOK_LEFT, LOOK_RIGHT, LOOK_BACK_LEFT, LOOK_FORWARD };
enum StuckState  { STUCK_IDLE,  STUCK_STOP, STUCK_BACK, STUCK_LEFT, STUCK_FORWARD };
enum AvoidState  { AVOID_IDLE,  AVOID_STOP, AVOID_BACK, AVOID_STOP2, AVOID_TURN, AVOID_FORWARD };
enum RandomState { RANDOM_IDLE, RANDOM_TURN, RANDOM_FORWARD };

LookState   lookState   = LOOK_IDLE;
StuckState  stuckState  = STUCK_IDLE;
AvoidState  avoidState  = AVOID_IDLE;
RandomState randomState = RANDOM_IDLE;
unsigned long navTimer  = 0;

// ════════════════════════════════════════════════════════════════════
//  UTILITY
// ════════════════════════════════════════════════════════════════════
void toS9(const String& msg) {
  if (debugVerbose) { Serial.print(F("[SEND] ")); Serial.println(msg); }
  else              { Serial.println(msg); }
}
void dbg(const char* msg)   { if (debugVerbose) Serial.println(msg); }
void beep(int freq, int ms) { tone(BUZZER_PIN, freq, ms); }

uint8_t calcCRC(const String& s) {
  uint8_t c = 0;
  for (uint16_t i = 0; i < s.length(); i++) c ^= (uint8_t)s[i];
  return c;
}

void toPico(const String& msg) {
  char hex[3];
  sprintf(hex, "%02X", calcCRC(msg));
  Serial1.print(msg);
  Serial1.print(F("|CRC:"));
  Serial1.println(hex);
}

// HC-SR04 distance — works for any digital or analogue pin used as digital I/O
long getDist(int trig, int echo) {
  if (!sens.us) return -1;
  digitalWrite(trig, LOW);  delayMicroseconds(2);
  digitalWrite(trig, HIGH); delayMicroseconds(10);
  digitalWrite(trig, LOW);
  long dur = pulseIn(echo, HIGH, 10000);
  return dur > 0 ? dur / 58 : -1;
}

// Steinhart-Hart thermistor (10 kΩ NTC, B = 3950)
float readThermistor(int pin) {
  int raw = analogRead(pin);
  if (raw <= 0) return 25.0f;
  float v = (raw / 1023.0f) * 5.0f;
  float r = (5.0f - v) / v * 10000.0f;
  float s = logf(r / 10000.0f) / 3950.0f + 1.0f / 298.15f;
  float c = (1.0f / s) - 273.15f;
  return (c < -50 || c > 120) ? 25.0f : c;
}

// ── Head temperature — STUB until sensor is physically installed ─────────────
// TODO: once HEAD_TEMP_SENSOR (A3) wired up, replace return line with:
//       return readThermistor(HEAD_TEMP_SENSOR);
float readHeadTemp() {
  return 25.0f;   // placeholder — sensor not yet installed
}

// ════════════════════════════════════════════════════════════════════
//  MOTOR QUEUE
// ════════════════════════════════════════════════════════════════════
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

// ════════════════════════════════════════════════════════════════════
//  SENSOR READING
// ════════════════════════════════════════════════════════════════════
void readAllSensors() {
  // ── DHT11 temperature & humidity ────────────────────────────────────────────
  if (sens.dht) {
    float t = dht.readTemperature();
    float h = dht.readHumidity();
    if (!isnan(t)) ambTemp  = t;
    if (!isnan(h)) humidity = h;
    lc = (dLeft  > SIDE_MIN && dLeft  != -1);
    rc = (dRight > SIDE_MIN && dRight != -1);
  }

  // ── Analog environmental sensors ────────────────────────────────────────────
  lightLevel = sens.light ? analogRead(LDR_AO)   : -1;
  soundLevel = (SOUND_AO >= 0 && sens.sound) ? analogRead(SOUND_AO) : -1;
  gasLevel   = (GAS_AO   >= 0 && sens.gas)   ? analogRead(GAS_AO)   :  0;

  // ── Battery voltage (A14, direct divider) ────────────────────────────────────
  int rawV = analogRead(VOLTAGE_SENSOR);
  battVolt  = (rawV / 1023.0f) * 5.0f * BAT_VDIV;
  battTemp  = readThermistor(TEMP_SENSOR_1);

  // Float-safe battery percentage
  float bRange = BAT_MAX - BAT_MIN;
  battPct = constrain(((battVolt - BAT_MIN) / bRange) * 100.0f, 0.0f, 100.0f);

  // ── Head temperature ─────────────────────────────────────────────────────────
  headTemp = readHeadTemp();

  // ── Ultrasonic sensors ───────────────────────────────────────────────────────
  if (sens.us) {
    dFront = getDist(FRONT_TRIG, FRONT_ECHO);
    dLeft  = getDist(LEFT_TRIG,  LEFT_ECHO);
    dRight = getDist(RIGHT_TRIG, RIGHT_ECHO);
    dRear  = getDist(REAR_TRIG,  REAR_ECHO);
  } else {
    dFront = dLeft = dRight = dRear = -1;
  }

  // ── IR obstacle sensors ──────────────────────────────────────────────────────
  irFront = sens.ir ? (digitalRead(FRONT_IR) == LOW) : false;
  irRear  = sens.ir ? (digitalRead(REAR_IR)  == LOW) : false;
  irLeft  = (LEFT_IR  >= 0 && sens.ir) ? (digitalRead(LEFT_IR)  == LOW) : false;
  irRight = (RIGHT_IR >= 0 && sens.ir) ? (digitalRead(RIGHT_IR) == LOW) : false;

  // ── Tilt sensor ──────────────────────────────────────────────────────────────
  tiltDetected = sens.tilt ? (digitalRead(TILT_SENSOR) == HIGH) : false;

  // ── PIR motion sensor ────────────────────────────────────────────────────────
  pirDetected = (PIR_PIN >= 0 && sens.pir) ? (digitalRead(PIR_PIN) == HIGH) : false;

  lastSenseTs = millis();
}

// ── Current sensor (pulse-counting ISR) ─────────────────────────────────────
void currentPulseISR() { currentPulses++; }

void updatePower() {
  if (!sens.current) return;
  unsigned long now = millis();
  unsigned long dt  = now - lastCurrentCalc;
  if (dt >= 1000) {
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
      unsigned long pulses = currentPulses;
      currentPulses = 0;
      currentAmps = (pulses / (dt / 1000.0f)) * 0.066f;
    }
    lastCurrentCalc = now;
  }
}

// ════════════════════════════════════════════════════════════════════
//  FAN CONTROL  (called every 500 ms from sense loop)
//
//  Body extract fan  (34) — on when battery temp > BAT_WTEMP
//                           OR ambient temp > 35 °C
//                           OR battery temp > BAT_CTEMP (forced, always)
//  Head blower fan   (35) — on when head temp ≥ HEAD_FAN_TEMP
//  Head extract fan  (37) — on when head temp ≥ HEAD_FAN_TEMP
//
//  Auto modes can be overridden by FAN_HEAD / FAN_BODY commands.
//  Battery critical overtemp always forces body fan on regardless.
// ════════════════════════════════════════════════════════════════════
void updateFans() {
  // ── Head fans ────────────────────────────────────────────────────────────────
  bool headHot   = (headTemp >= HEAD_FAN_TEMP);
  bool headState = fanHeadAuto ? headHot : fanHeadOn;
  digitalWrite(FAN_HEAD_BLOW_PIN, headState ? HIGH : LOW);
  digitalWrite(FAN_HEAD_EXT_PIN,  headState ? HIGH : LOW);

  // ── Body fan ─────────────────────────────────────────────────────────────────
  bool bodyHot   = (battTemp > BAT_WTEMP || ambTemp > 35.0f);
  bool bodyState = fanBodyAuto ? bodyHot : fanBodyOn;
  if (battTemp > BAT_CTEMP) bodyState = true;   // overtemp: always force on
  digitalWrite(FAN_BODY_PIN, bodyState ? HIGH : LOW);
}

// ════════════════════════════════════════════════════════════════════
//  UV LIGHT STRIP CONTROL  (called every 500 ms from sense loop)
//
//  PIR safety interlock: UV will NEVER activate while pirDetected is
//  true, regardless of manual command or auto mode.
//
//  Auto mode: on when dark (lightLevel < 300) AND PIR clear.
//  Manual:    on when UV:ON received AND PIR clear.
// ════════════════════════════════════════════════════════════════════
void updateUV() {
  bool pirSafe = !(PIR_PIN >= 0 && pirDetected);   // safe when PIR not triggered

  bool newState = false;
  if (uvAuto) {
    newState = (lightLevel >= 0 && lightLevel < 300 && pirSafe);
  } else {
    newState = (uvManualOn && pirSafe);
  }

  if (newState != uvActive) {
    uvActive = newState;
    digitalWrite(UV_LIGHT_PIN, uvActive ? HIGH : LOW);
    if (uvActive) {
      toS9("UV:ACTIVE");
      Serial1.println(F("UV:ACTIVE"));
      Serial3.println(F("UV:ACTIVE"));
    } else {
      String reason = pirSafe ? "OFF" : "BLOCKED_PIR";
      toS9("UV:" + reason);
      Serial1.println("UV:" + reason);
      Serial3.println("UV:" + reason);
    }
  }
}

// ════════════════════════════════════════════════════════════════════
//  TELEMETRY — Pico  (Serial1)
//
//  STAT packet field order (updated V31 — flame removed):
//  idx: 0=gas  1=temp  2=hum  3=haz  4=pir  5=tilt  6=ir  7=volt  8=pct  9=amps
//  ⚠ Pico STAT parser must be updated to match this new field order.
// ════════════════════════════════════════════════════════════════════
void sendTelemetryToPico() {
  // ── STAT: ────────────────────────────────────────────────────────────────────
  int haz = (emergencyStop || tiltDetected) ? 1 : 0;

  String t = F("STAT:");
  t += String(gasLevel);             t += ':';   // 0 gas
  t += String(ambTemp,  1);          t += ':';   // 1 temp
  t += String(humidity, 1);          t += ':';   // 2 hum
  t += String(haz);                  t += ':';   // 3 haz
  t += (pirDetected  ? "1" : "0");   t += ':';   // 4 pir
  t += (tiltDetected ? "1" : "0");   t += ':';   // 5 tilt
  t += ((irFront||irRear||irLeft||irRight) ? "1":"0"); t += ':'; // 6 ir
  t += String(battVolt, 2);          t += ':';   // 7 volt
  t += String((int)battPct);         t += ':';   // 8 pct
  t += String(currentAmps, 2);                   // 9 amps
  Serial1.println(t);

  // ── US: ──────────────────────────────────────────────────────────────────────
  String u = F("US:");
  u += String(dFront); u += ',';
  u += String(dRear);  u += ',';
  u += String(dLeft);  u += ',';
  u += String(dRight);
  Serial1.println(u);
  toS9(u);

  // ── STATUS| ───────────────────────────────────────────────────────────────────
  String s = F("STATUS|ESTOP:");
  s += (emergencyStop  ? "YES" : "NO");
  s += F("|AUTO:");
  s += (autonomousMode ? "ON"  : "OFF");
  s += F("|BAT:");
  s += String(battVolt, 2);
  s += F("|PCT:");
  s += String((int)battPct);
  s += F("|HTEMP:");
  s += String(headTemp, 1);
  s += F("|FANS:HB:");
  s += (digitalRead(FAN_HEAD_BLOW_PIN) ? "1" : "0");
  s += F(",HE:");
  s += (digitalRead(FAN_HEAD_EXT_PIN)  ? "1" : "0");
  s += F(",BD:");
  s += (digitalRead(FAN_BODY_PIN)      ? "1" : "0");
  s += F("|UV:");
  s += (uvActive ? "ON" : "OFF");
  s += F("|R3:");
  s += (r3CommFail  ? F("FAIL") : F("OK"));
  s += F("|ESP:");
  s += (esp32Ready  ? F("OK")   : F("WAIT"));
  s += F("|S9:");
  s += (s9Connected ? F("OK")   : F("WAIT"));
  s += F("|FW:");
  s += FW_VERSION;
  s += F("|CHG:");
  s += (isCharging ? F("YES") : F("NO"));
  Serial1.println(s);
  Serial3.println(s);
}

// ════════════════════════════════════════════════════════════════════
//  TELEMETRY — ESP32  (Serial3)
// ════════════════════════════════════════════════════════════════════
void sendTelemetryToESP32() {
  String t = F("TELEM:");
  if      (emergencyStop)  t += F("ESTOP");
  else if (autonomousMode) t += F("AUTO");
  else if (nav.isMoving)   t += F("MOVING");
  else                     t += F("IDLE");
  t += ':';
  t += String(battVolt,   1);  t += ':';
  t += (autonomousMode ? F("Auto") : F("Manual")); t += ':';
  t += String(dFront);   t += ':';
  t += String(dLeft);    t += ':';
  t += String(dRight);   t += ':';
  t += String(dRear);    t += ':';
  t += String(ambTemp,  1);    t += ':';
  t += String((int)humidity);  t += ':';
  t += String(gasLevel);
  Serial3.println(t);

  // Compact STAT for web dashboard — same field order as Pico STAT above
  int haz = (emergencyStop || tiltDetected) ? 1 : 0;
  String s = F("STAT:");
  s += String(gasLevel);             s += ':';   // 0 gas
  s += String(ambTemp, 1);           s += ':';   // 1 temp
  s += String(humidity, 1);          s += ':';   // 2 hum
  s += String(haz);                  s += ':';   // 3 haz
  s += (pirDetected  ? "1" : "0");   s += ':';   // 4 pir
  s += (tiltDetected ? "1" : "0");   s += ':';   // 5 tilt
  s += ((irFront||irRear||irLeft||irRight) ? "1":"0"); s += ':'; // 6 ir
  s += String(battVolt, 2);          s += ':';   // 7 volt
  s += String((int)battPct);         s += ':';   // 8 pct
  s += String(currentAmps, 2);                   // 9 amps
  Serial3.println(s);

  String u = F("US:");
  u += String(dFront); u += ',';
  u += String(dRear);  u += ',';
  u += String(dLeft);  u += ',';
  u += String(dRight);
  Serial3.println(u);
}

// ════════════════════════════════════════════════════════════════════
//  TIERED BATTERY WARNINGS  (edge-triggered — no spamming)
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
      toS9("EVENT:BATTERY_WARN");
      Serial1.println(F("BAT:WARN"));
      Serial3.println(F("BAT:WARN"));
      beep(1200, 200);
      break;
    case BAT_TIER_LOW:
      toS9("EVENT:BATTERY_LOW");
      Serial1.println(F("BAT:LOW"));
      Serial3.println(F("BAT:LOW"));
      sendMotor("SLOW");
      beep(1800, 300);
      break;
    case BAT_TIER_CRITICAL:
      emergencyStop = true;
      sendMotor("STOP");
      toS9("EVENT:BATTERY_CRITICAL");
      Serial1.println(F("BAT:CRITICAL"));
      Serial3.println(F("BAT:CRITICAL"));
      beep(2500, 1000);
      break;
    default: break;
  }
}

// ════════════════════════════════════════════════════════════════════
//  SAFETY  (tilt = stop, not latching ESTOP; gas = alert only)
// ════════════════════════════════════════════════════════════════════
void checkSafety() {
  checkBatteryTiers();

  // Battery over-temperature → latching ESTOP + force all fans
  if (battTemp > BAT_CTEMP && !emergencyStop) {
    emergencyStop = true;
    sendMotor("STOP");
    // Force all fans on immediately (don't wait for next updateFans() tick)
    digitalWrite(FAN_BODY_PIN,      HIGH);
    digitalWrite(FAN_HEAD_BLOW_PIN, HIGH);
    digitalWrite(FAN_HEAD_EXT_PIN,  HIGH);
    toS9("EVENT:OVERTEMP");
    Serial1.println(F("SAFETY:OVERTEMP"));
    Serial3.println(F("ALERT:OVERTEMP"));
    beep(2500, 1000);
  }

  // Tilt: stop motors but NOT a latching ESTOP (recoverable)
  if (tiltDetected && sens.tilt) {
    sendMotor("STOP");
    toS9("EVENT:TILT");
    Serial1.println(F("SAFETY:TILT"));
    Serial3.println(F("ALERT:TILT_DETECTED"));
    beep(2000, 300);
  }

  // Gas: alert only (analog threshold — tune GAS_ALERT_THRESHOLD to suit your sensor)
  if (sens.gas && gasLevel > GAS_ALERT_THRESHOLD) {
    toS9("EVENT:GAS_ALERT");
    Serial1.println(F("SAFETY:GAS_ALERT"));
    Serial3.println(F("ALERT:GAS_DETECTED"));
  }

  // Fans and UV are managed by their own update functions (called from loop)
}

// ════════════════════════════════════════════════════════════════════
//  ESTOP AUTO-RECOVERY  (3× retry)
// ════════════════════════════════════════════════════════════════════
void handleEstopRecovery() {
  if (estopT == 0) {
    estopT = millis();
    motorCommPrintln(F("MOTOR|S"));
    nav.isMoving = false;
    return;
  }
  if (millis() - estopT > 5000) {
    estopRetries++;
    if (estopRetries < MAX_ESTOP) {
      if (battVolt > BAT_MIN && battTemp < BAT_CTEMP) {
        emergencyStop = false;
        estopT = 0;
        toS9("ESTOP|CLEARED|Auto-restart");
        beep(1500, 200);
      } else {
        estopT = millis();
      }
    } else {
      toS9("ESTOP|MANUAL_REQUIRED|Max retries reached");
      beep(2000, 1000);
      estopT = millis();
    }
  }
}

// ════════════════════════════════════════════════════════════════════
//  S9 COMMUNICATION
// ════════════════════════════════════════════════════════════════════
void sendStatusToS9() {
  String s = F("TELE:");
  s += String(battVolt, 2); s += ',';
  s += String((int)battPct); s += ',';
  s += (nav.isMoving ? "1" : "0");
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
      if (s9Buffer.length() > 80) {
        Serial.print(F("[WARN] S9 buffer overflow: "));
        Serial.println(s9Buffer);
        s9Buffer = "";
      }
    }
  }
}

void processS9Command(String cmd) {
  cmd.trim();
  if (cmd.length() == 0) return;
  Serial.print(F("[RECV] S9: ")); Serial.println(cmd);
  s9LastHB    = millis();
  s9Connected = true;

  // ── Motor commands ───────────────────────────────────────────────────────────
  if (cmd == "MOTOR:F")     { sendMotor("FORWARD");  toS9("ACK|MOTOR:F|END"); return; }
  if (cmd == "MOTOR:B")     { sendMotor("BACKWARD"); toS9("ACK|MOTOR:B|END"); return; }
  if (cmd == "MOTOR:L")     { sendMotor("LEFT");     toS9("ACK|MOTOR:L|END"); return; }
  if (cmd == "MOTOR:R")     { sendMotor("RIGHT");    toS9("ACK|MOTOR:R|END"); return; }
  if (cmd == "MOTOR:S")     { sendMotor("STOP");     toS9("ACK|MOTOR:S|END"); return; }
  if (cmd == "MOTOR:DANCE") {
    mQueue.pending = false;
    motorCommPrintln(F("MOTOR|DANCE"));
    toS9("ACK|DANCE|END");
    return;
  }
  if (cmd == "DEFENSE") {
    mQueue.pending = false;
    motorCommPrintln(F("DEFENSE"));
    toS9("ACK|DEFENSE|END");
    return;
  }

  // ── Speed ────────────────────────────────────────────────────────────────────
  if (cmd.startsWith("SPEED:")) { motorCommPrintln(cmd); toS9("ACK|" + cmd + "|END"); return; }

  // ── Autonomous mode ──────────────────────────────────────────────────────────
  if (cmd == "AUTO:ON")  { autonomousMode = true;  toS9("ACK|AUTO_ON|END");  return; }
  if (cmd == "AUTO:OFF") { autonomousMode = false; sendMotor("STOP"); toS9("ACK|AUTO_OFF|END"); return; }

  // ── Emergency stop ───────────────────────────────────────────────────────────
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
    estopT        = 0;
    toS9("ACK|ESTOP_CLEARED|END");
    return;
  }

  // ── Fan control ──────────────────────────────────────────────────────────────
  if (cmd == "FAN_HEAD:ON")   { fanHeadAuto=false; fanHeadOn=true;  updateFans(); toS9("ACK|FAN_HEAD:ON|END");   return; }
  if (cmd == "FAN_HEAD:OFF")  { fanHeadAuto=false; fanHeadOn=false; updateFans(); toS9("ACK|FAN_HEAD:OFF|END");  return; }
  if (cmd == "FAN_HEAD:AUTO") { fanHeadAuto=true;                   updateFans(); toS9("ACK|FAN_HEAD:AUTO|END"); return; }
  if (cmd == "FAN_BODY:ON")   { fanBodyAuto=false; fanBodyOn=true;  updateFans(); toS9("ACK|FAN_BODY:ON|END");   return; }
  if (cmd == "FAN_BODY:OFF")  { fanBodyAuto=false; fanBodyOn=false; updateFans(); toS9("ACK|FAN_BODY:OFF|END");  return; }
  if (cmd == "FAN_BODY:AUTO") { fanBodyAuto=true;                   updateFans(); toS9("ACK|FAN_BODY:AUTO|END"); return; }
  if (cmd == "FAN_ALL:ON")    {
    fanHeadAuto=false; fanHeadOn=true;
    fanBodyAuto=false; fanBodyOn=true;
    updateFans(); toS9("ACK|FAN_ALL:ON|END"); return;
  }
  if (cmd == "FAN_ALL:OFF")   {
    fanHeadAuto=false; fanHeadOn=false;
    fanBodyAuto=false; fanBodyOn=false;
    updateFans(); toS9("ACK|FAN_ALL:OFF|END"); return;
  }

  // ── UV light strip ───────────────────────────────────────────────────────────
  if (cmd == "UV:ON")   { uvManualOn=true;  uvAuto=false; updateUV(); toS9("ACK|UV:ON|END");   return; }
  if (cmd == "UV:OFF")  { uvManualOn=false; uvAuto=false; updateUV(); toS9("ACK|UV:OFF|END");  return; }
  if (cmd == "UV:AUTO") { uvAuto=true;                    updateUV(); toS9("ACK|UV:AUTO|END"); return; }

  // ── Mode forwarding ──────────────────────────────────────────────────────────
  if (cmd.startsWith("MODE:")) {
    String mode = cmd.substring(5);
    toS9("ACK|MODE:" + mode + "|END");
    Serial1.print(F("MODE:")); Serial1.println(mode);
    Serial3.print(F("MODE:")); Serial3.println(mode);
    return;
  }

  // ── Sensor toggle ────────────────────────────────────────────────────────────
  if (cmd.startsWith("TOGGLE_SENSOR:")) { applyToggle(cmd); return; }
  if (cmd == "SENSOR_STATUS") {
    String ss = sensorStatusString();
    toS9(ss);
    Serial1.println(ss);   // also reply to Pico if S9 requested it
    return;
  }

  // ── Face / object passthrough to Pico ─────────────────────────────────────────
  if (cmd.startsWith("FACE:")) { lastFace = cmd.substring(5); return; }
  if (cmd.startsWith("OBJ:"))  { Serial1.println(cmd); return; }
  if (cmd.startsWith("SENS|")) { Serial1.println(cmd); return; }

  // ── Diagnostics ──────────────────────────────────────────────────────────────
  if (cmd == "DIAG" || cmd == "DIAG:RUN") {
    String d = F("DIAG|");
    d += "BAT:" + String(battVolt,2) + "V|PCT:" + String((int)battPct) + "%|";
    d += "TEMP:" + String(ambTemp,1) + "C|HTEMP:" + String(headTemp,1) + "C|";
    d += "F:" + String(dFront) + "cm|R:" + String(dRear) + "cm|";
    d += "L:" + String(dLeft)  + "cm|Ri:" + String(dRight) + "cm|";
    d += "UV:" + String(uvActive?"ON":"OFF") + "|";
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
  if (cmd == "R3:RETRY")  { r3CommFail = false; runR3CommTest(); return; }

  if (cmd == "NOTIFY:PATROL_START") { toS9("ACK|PATROL_START|END"); return; }
  if (cmd == "KEEP_DISTANCE")       { toS9("ACK|KEEP_DISTANCE|END"); return; }
}

// ════════════════════════════════════════════════════════════════════
//  PICO DASHBOARD COMMUNICATION  (Serial1 receive)
// ════════════════════════════════════════════════════════════════════
void processPicoCommand(String cmd) {
  if (debugVerbose) { Serial.print(F("[PICO] RX: ")); Serial.println(cmd); }
  picoLinked = true;

  // B1: PING_PICO handshake
  if (cmd.startsWith("PING_PICO:")) {
    picoLastPingMs = millis();
    picoPingSeq    = (uint8_t)cmd.substring(10).toInt();
    Serial1.print(F("PONG_PICO:"));
    Serial1.println(picoPingSeq);
    return;
  }

  if (cmd.startsWith("MODE:")) {
    String mode = cmd.substring(5);
    // Do NOT echo back to Serial1 — that would loop back to the Pico
    toS9("REQ_MODE:" + mode);
    Serial3.print(F("MODE:")); Serial3.println(mode);
    return;
  }

  if (cmd.startsWith("TOGGLE_SENSOR:")) { applyToggle(cmd); return; }

  // B2: Pico requests sensor config on boot — reply directly to Serial1
  if (cmd == "SENSOR_STATUS") {
    Serial1.println(sensorStatusString());
    return;
  }

  // B3: Pico footer ESTOP button — was silently dropped before
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
    estopT        = 0;
    toS9("ACK|ESTOP_CLEARED|END");
    return;
  }

  // B4: Pico boot greeting — picoLinked already set above, just consume it
  if (cmd == "PONG") { return; }

  if (debugVerbose) { Serial.print(F("[PICO] Unknown: ")); Serial.println(cmd); }
}

void handlePicoCommunication() {
  while (Serial1.available()) {
    char c = Serial1.read();
    if (c == '\n') {
      picoBuf.trim();
      // Strip CRC suffix appended by toPico() on the other end
      int crcIdx = picoBuf.indexOf("|CRC:");
      if (crcIdx > 0) picoBuf = picoBuf.substring(0, crcIdx);
      if (picoBuf.length() > 0) processPicoCommand(picoBuf);
      picoBuf = "";
    } else if (c != '\r') {
      picoBuf += c;
      if (picoBuf.length() > 80) picoBuf = "";
    }
  }
}

// ════════════════════════════════════════════════════════════════════
//  R3 MOTOR SHIELD COMMUNICATION
// ════════════════════════════════════════════════════════════════════
void processR3Response(String resp) {
  if (debugVerbose) { Serial.print(F("[R3] RX: ")); Serial.println(resp); }
  if (resp.startsWith("ACK:MOTOR|")) { toS9("ACK|" + resp + "|END"); return; }
  if (resp == "ACK:DANCE:DONE")      { toS9("ACK|DANCE:DONE|END");   return; }
  if (resp == "ACK:DEFENSE:DONE")    { toS9("ACK|DEFENSE:DONE|END"); return; }
  if (resp.startsWith("PONG:"))      { toS9("PONG|" + resp.substring(5) + "|END"); return; }
  if (resp.startsWith("ERR:"))       { toS9("ERR|" + resp.substring(4) + "|END");  return; }
  if (debugVerbose) { Serial.print(F("[R3] Unknown: ")); Serial.println(resp); }
}

void handleR3Communication() {
  while (motorComm.available()) {
    char c = motorComm.read();
    if (c == '\n') {
      r3Buf.trim();
      if (r3Buf.length() > 0) processR3Response(r3Buf);
      r3Buf = "";
    } else if (c != '\r') {
      r3Buf += c;
      if (r3Buf.length() > 80) r3Buf = "";
    }
  }
}

// ════════════════════════════════════════════════════════════════════
//  ESP32 BRIDGE COMMUNICATION  (Serial3)
// ════════════════════════════════════════════════════════════════════
void processMotorOrModeCmd(String cmd) {
  if      (cmd == "MOTOR|F") { autonomousMode=false; sendMotor("FORWARD");  toS9("ACK|MOTOR|F|END"); }
  else if (cmd == "MOTOR|B") { autonomousMode=false; sendMotor("BACKWARD"); toS9("ACK|MOTOR|B|END"); }
  else if (cmd == "MOTOR|L") { autonomousMode=false; sendMotor("LEFT");     toS9("ACK|MOTOR|L|END"); }
  else if (cmd == "MOTOR|R") { autonomousMode=false; sendMotor("RIGHT");    toS9("ACK|MOTOR|R|END"); }
  else if (cmd == "MOTOR|S") { autonomousMode=false; sendMotor("STOP");     toS9("ACK|MOTOR|S|END"); }
  else if (cmd.startsWith("MODE|") || cmd.startsWith("MODE:")) {
    String mode = cmd.substring(5);
    toS9("ACK|MODE:" + mode + "|END");
    Serial1.print(F("MODE:")); Serial1.println(mode);
  }
}

void processESP32Command(String cmd) {
  if (debugVerbose) { Serial.print(F("[ESP32] RX: ")); Serial.println(cmd); }

  if (cmd == "READY") { esp32Ready = true; return; }

  // Forward ESP32 WiFi IP to S9 app so it can auto-connect via HTTP
  if (cmd.startsWith("WIFI_IP:")) {
    String ip = cmd.substring(8);
    ip.trim();
    if (ip.length() > 0) {
      toS9("WIFI_IP:" + ip);
      Serial.print(F("[MEGA] Forwarded WiFi IP to S9: ")); Serial.println(ip);
    }
    return;
  }

  if (cmd.startsWith("BTCMD|"))  { processMotorOrModeCmd(cmd.substring(6)); return; }

  if (cmd.startsWith("WEBCMD|")) {
    String sub = cmd.substring(7);
    if (sub == "SAFETY|CLR") {
      emergencyStop = false; estopRetries = 0; estopT = 0;
      toS9("ACK|ESTOP_CLEARED|END");
      return;
    }
    // Fan and UV forwarding from web dashboard
    if (sub.startsWith("FAN_") || sub.startsWith("UV:")) { processS9Command(sub); return; }
    processMotorOrModeCmd(sub);
    return;
  }

  if (cmd.startsWith("CMD:")) {
    String sub = cmd.substring(4);
    sub.toUpperCase();
    if      (sub == "F")      { autonomousMode=false; sendMotor("FORWARD");  }
    else if (sub == "B")      { autonomousMode=false; sendMotor("BACKWARD"); }
    else if (sub == "L")      { autonomousMode=false; sendMotor("LEFT");     }
    else if (sub == "R")      { autonomousMode=false; sendMotor("RIGHT");    }
    else if (sub == "S")      { autonomousMode=false; sendMotor("STOP");     }
    else if (sub == "AUTO")   { autonomousMode=!autonomousMode; if(!autonomousMode) sendMotor("STOP"); }
    else if (sub == "DANCE")  { motorCommPrintln(F("MOTOR|DANCE")); }
    else if (sub == "SLOW")   { sendMotor("SLOW");   }
    else if (sub == "NORMAL") { sendMotor("NORMAL"); }
    else if (sub == "FAST")   { sendMotor("FAST");   }
    else if (sub == "ESTOP")  { emergencyStop=true; sendMotor("STOP"); }
    else if (sub == "CLEAR")  { emergencyStop=false; estopRetries=0; estopT=0; }
    else if (sub.startsWith("TOGGLE_SENSOR:")) { applyToggle(sub); }
    return;
  }
}

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

// ════════════════════════════════════════════════════════════════════
//  GPS  (Serial2 — TinyGPS++)
// ════════════════════════════════════════════════════════════════════
void handleGPS() {
  if (!sens.gps) return;
  while (gpsSerial.available()) gps.encode(gpsSerial.read());
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
  noInterrupts();
  if (!rfReceiver.available()) { interrupts(); return; }
  unsigned long code = rfReceiver.getReceivedValue();
  rfReceiver.resetAvailable();
  interrupts();
  if (code == 0 || code == lastRFCode) return;
  lastRFCode = code; lastRFTime = millis();

  if      (code == RF_FWD)  { sendMotor("FORWARD");  toS9("RF:FORWARD");  }
  else if (code == RF_BWD)  { sendMotor("BACKWARD"); toS9("RF:BACKWARD"); }
  else if (code == RF_LFT)  { sendMotor("LEFT");     toS9("RF:LEFT");     }
  else if (code == RF_RGT)  { sendMotor("RIGHT");    toS9("RF:RIGHT");    }
  else if (code == RF_STP)  { sendMotor("STOP"); autonomousMode=false; toS9("RF:STOP"); }
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
  if (GESTURE_INT < 0) return;   // sensor not connected
  uint8_t data = 0;
  paj7620ReadReg(0x43, 1, &data);
  if (data == 0) return;
  const char* g = nullptr;
  if      (data == GES_UP_FLAG)        { g="UP";    sendMotor("FORWARD");  }
  else if (data == GES_DOWN_FLAG)      { g="DOWN";  sendMotor("BACKWARD"); }
  else if (data == GES_LEFT_FLAG)      { g="LEFT";  sendMotor("LEFT");     }
  else if (data == GES_RIGHT_FLAG)     { g="RIGHT"; sendMotor("RIGHT");    }
  else if (data == GES_FORWARD_FLAG)   { g="NEAR";  sendMotor("STOP");     }
  else if (data == GES_CLOCKWISE_FLAG) { g="CW";    motorCommPrintln(F("MOTOR|DANCE")); }
  if (g) {
    Serial1.print(F("GESTURE:")); Serial1.println(g);
    toS9("GESTURE:" + String(g));
    Serial3.print(F("GESTURE:")); Serial3.println(g);
  }
}

// ════════════════════════════════════════════════════════════════════
//  MOMENTARY BUTTON  (A11 — toggles autonomous mode)
//  Confirmation beep replaces the headlight blink from V30.
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
      // Two-tone confirmation beep (was headlight blink)
      beep(autonomousMode ? 1500 : 1000, 120);
      delay(60);
      beep(autonomousMode ? 1800 : 800,  120);
    }
  } else {
    btnPressed = false;
  }
}

// ════════════════════════════════════════════════════════════════════
//  AUTONOMOUS NAVIGATION
// ════════════════════════════════════════════════════════════════════
void lookAndDecide() {
  if (lookState == LOOK_IDLE) {
    sendMotor("STOP");
    navTimer  = millis() + 150;
    lookState = LOOK_STOP;
    return;
  }
  if (lookState == LOOK_STOP && millis() >= navTimer) {
    bool leftClear  = (dLeft  > SIDE_MIN || dLeft  < 0);
    bool rightClear = (dRight > SIDE_MIN || dRight < 0);
    if (leftClear && (dLeft > dRight || !rightClear)) {
      sendMotor("LEFT");  navTimer = millis() + T45; lookState = LOOK_LEFT;
    } else if (rightClear) {
      sendMotor("RIGHT"); navTimer = millis() + T45; lookState = LOOK_RIGHT;
    } else {
      sendMotor("BACKWARD"); navTimer = millis() + 800; lookState = LOOK_BACK_LEFT;
    }
    return;
  }
  if (lookState == LOOK_LEFT      && millis() >= navTimer) { sendMotor("FORWARD"); nav.isMoving=true; lookState=LOOK_IDLE; return; }
  if (lookState == LOOK_RIGHT     && millis() >= navTimer) { sendMotor("FORWARD"); nav.isMoving=true; lookState=LOOK_IDLE; return; }
  if (lookState == LOOK_BACK_LEFT && millis() >= navTimer) { sendMotor("LEFT");  navTimer=millis()+T90; lookState=LOOK_FORWARD; return; }
  if (lookState == LOOK_FORWARD   && millis() >= navTimer) { sendMotor("FORWARD"); nav.isMoving=true; lookState=LOOK_IDLE; return; }
}

void handleStuck() {
  if (stuckState == STUCK_IDLE)  { sendMotor("STOP");     navTimer=millis()+150;  stuckState=STUCK_STOP; return; }
  if (stuckState == STUCK_STOP  && millis()>=navTimer) { sendMotor("BACKWARD"); navTimer=millis()+1200; stuckState=STUCK_BACK; return; }
  if (stuckState == STUCK_BACK  && millis()>=navTimer) { sendMotor("LEFT");    navTimer=millis()+T90;  stuckState=STUCK_LEFT; return; }
  if (stuckState == STUCK_LEFT  && millis()>=navTimer) {
    sendMotor("FORWARD");
    nav.stuckDetected=false; nav.stuckStart=0;
    stuckState=STUCK_IDLE;
    return;
  }
}

void makeAutonomousDecision() {
  if (emergencyStop) { sendMotor("STOP"); return; }
  bool isTurning = (randomState==RANDOM_TURN || lookState!=LOOK_IDLE || avoidState==AVOID_TURN);
  if (nav.isMoving && !nav.isAvoiding && !isTurning) {
    if (abs(dFront - nav.lastFrontDist) < 5) {
      if (nav.stuckStart == 0) nav.stuckStart = millis();
      else if (millis() - nav.stuckStart > 3000) { handleStuck(); return; }
    } else { nav.stuckStart=0; nav.stuckDetected=false; }
  }
  nav.lastFrontDist = dFront;
  if (nav.isAvoiding) return;
  if (!nav.isMoving) {
    if (dFront > OBS_WARN || dFront < 0) sendMotor("FORWARD");
    else lookAndDecide();
  } else {
    if      (dFront > 0 && dFront < OBS_SLOW) sendMotor("SLOW");
    else if (dFront > OBS_SLOW)               sendMotor("NORMAL");
  }
}

void handleRandomTurn() {
  if (randomState == RANDOM_IDLE && nav.isMoving && random(1000) > 992) {
    (random(2)==0) ? sendMotor("LEFT") : sendMotor("RIGHT");
    navTimer = millis() + 180;
    randomState = RANDOM_TURN;
  }
  if (randomState == RANDOM_TURN && millis() >= navTimer) {
    sendMotor("FORWARD");
    randomState = RANDOM_IDLE;
  }
}

void collisionAvoidance() {
  if (!autonomousMode) { avoidState=AVOID_IDLE; nav.isAvoiding=false; return; }
  if (millis() - lastSenseTs > 600) return;  // stale data guard

  if (sens.us && dFront>0 && dFront<OBS_STOP && !nav.isAvoiding && avoidState==AVOID_IDLE) {
    nav.isAvoiding=true; nav.avoidStart=millis(); nav.avoidAttempts++;
    sendMotor("STOP"); navTimer=millis()+100; avoidState=AVOID_STOP; return;
  }
  if (avoidState==AVOID_STOP && millis()>=navTimer) {
    sendMotor("BACKWARD"); navTimer=millis()+700; avoidState=AVOID_BACK; return;
  }
  if (avoidState==AVOID_BACK && millis()>=navTimer) {
    sendMotor("STOP"); navTimer=millis()+150; avoidState=AVOID_STOP2; return;
  }
  if (avoidState==AVOID_STOP2 && millis()>=navTimer) {
    bool lc2=(dLeft<0||dLeft>SIDE_MIN); bool rc2=(dRight<0||dRight>SIDE_MIN);
    if      (lc2 && (!rc2 || dLeft>dRight)) { sendMotor("LEFT");  navTimer=millis()+T90; }
    else if (rc2)                            { sendMotor("RIGHT"); navTimer=millis()+T90; }
    else                                     { sendMotor("LEFT");  navTimer=millis()+(T90*2); }
    avoidState=AVOID_TURN; return;
  }
  if (avoidState==AVOID_TURN && millis()>=navTimer) {
    if (autonomousMode) { sendMotor("FORWARD"); nav.isMoving=true; }
    nav.isAvoiding=false; nav.lastAvoidEnd=millis(); avoidState=AVOID_IDLE; return;
  }

  if (sens.ir && irFront && !nav.isAvoiding && avoidState==AVOID_IDLE) {
    sendMotor("STOP"); navTimer=millis()+100; avoidState=AVOID_STOP; return;
  }

  if (nav.isReversing && sens.us && dRear>0 && dRear<15) {
    sendMotor("STOP"); nav.isReversing=false;
  }

  if (nav.avoidAttempts > 5) {
    autonomousMode=false; nav.avoidAttempts=0;
    sendMotor("STOP"); toS9("EVENT:NAVIGATION_FAILED");
  }
  if (nav.avoidAttempts>0 && millis()-nav.lastAvoidEnd>10000) nav.avoidAttempts=0;
}

// ════════════════════════════════════════════════════════════════════
//  R3 COMM TEST + READY WAIT
// ════════════════════════════════════════════════════════════════════
bool waitForR3Line(const String &prefix, unsigned long timeout, String &outLine) {
  unsigned long start = millis();
  String line = "";
  while (millis() - start < timeout) {
    while (motorComm.available()) {
      char c = motorComm.read();
      if (c == '\r' || c == '\n') {
        if (line.length() > 0) {
          line.trim(); outLine = line;
          if (line.startsWith(prefix)) return true;
          line = "";
        }
      } else {
        line += c;
        if (line.length() > 128) line = "";
      }
    }
  }
  return false;
}

bool readR3StatusResponse(unsigned long timeout) {
  unsigned long start = millis();
  String line = "";
  bool sawEnd = false;
  while (millis() - start < timeout) {
    while (motorComm.available()) {
      char c = motorComm.read();
      if (c == '\r' || c == '\n') {
        if (line.length() > 0) {
          line.trim();
          Serial.print("R3 STATUS: "); Serial.println(line);
          if (line == "R3:STATUS:END") { sawEnd = true; return true; }
          line = "";
        }
      } else {
        line += c;
        if (line.length() > 128) line = "";
      }
    }
  }
  return sawEnd;
}

void runR3CommTest() {
  String response;
  motorComm.println(F("PING"));
  if (waitForR3Line("PONG", 2000, response)) {
    Serial.println(F("R3 COMM OK")); Serial1.println(F("R3 COMM OK"));
  } else {
    Serial.println(F("R3 COMM FAIL")); Serial1.println(F("R3 COMM FAIL"));
    r3CommFail = true;
  }
  if (!r3CommFail) {
    motorComm.println(F("MOTOR|S"));
    if (waitForR3Line("ACK:MOTOR|S", 2000, response)) {
      Serial.println(F("ACK:MOTOR|S received")); Serial1.println(F("ACK:MOTOR|S received"));
    } else {
      Serial.println(F("ACK:MOTOR|S timeout")); Serial1.println(F("ACK:MOTOR|S timeout"));
    }
    motorComm.println(F("STATUS"));
    readR3StatusResponse(2000);
  }
}

bool waitForR3Ready() {
  // Give R3 time to boot its SoftwareSerial before we start listening
  // R3 delays 1000ms + 500ms in setup() before announcing READY
  dbg("[R3] Waiting for R3 boot...");
  delay(2000);

  // Flush any garbage that accumulated during R3 boot
  while (Serial2.available()) Serial2.read();

  unsigned long timeout = millis() + 5000;
  String response = "";
  while (millis() < timeout) {
    if (motorComm.available()) {
      char c = motorComm.read();
      if (c == '\r' || c == '\n') {
        if (response.indexOf("R3:READY") != -1 || response.indexOf("PONG") != -1) return true;
        response = "";
      } else response += c;
    }
  }
  dbg("[WARN] R3 ready timeout — proceeding");
  return false;
}

// ════════════════════════════════════════════════════════════════════
//  STARTUP
// ════════════════════════════════════════════════════════════════════
void initPins() {
  // ── Analog inputs ────────────────────────────────────────────────────────────
  pinMode(VOLTAGE_SENSOR,   INPUT);
  pinMode(TEMP_SENSOR_1,    INPUT);
  pinMode(HEAD_TEMP_SENSOR, INPUT);   // placeholder — safe to configure now
  pinMode(LDR_AO,           INPUT);
  if (GAS_AO   >= 0) pinMode(GAS_AO,   INPUT);
  if (SOUND_AO >= 0) pinMode(SOUND_AO, INPUT);

  // ── Digital inputs ───────────────────────────────────────────────────────────
  pinMode(LDR_DO,       INPUT);
  pinMode(REAR_IR,      INPUT);
  pinMode(FRONT_IR,     INPUT);
  if (LEFT_IR  >= 0) pinMode(LEFT_IR,  INPUT);
  if (RIGHT_IR >= 0) pinMode(RIGHT_IR, INPUT);
  pinMode(TILT_SENSOR,  INPUT);
  if (PIR_PIN  >= 0) pinMode(PIR_PIN,  INPUT);
  pinMode(UNHINGED_SW,  INPUT_PULLUP);
  pinMode(MOMENTARY_BTN,INPUT_PULLUP);
  pinMode(GESTURE_INT,  INPUT);
  pinMode(CURRENT_SENSOR,INPUT);

  // ── Echo pins ────────────────────────────────────────────────────────────────
  pinMode(FRONT_ECHO, INPUT); pinMode(LEFT_ECHO,  INPUT);
  pinMode(RIGHT_ECHO, INPUT); pinMode(REAR_ECHO,  INPUT);

  // ── Trigger pins ─────────────────────────────────────────────────────────────
  pinMode(FRONT_TRIG, OUTPUT); digitalWrite(FRONT_TRIG, LOW);
  pinMode(LEFT_TRIG,  OUTPUT); digitalWrite(LEFT_TRIG,  LOW);
  pinMode(RIGHT_TRIG, OUTPUT); digitalWrite(RIGHT_TRIG, LOW);
  pinMode(REAR_TRIG,  OUTPUT); digitalWrite(REAR_TRIG,  LOW);

  // ── Output devices ───────────────────────────────────────────────────────────
  pinMode(FAN_BODY_PIN,      OUTPUT); digitalWrite(FAN_BODY_PIN,      LOW);
  pinMode(FAN_HEAD_BLOW_PIN, OUTPUT); digitalWrite(FAN_HEAD_BLOW_PIN, LOW);
  pinMode(FAN_HEAD_EXT_PIN,  OUTPUT); digitalWrite(FAN_HEAD_EXT_PIN,  LOW);
  pinMode(UV_LIGHT_PIN,      OUTPUT); digitalWrite(UV_LIGHT_PIN,      LOW);  // SAFE default OFF
  pinMode(BUZZER_PIN,        OUTPUT); digitalWrite(BUZZER_PIN,        LOW);
  pinMode(CHARGE_DETECT_PIN, INPUT_PULLUP);  // Barrel jack third wire — LOW=charging
}

// ════════════════════════════════════════════════════════════════════
//  MANUAL CHARGE DETECTION
//  Barrel jack third wire: 0V = charger plugged in, 8.4V = unplugged.
//  8.4V is stepped to ~3.4V via 10kΩ/6.8kΩ voltage divider on pin 27.
//  LOW  = charging   HIGH = not charging
// ════════════════════════════════════════════════════════════════════
void checkManualCharging() {
  isCharging = (digitalRead(CHARGE_DETECT_PIN) == LOW);

  if (isCharging && !wasCharging) {
    wasCharging    = true;
    autonomousMode = false;
    sendMotor("STOP");
    toS9("CHARGE:MANUAL:CONNECTED");
    Serial1.println(F("CHARGE:MANUAL:CONNECTED"));
    Serial3.println(F("CHARGE:MANUAL:CONNECTED"));
    dbg("[CHARGE] Manual charger connected");
    beep(1000, 80); delay(100); beep(1300, 80);
  }

  if (!isCharging && wasCharging) {
    wasCharging = false;
    toS9("CHARGE:MANUAL:DISCONNECTED");
    Serial1.println(F("CHARGE:MANUAL:DISCONNECTED"));
    Serial3.println(F("CHARGE:MANUAL:DISCONNECTED"));
    dbg("[CHARGE] Manual charger disconnected");
    beep(800, 150);
  }
}

void startupSequence() {
  // Brief test pulse on all three fans (confirms wiring)
  digitalWrite(FAN_HEAD_BLOW_PIN, HIGH);
  digitalWrite(FAN_HEAD_EXT_PIN,  HIGH);
  digitalWrite(FAN_BODY_PIN,      HIGH);
  delay(400);
  digitalWrite(FAN_HEAD_BLOW_PIN, LOW);
  digitalWrite(FAN_HEAD_EXT_PIN,  LOW);
  digitalWrite(FAN_BODY_PIN,      LOW);

  // Boot tone
  beep(800, 80); delay(100); beep(1200, 80); delay(100); beep(1600, 150);
}

// ════════════════════════════════════════════════════════════════════
//  SETUP
// ════════════════════════════════════════════════════════════════════
void setup() {
  // Start USB debug first so we can see everything
  Serial.begin(115200);
  while (!Serial && millis() < 4000) {}
  delay(250);

  Serial1.begin(115200);  // Pico
  Serial2.begin(9600);    // R3 — started before STOP sequence below
  Serial3.begin(115200);  // ESP32
  gpsSerial.begin(9600);        // GPS NEO-6M via SoftwareSerial

  delay(400);
  Wire.begin();
  dht.begin();
  initPins();

  rfReceiver.enableReceive(digitalPinToInterrupt(RF_PIN));  // INT4 on pin 2
  attachInterrupt(digitalPinToInterrupt(CURRENT_SENSOR), currentPulseISR, RISING);

  if (GESTURE_INT >= 0) {
    if (paj7620Init() == 0) { dbg("[INIT] PAJ7620 OK"); }
    else                     { dbg("[INIT] PAJ7620 FAILED"); }
  } else {
    dbg("[INIT] PAJ7620 skipped — not connected");
  }

  readAllSensors();
  waitForR3Ready();
  startupSequence();

  systemReady    = true;
  autonomousMode = false;
  bootStartTime  = millis();
  nav.lastAvoidEnd = millis();

  // Boot announcements
  toS9("SYSTEM|READY|" + String(FW_VERSION) + "|END");

  {
    String boot = F("MEGA_READY|FW:");
    boot += FW_VERSION;
    boot += F("|END");
    Serial1.println(boot);
  }
  Serial1.println(sensorStatusString());
  Serial3.println(F("MEGA:BOOT"));

  runR3CommTest();

  {
    String connStatus = F("CONN_STATUS|");
    connStatus += r3CommFail ? F("R3:FAIL|") : F("R3:OK|");
    connStatus += F("MEGA:OK|FW:");
    connStatus += FW_VERSION;
    connStatus += F("|END");
    Serial1.println(connStatus);
  }

  dbg("[READY] BuddyBot " FW_VERSION);
}

// ════════════════════════════════════════════════════════════════════
//  MAIN LOOP
// ════════════════════════════════════════════════════════════════════
void loop() {
  // BOOT LOCK — keep motors dead for first 5 seconds
  if (millis() - bootStartTime < BOOT_LOCK_TIME) {
    sendMotor("STOP");
    drainMotorQueue();
    handleS9Communication();
    handlePicoCommunication();   // [FIX MEGA-06] was missing — Pico PONG/PING dropped during boot
    handleR3Communication();
    handleESP32Communication();
    checkManualCharging();       // always check charge state, even during boot
    return;
  }

  if (!systemReady) { delay(50); return; }
  unsigned long now = millis();

  drainMotorQueue();  // always first

  if (emergencyStop && estopRetries < MAX_ESTOP) handleEstopRecovery();

  unhingedMode = (digitalRead(UNHINGED_SW) == LOW);

  handleS9Communication();
  handlePicoCommunication();
  handleR3Communication();
  handleESP32Communication();
  handleGPS();
  handleRF();

  // S9 connection watchdog
  if (s9Connected && (now - s9LastHB > S9_TIMEOUT)) {
    s9Connected = false;
    dbg("[S9] Disconnected");
  }

  // Pico link watchdog
  if (picoLinked && picoLastPingMs > 0 && (now - picoLastPingMs > 30000)) {
    if (debugVerbose) Serial.println(F("[PICO] WATCHDOG: no ping in 30s"));
    picoLinked     = false;
    picoLastPingMs = 0;
  }

  // Sensor + safety + fans + UV loop (500 ms)
  if (now - lastSense > 500) {
    lastSense = now;
    readAllSensors();
    updatePower();
    updateFans();
    updateUV();
    checkSafety();
    checkGestures();
    handleButton();
    checkManualCharging();   // [FIX MEGA-03] charge detection
  }

  // Telemetry broadcast (1000 ms)
  if (now - lastTelem > 1000) {
    lastTelem = now;
    sendTelemetryToPico();
    sendTelemetryToESP32();
    uptimeSec++;
  }

  // Status to S9 (2000 ms, only when connected)
  if (s9Connected && (now - s9LastSent > 2000)) {
    s9LastSent = now;
    sendStatusToS9();
  }

  // Autonomous navigation (200 ms)
  if (autonomousMode && !emergencyStop && (now - lastNavDec > 200)) {
    lastNavDec = now;
    makeAutonomousDecision();
    lookAndDecide();
    handleStuck();
    handleRandomTurn();
  }

  // Collision avoidance (50 ms — fast loop)
  if (autonomousMode && (now - lastAvoid > 50)) {
    lastAvoid = now;
    collisionAvoidance();
  }

  // Reverse beep
  if (nav.isReversing) {
    static unsigned long lastBeepT = 0;
    if (now - lastBeepT > 500) { lastBeepT = now; beep(1000, 80); }
  }

  // RF debounce reset
  if (now - lastRFTime > 500) lastRFCode = 0;

  // ESP32 handshake re-send (5 s interval) for late-boot ESP32
  if (!esp32Ready && (now - lastEsp32Check > 5000)) {
    lastEsp32Check = now;
    Serial3.println(F("MEGA:BOOT"));
  }
}
