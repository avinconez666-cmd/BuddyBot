/*
 * ════════════════════════════════════════════════════════════════════
 *  BUDDYBOT  ·  KEYESTUDIO MEGA 2560 SMART (KS0509)  ·  PRODUCTION V35.0
 * ════════════════════════════════════════════════════════════════════
 *
 *  DO NOT CHANGE PIN DEFINITIONS WITHOUT EXPLICIT PERMISSION!!!
 *
 *  CHANGES FROM V34
 *  ─────────────────
 *  [BOARD]   Migrated from KS5014 (Mega 2560 Plus WiFi) to KS0509
 *            (Mega 2560 Smart). Built-in ESP8266 WiFi absent on KS0509;
 *            WiFi continues to be handled by the Pico W internal ESP8285.
 *
 *  [MOTOR]   Uno R3 + Adafruit Motor Shield V1 REMOVED.
 *            Motors now driven directly via KS0509 on-board dual TB6612
 *            chips. Serial2 (was R3 link) is now FREE.
 *            sendMotor() calls TB6612 GPIO directly -- no serial hop,
 *            no queue, no r3CommFail, no runR3CommTest().
 *            DANCE and DEFENSE are non-blocking state machines on Mega.
 *
 *  [PINS]    7 defines relocated to clear KS0509 hardwired motor pins
 *            and fix a pre-existing D37 double-assignment bug:
 *              PIR_PIN:           10 -> 23
 *              FAN_BODY_PIN:      11 -> 45  (PWM, Timer5)
 *              FAN_HEAD_BLOW_PIN: 12 -> 46  (PWM, Timer5)
 *              RIGHT_IR:          29 -> 26
 *              LEFT_IR:           30 -> 27
 *              TSOP_LEFT:         34 -> 41
 *              RIGHT_TRIG:        37 -> 42  (was double-assigned with FAN_HEAD_EXT_PIN)
 *
 *  SERIAL CHANNEL MAP (authoritative -- do not change)
 *  ──────────────────────────────────────────────────
 *  Serial   (USB, pins 0/1)      115200  <-> Samsung S9 Android app
 *  Serial1  (pins 18 TX / 19 RX) 115200  <-> Pico W GP5(RX)/GP4(TX)
 *  Serial2  (pins 16 TX / 17 RX)          FREE (was R3 motor link)
 *  Serial3  (pins 14 TX / 15 RX)   9600  <-> GPS NEO-6M (hardware UART3)
 *
 *  KS0509 MOTOR PIN MAP (hardwired on PCB -- do not reassign)
 *  ─────────────────────────────────────────────────────────
 *  Motor A (M4) = Front Left  : IN1=D35, IN2=D34, PWM=D12
 *  Motor B (M3) = Rear  Left  : IN1=D33, IN2=D32, PWM=D11
 *  Motor C (M1) = Front Right : IN1=D29, IN2=D28, PWM=D9
 *  Motor D (M2) = Rear  Right : IN1=D31, IN2=D30, PWM=D10
 *  Right motors are physically mirrored -- fwd=false drives them forward.
 *  If a motor spins wrong way on first test, swap its IN1/IN2 wires.
 *
 *  SENSOR TOGGLE IDs
 *  ──────────────────
 *  DHT  LIGHT  SOUND  GAS  PIR  TILT  IR  US  CURRENT  GPS
 *
 * ════════════════════════════════════════════════════════════════════
 */

#include <TinyGPS++.h>
#include <Wire.h>
#include <DHT.h>
#include "paj7620.h"
#include <util/atomic.h>

// Forward declarations
void setLedMode(String m);
void updateLeds();
void triggerDocking(bool manual=false);
void applyMotorSpeed();
void motorsStop();

// ════════════════════════════════════════════════════════════════════
//  CONFIGURATION
// ════════════════════════════════════════════════════════════════════
#define DEBUG_VERBOSE   false
#define FW_VERSION      "V35.0"

const String PRIORITY_USER = "AJ";

const float BAT_MAX   = 8.4f;
const float BAT_MIN   = 6.0f;
const float BAT_LOW   = 6.6f;
const float BAT_WARN  = 7.0f;
const float BAT_VDIV  = 5.62f;
const float BAT_CTEMP = 50.0f;
const float BAT_WTEMP = 45.0f;

const float HEAD_FAN_TEMP = 35.0f;

const int OBS_STOP = 40;
const int OBS_SLOW = 50;
const int OBS_WARN = 60;
const int SIDE_MIN = 35;
const int T45      = 350;
const int T90      = 700;

const int GAS_ALERT_THRESHOLD  = 400;  // retained for STAT telemetry field
const int GAS_SMOKE_THRESHOLD  = 500;  // ADC trigger -- smoke alarm + ESTOP
const int GAS_CLEAR_THRESHOLD  = 280;  // ADC value considered clean air
const unsigned long GAS_WARMUP_MS     = 30000UL; // MQ2 heater stabilise time (30 s)
const unsigned long GAS_CLEAR_HOLD_MS = 10000UL; // clear must hold 10 s before auto-reset

const float CURRENT_SENSITIVITY = 0.066f;
const int   CURRENT_ZERO_OFFSET = 512;
const float CURRENT_VREF        = 5.0f;

// ════════════════════════════════════════════════════════════════════
//  PIN DEFINITIONS
// ════════════════════════════════════════════════════════════════════

// ── Analog sensors ───────────────────────────────────────────────────────────
#define VOLTAGE_SENSOR    A15
#define TEMP_SENSOR_1     A14
#define HEAD_TEMP_SENSOR  A13
#define LDR_AO            A10
#define SOUND_AO          A12
#define GAS_AO            A0    // MQ2 smoke sensor -- analog output only
#define GESTURE_INT       -1

// ── RGBW interior lighting (all PWM-capable) ─────────────────────────────────
#define LED_R_PIN         4
#define LED_G_PIN         5
#define LED_B_PIN         7
#define LED_W_PIN         6

// ── Digital outputs ──────────────────────────────────────────────────────────
#define FAN_BODY_PIN      45    // moved from 11 (Timer5 PWM -- safe with DHT on 44)
#define FAN_HEAD_BLOW_PIN 46    // moved from 12 (Timer5 PWM)
#define FAN_HEAD_EXT_PIN  37
#define UV_LIGHT_PIN      2
#define BUZZER_PIN        22

// ── Digital inputs ───────────────────────────────────────────────────────────
#define MOMENTARY_BTN     8
#define UNHINGED_SW       40
#define TILT_SENSOR       3
#define PIR_PIN           23    // moved from 10
#define DHT_PIN           44
#define GAS_DO            -1
#define CURRENT_SENSOR    A9
#define CHARGE_DETECT_PIN -1
#define TSOP_LEFT         41    // moved from 34
#define TSOP_CENTRE       36
#define TSOP_RIGHT        38
#define HALL_DOCK         -1
#define RELAY_MOTORS      -1

// ── IR obstacle sensors (LOW = obstacle detected) ────────────────────────────
#define REAR_IR   25
#define FRONT_IR  A4
#define LEFT_IR   27    // moved from 30
#define RIGHT_IR  26    // moved from 29

// ── Ultrasonic sensors ───────────────────────────────────────────────────────
#define FRONT_TRIG  47
#define FRONT_ECHO  49
#define LEFT_TRIG   48
#define LEFT_ECHO   50
#define RIGHT_TRIG  42    // moved from 37 (bug fix -- was double-assigned with FAN_HEAD_EXT_PIN)
#define RIGHT_ECHO  39
#define REAR_TRIG   53
#define REAR_ECHO   51

// ── KS0509 integrated TB6612 motor drivers (hardwired on PCB) ────────────────
#define MTR_A_IN1  35   // Front Left  direction A
#define MTR_A_IN2  34   // Front Left  direction B
#define MTR_A_PWM  12   // Front Left  speed
#define MTR_B_IN1  33   // Rear  Left  direction A
#define MTR_B_IN2  32   // Rear  Left  direction B
#define MTR_B_PWM  11   // Rear  Left  speed
#define MTR_C_IN1  29   // Front Right direction A
#define MTR_C_IN2  28   // Front Right direction B
#define MTR_C_PWM   9   // Front Right speed
#define MTR_D_IN1  31   // Rear  Right direction A
#define MTR_D_IN2  30   // Rear  Right direction B
#define MTR_D_PWM  10   // Rear  Right speed

#define MOTOR_SLOW   130
#define MOTOR_NORMAL 200
#define MOTOR_FAST   240

// ════════════════════════════════════════════════════════════════════
//  OBJECTS
// ════════════════════════════════════════════════════════════════════
DHT         dht(DHT_PIN, DHT11);
TinyGPSPlus gps;

// ════════════════════════════════════════════════════════════════════
//  SENSOR TOGGLE TABLE
// ════════════════════════════════════════════════════════════════════
struct SensorFlags {
    bool dht     = true;
    bool light   = true;
    bool sound   = true;
    bool gas     = true;
    bool pir     = false;
    bool tilt    = true;
    bool ir      = true;
    bool us      = true;
    bool current = true;
    bool gps     = true;
} sens;

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

void applyToggle(const String& cmd) {
  int c1 = cmd.indexOf(':');
  int c2 = cmd.indexOf(':', c1 + 1);
  if (c1 < 0 || c2 < 0) return;
  String id  = cmd.substring(c1 + 1, c2);
  String val = cmd.substring(c2 + 1);
  id.toUpperCase(); val.toUpperCase();
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
}

// ════════════════════════════════════════════════════════════════════
//  GLOBAL STATE
// ════════════════════════════════════════════════════════════════════
bool lc = false;
bool rc = false;

float battVolt    = 8.4f;
float battPct     = 100.0f;
float battTemp    = 25.0f;
float ambTemp     = 25.0f;
float headTemp    = 25.0f;
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
bool  tiltDetected = false;
bool  pirDetected  = false;

enum BatTier { BAT_TIER_OK, BAT_TIER_WARN, BAT_TIER_LOW, BAT_TIER_CRITICAL };
BatTier lastBatTier = BAT_TIER_OK;
unsigned long lastCurrentCalc = 0;

bool fanBodyAuto = true;  bool fanHeadAuto = true;
bool fanBodyOn   = false; bool fanHeadOn   = false;

bool uvManualOn = false; bool uvAuto = false; bool uvActive = false;

enum LedMode { LED_OFF, LED_POLICE, LED_ALERT, LED_RAINBOW, LED_BREATHE, LED_PARTY, LED_SOLID };
LedMode ledMode = LED_OFF;
uint8_t ledR=0, ledG=0, ledB=0;
uint8_t ledBright = 255;
enum WhiteMode { WHITE_M_OFF, WHITE_M_ON, WHITE_M_AUTO };
WhiteMode whiteMode = WHITE_M_OFF;
unsigned long ledTimer = 0;
uint16_t ledHue = 0;
bool ledPhase = false;

float magHeading=-1.0f; bool magOk=false; uint8_t magChip=0;
unsigned long lastHDGTx=0, lastGPSTx=0;

enum DockState { DOCK_IDLE, DOCK_SEARCHING, DOCK_ALIGNING, DOCK_APPROACHING, DOCK_LOCKED, DOCK_COMPLETE, DOCK_FAILED };
DockState dockState = DOCK_IDLE;
bool selfChargeEnabled = false;
unsigned long dockTimer=0, searchTimer=0, dockSearchDir=0;
#define DOCK_TRIGGER_PCT 20
#define DOCK_FULL_PCT    95
#define DOCK_TIMEOUT_MS  20000
#define DOCK_APPROACH_MS 4000

bool systemReady    = false;
bool emergencyStop  = false;
bool autonomousMode = false;
bool gestureMode    = false;
bool unhingedMode   = false;
bool fanAuto        = true;
bool debugVerbose   = DEBUG_VERBOSE;
int  estopRetries   = 0;
const int MAX_ESTOP = 3;
unsigned long estopT = 0;

unsigned long picoLastPingMs = 0;
uint8_t       picoPingSeq    = 0;
bool          picoLinked     = false;

bool          s9Connected = false;
String        s9Buffer    = "";
unsigned long s9LastHB    = 0;
unsigned long s9LastSent  = 0;
const unsigned long S9_TIMEOUT = 6000;

unsigned long lastSense      = 0;
unsigned long lastTelem      = 0;
unsigned long lastNavDec     = 0;
unsigned long lastAvoid      = 0;
unsigned long lastSenseTs    = 0;
unsigned long bootStartTime  = 0;
const unsigned long BOOT_LOCK_TIME = 5000;
unsigned long uptimeSec      = 0;

bool          lastTiltDetected = false;
bool          btnPressed = false;
unsigned long lastBtn    = 0;
const unsigned long BTN_DEBOUNCE = 200;

String lastFace   = "";
bool   isCharging  = false;
bool   wasCharging = false;
String picoBuf     = "";

// ── MQ2 smoke alarm state ─────────────────────────────────────────────────────
bool          smokeAlarm       = false; // true while smoke is detected above threshold
unsigned long smokeAlarmBeepTs = 0;    // timestamp of last alarm beep (loop-level pulsing)
unsigned long gasClearSince    = 0;    // millis() when ADC first dropped below GAS_CLEAR_THRESHOLD
bool          gasSensorReady   = false;// false during MQ2 warmup period
unsigned long gasWarmupStart   = 0;   // set in setup() -- gates alarm enable

// ── Motor state ───────────────────────────────────────────────────────────────
uint8_t motorSpeedSlot = MOTOR_NORMAL;
enum MotorDir { DIR_STOP, DIR_FORWARD, DIR_BACKWARD, DIR_LEFT, DIR_RIGHT };
MotorDir currentMotorDir = DIR_STOP;

// Dance state machine
bool          danceActive = false;
uint8_t       danceStep   = 0;
unsigned long danceTimer  = 0;

// Defense state machine
bool          defenseActive = false;
uint8_t       defenseStep   = 0;
unsigned long defenseTimer  = 0;

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

void picoDbg(const char* msg)    { Serial1.print(F("DBG:")); Serial1.println(msg); }
void picoDbg(const String& msg)  { Serial1.print(F("DBG:")); Serial1.println(msg); }

uint8_t calcCRC(const String& s) {
  uint8_t c = 0;
  for (uint16_t i = 0; i < s.length(); i++) c ^= (uint8_t)s[i];
  return c;
}

void toPico(const String& msg) {
  char hex[3];
  sprintf(hex, "%02X", calcCRC(msg));
  Serial1.print(msg); Serial1.print(F("|CRC:")); Serial1.println(hex);
}

long getDist(int trig, int echo) {
  if (!sens.us) return -1;
  digitalWrite(trig, LOW);  delayMicroseconds(2);
  digitalWrite(trig, HIGH); delayMicroseconds(10);
  digitalWrite(trig, LOW);
  long dur = pulseIn(echo, HIGH, 10000);
  return dur > 0 ? dur / 58 : -1;
}

float readThermistor(int pin) {
  int raw = analogRead(pin);
  if (raw <= 0) return 25.0f;
  float v = (raw / 1023.0f) * 5.0f;
  float r = (5.0f - v) / v * 10000.0f;
  float s = logf(r / 10000.0f) / 3950.0f + 1.0f / 298.15f;
  float c = (1.0f / s) - 273.15f;
  return (c < -50 || c > 120) ? 25.0f : c;
}

float readHeadTemp() { return readThermistor(HEAD_TEMP_SENSOR); }

// ════════════════════════════════════════════════════════════════════
//  TB6612 DIRECT MOTOR CONTROL
// ════════════════════════════════════════════════════════════════════
//
//  Left side  = Motors A (front) + B (rear)
//  Right side = Motors C (front) + D (rear)
//  Right motors are physically mirrored -- fwd=false drives them FORWARD.
//  Direction table:
//    fwd=true  -> IN1=HIGH IN2=LOW  -> left motors forward,  right motors backward
//    fwd=false -> IN1=LOW  IN2=HIGH -> left motors backward, right motors forward
//
//  Net result:
//    motorsForward()  : left fwd=true,  right fwd=false -> robot goes forward
//    motorsBackward() : left fwd=false, right fwd=true  -> robot goes backward
//    motorsLeft()     : all  fwd=false  -> left back,  right fwd  -> spin left
//    motorsRight()    : all  fwd=true   -> left fwd,   right back -> spin right
//
void motorSet(int in1, int in2, int pwm, uint8_t speed, bool fwd) {
  digitalWrite(in1, fwd ? HIGH : LOW);
  digitalWrite(in2, fwd ? LOW  : HIGH);
  analogWrite(pwm, speed);
}

void motorCoast(int in1, int in2, int pwm) {
  digitalWrite(in1, LOW); digitalWrite(in2, LOW); analogWrite(pwm, 0);
}

void motorsForward() {
  motorSet(MTR_A_IN1, MTR_A_IN2, MTR_A_PWM, motorSpeedSlot, true);
  motorSet(MTR_B_IN1, MTR_B_IN2, MTR_B_PWM, motorSpeedSlot, true);
  motorSet(MTR_C_IN1, MTR_C_IN2, MTR_C_PWM, motorSpeedSlot, false);
  motorSet(MTR_D_IN1, MTR_D_IN2, MTR_D_PWM, motorSpeedSlot, false);
}

void motorsBackward() {
  motorSet(MTR_A_IN1, MTR_A_IN2, MTR_A_PWM, motorSpeedSlot, false);
  motorSet(MTR_B_IN1, MTR_B_IN2, MTR_B_PWM, motorSpeedSlot, false);
  motorSet(MTR_C_IN1, MTR_C_IN2, MTR_C_PWM, motorSpeedSlot, true);
  motorSet(MTR_D_IN1, MTR_D_IN2, MTR_D_PWM, motorSpeedSlot, true);
}

void motorsLeft() {
  motorSet(MTR_A_IN1, MTR_A_IN2, MTR_A_PWM, motorSpeedSlot, false);
  motorSet(MTR_B_IN1, MTR_B_IN2, MTR_B_PWM, motorSpeedSlot, false);
  motorSet(MTR_C_IN1, MTR_C_IN2, MTR_C_PWM, motorSpeedSlot, false);
  motorSet(MTR_D_IN1, MTR_D_IN2, MTR_D_PWM, motorSpeedSlot, false);
}

void motorsRight() {
  motorSet(MTR_A_IN1, MTR_A_IN2, MTR_A_PWM, motorSpeedSlot, true);
  motorSet(MTR_B_IN1, MTR_B_IN2, MTR_B_PWM, motorSpeedSlot, true);
  motorSet(MTR_C_IN1, MTR_C_IN2, MTR_C_PWM, motorSpeedSlot, true);
  motorSet(MTR_D_IN1, MTR_D_IN2, MTR_D_PWM, motorSpeedSlot, true);
}

void motorsStop() {
  motorCoast(MTR_A_IN1, MTR_A_IN2, MTR_A_PWM);
  motorCoast(MTR_B_IN1, MTR_B_IN2, MTR_B_PWM);
  motorCoast(MTR_C_IN1, MTR_C_IN2, MTR_C_PWM);
  motorCoast(MTR_D_IN1, MTR_D_IN2, MTR_D_PWM);
}

void applyMotorSpeed() {
  switch (currentMotorDir) {
    case DIR_FORWARD:  motorsForward();  break;
    case DIR_BACKWARD: motorsBackward(); break;
    case DIR_LEFT:     motorsLeft();     break;
    case DIR_RIGHT:    motorsRight();    break;
    default: break;
  }
}

// ════════════════════════════════════════════════════════════════════
//  MOTOR COMMANDS  (drop-in replacement for V33 sendMotor + queue)
// ════════════════════════════════════════════════════════════════════
void sendMotor(const char* cmd) {
  picoDbg(String(F("MTR>")) + cmd);
  if (strcmp(cmd, "STOP") == 0) {
    motorsStop(); nav.isMoving=false; nav.isReversing=false; currentMotorDir=DIR_STOP; return;
  }
  if      (strcmp(cmd, "FORWARD")  == 0) { currentMotorDir=DIR_FORWARD;  nav.isMoving=true;  nav.isReversing=false; motorsForward();  }
  else if (strcmp(cmd, "BACKWARD") == 0) { currentMotorDir=DIR_BACKWARD; nav.isMoving=true;  nav.isReversing=true;  motorsBackward(); }
  else if (strcmp(cmd, "LEFT")     == 0) { currentMotorDir=DIR_LEFT;     nav.isMoving=true;  nav.isReversing=false; motorsLeft();     }
  else if (strcmp(cmd, "RIGHT")    == 0) { currentMotorDir=DIR_RIGHT;    nav.isMoving=true;  nav.isReversing=false; motorsRight();    }
  else if (strcmp(cmd, "SLOW")     == 0) { motorSpeedSlot=MOTOR_SLOW;   applyMotorSpeed(); }
  else if (strcmp(cmd, "NORMAL")   == 0) { motorSpeedSlot=MOTOR_NORMAL; applyMotorSpeed(); }
  else if (strcmp(cmd, "FAST")     == 0) { motorSpeedSlot=MOTOR_FAST;   applyMotorSpeed(); }
}

// ════════════════════════════════════════════════════════════════════
//  DANCE STATE MACHINE  (non-blocking)
// ════════════════════════════════════════════════════════════════════
void startDance() {
  if (danceActive || defenseActive) return;
  danceActive=true; danceStep=0; danceTimer=millis();
  motorSpeedSlot=MOTOR_FAST; motorsLeft();
}

void runDance() {
  if (!danceActive) return;
  unsigned long now = millis();
  switch (danceStep) {
    case 0: if(now-danceTimer>400){danceStep=1;danceTimer=now;motorsRight();   } break;
    case 1: if(now-danceTimer>400){danceStep=2;danceTimer=now;motorsLeft();    } break;
    case 2: if(now-danceTimer>400){danceStep=3;danceTimer=now;motorsRight();   } break;
    case 3: if(now-danceTimer>400){danceStep=4;danceTimer=now;motorsForward(); } break;
    case 4: if(now-danceTimer>300){danceStep=5;danceTimer=now;motorsBackward();} break;
    case 5: if(now-danceTimer>300){
      danceActive=false; motorSpeedSlot=MOTOR_NORMAL; motorsStop();
      nav.isMoving=false; currentMotorDir=DIR_STOP;
      toS9("ACK|DANCE:DONE|END"); Serial1.println(F("ACK:DANCE:DONE"));
    } break;
  }
}

// ════════════════════════════════════════════════════════════════════
//  DEFENSE STATE MACHINE  (non-blocking spin-and-face)
// ════════════════════════════════════════════════════════════════════
void startDefense() {
  if (defenseActive || danceActive) return;
  defenseActive=true; defenseStep=0; defenseTimer=millis();
  motorSpeedSlot=MOTOR_FAST; motorsLeft();
}

void runDefense() {
  if (!defenseActive) return;
  unsigned long now = millis();
  switch (defenseStep) {
    case 0: if(now-defenseTimer>600){defenseStep=1;defenseTimer=now;motorsRight();} break;
    case 1: if(now-defenseTimer>600){defenseStep=2;defenseTimer=now;motorsLeft(); } break;
    case 2: if(now-defenseTimer>600){
      defenseActive=false; motorSpeedSlot=MOTOR_NORMAL; motorsStop();
      nav.isMoving=false; currentMotorDir=DIR_STOP;
      toS9("ACK|DEFENSE:DONE|END"); Serial1.println(F("ACK:DEFENSE:DONE"));
    } break;
  }
}

// ════════════════════════════════════════════════════════════════════
//  SENSOR READING
// ════════════════════════════════════════════════════════════════════
void readAllSensors() {
  if (sens.dht) {
    float t = dht.readTemperature(); float h = dht.readHumidity();
    if (!isnan(t)) ambTemp  = t;
    if (!isnan(h)) humidity = h;
  }
  lightLevel = sens.light ? analogRead(LDR_AO) : -1;
  soundLevel = (SOUND_AO >= 0 && sens.sound) ? analogRead(SOUND_AO) : -1;
  gasLevel   = (GAS_AO   >= 0 && sens.gas)   ? analogRead(GAS_AO)   :  0;

  int rawV = analogRead(VOLTAGE_SENSOR);
  battVolt = (rawV / 1023.0f) * 5.0f * BAT_VDIV;
  battTemp = readThermistor(TEMP_SENSOR_1);
  battPct  = constrain(((battVolt - BAT_MIN) / (BAT_MAX - BAT_MIN)) * 100.0f, 0.0f, 100.0f);
  headTemp = readHeadTemp();

  if (sens.us) {
    dFront = getDist(FRONT_TRIG, FRONT_ECHO);
    dLeft  = getDist(LEFT_TRIG,  LEFT_ECHO);
    dRight = getDist(RIGHT_TRIG, RIGHT_ECHO);
    dRear  = getDist(REAR_TRIG,  REAR_ECHO);
    lc = (dLeft  > SIDE_MIN && dLeft  != -1);
    rc = (dRight > SIDE_MIN && dRight != -1);
  } else { dFront = dLeft = dRight = dRear = -1; }

  irFront = sens.ir ? (digitalRead(FRONT_IR) == LOW) : false;
  irRear  = sens.ir ? (digitalRead(REAR_IR)  == LOW) : false;
  irLeft  = (LEFT_IR  >= 0 && sens.ir) ? (digitalRead(LEFT_IR)  == LOW) : false;
  irRight = (RIGHT_IR >= 0 && sens.ir) ? (digitalRead(RIGHT_IR) == LOW) : false;
  tiltDetected = sens.tilt ? (digitalRead(TILT_SENSOR) == HIGH) : false;
  pirDetected  = (PIR_PIN >= 0 && sens.pir) ? (digitalRead(PIR_PIN) == HIGH) : false;
  lastSenseTs  = millis();
}

void updatePower() {
  if (!sens.current || CURRENT_SENSOR < 0) { currentAmps = 0.0f; return; }
  unsigned long now = millis();
  if (now - lastCurrentCalc < 250) return;
  lastCurrentCalc = now;
  int raw     = analogRead(CURRENT_SENSOR);
  float volts = ((raw - CURRENT_ZERO_OFFSET) / 1023.0f) * CURRENT_VREF;
  float amps  = volts / CURRENT_SENSITIVITY;
  if (amps < 0) amps = -amps;
  currentAmps = (currentAmps * 0.7f) + (amps * 0.3f);
}

// ════════════════════════════════════════════════════════════════════
//  FAN CONTROL
// ════════════════════════════════════════════════════════════════════
void updateFans() {
  bool headHot   = (headTemp >= HEAD_FAN_TEMP);
  bool headState = fanHeadAuto ? headHot : fanHeadOn;
  digitalWrite(FAN_HEAD_BLOW_PIN, headState ? HIGH : LOW);
  digitalWrite(FAN_HEAD_EXT_PIN,  headState ? HIGH : LOW);
  bool bodyHot   = (battTemp > BAT_WTEMP || ambTemp > 35.0f);
  bool bodyState = fanBodyAuto ? bodyHot : fanBodyOn;
  if (battTemp > BAT_CTEMP) bodyState = true;
  digitalWrite(FAN_BODY_PIN, bodyState ? HIGH : LOW);
}

// ════════════════════════════════════════════════════════════════════
//  UV LIGHT STRIP CONTROL
// ════════════════════════════════════════════════════════════════════
void updateUV() {
  bool pirSafe = !(PIR_PIN >= 0 && pirDetected);
  bool newState = uvAuto ? (lightLevel >= 0 && lightLevel < 300 && pirSafe) : (uvManualOn && pirSafe);
  if (newState != uvActive) {
    uvActive = newState;
    digitalWrite(UV_LIGHT_PIN, uvActive ? HIGH : LOW);
    String reason = uvActive ? "ACTIVE" : (pirSafe ? "OFF" : "BLOCKED_PIR");
    toS9("UV:" + reason);
    Serial1.println("UV:" + reason);
  }
}

// ════════════════════════════════════════════════════════════════════
//  TELEMETRY -- Pico (Serial1)
// ════════════════════════════════════════════════════════════════════
void sendTelemetryToPico() {
  int haz=(emergencyStop||tiltDetected)?1:0;
  String t=F("STAT:");
  t+=String(gasLevel);t+=':';t+=String(ambTemp,1);t+=':';t+=String(humidity,1);t+=':';
  t+=String(haz);t+=':';t+=(pirDetected?"1":"0");t+=':';t+=(tiltDetected?"1":"0");t+=':';
  t+=((irFront||irRear||irLeft||irRight)?"1":"0");t+=':';
  t+=String(battVolt,2);t+=':';t+=String((int)battPct);t+=':';t+=String(currentAmps,2);
  Serial1.println(t);
  String u=F("US:");u+=String(dFront);u+=',';u+=String(dRear);u+=',';u+=String(dLeft);u+=',';u+=String(dRight);
  Serial1.println(u);toS9(u);
  String ir=F("IR:");ir+=(irFront?"1":"0");ir+=',';ir+=(irRear?"1":"0");
  Serial1.println(ir);
  String s=F("STATUS|ESTOP:");s+=(emergencyStop?"YES":"NO");
  s+=F("|AUTO:");s+=(autonomousMode?"ON":"OFF");
  s+=F("|BAT:");s+=String(battVolt,2);s+=F("|PCT:");s+=String((int)battPct);
  s+=F("|HTEMP:");s+=String(headTemp,1);
  s+=F("|FANS:HB:");s+=(digitalRead(FAN_HEAD_BLOW_PIN)?"1":"0");
  s+=F(",HE:");s+=(digitalRead(FAN_HEAD_EXT_PIN)?"1":"0");
  s+=F(",BD:");s+=(digitalRead(FAN_BODY_PIN)?"1":"0");
  s+=F("|UV:");s+=(uvActive?"ON":"OFF");
  s+=F("|S9:");s+=(s9Connected?F("OK"):F("WAIT"));
  s+=F("|FW:");s+=FW_VERSION;
  s+=F("|CHG:");s+=(isCharging?F("YES"):F("NO"));
  s+=F("|ADOCK:");s+=(selfChargeEnabled?F("ON"):F("OFF"));
  s+=F("|DOCKST:");
  switch(dockState){
    case DOCK_IDLE:        s+=F("IDLE");    break;
    case DOCK_SEARCHING:   s+=F("SEARCH");  break;
    case DOCK_ALIGNING:    s+=F("ALIGN");   break;
    case DOCK_APPROACHING: s+=F("APPROACH");break;
    case DOCK_LOCKED:      s+=F("LOCKED");  break;
    case DOCK_COMPLETE:    s+=F("DONE");    break;
    case DOCK_FAILED:      s+=F("FAIL");    break;
  }
  Serial1.println(s);
}

// ════════════════════════════════════════════════════════════════════
//  BATTERY WARNINGS
// ════════════════════════════════════════════════════════════════════
void checkBatteryTiers() {
  BatTier tier;
  if      (battVolt<=BAT_MIN)  tier=BAT_TIER_CRITICAL;
  else if (battVolt<=BAT_LOW)  tier=BAT_TIER_LOW;
  else if (battVolt<=BAT_WARN) tier=BAT_TIER_WARN;
  else                         tier=BAT_TIER_OK;
  if (tier==lastBatTier) return;
  lastBatTier=tier;
  switch (tier) {
    case BAT_TIER_WARN:     toS9("EVENT:BATTERY_WARN");     Serial1.println(F("BAT:WARN"));     beep(1200,200); break;
    case BAT_TIER_LOW:      toS9("EVENT:BATTERY_LOW");      Serial1.println(F("BAT:LOW"));      sendMotor("SLOW"); beep(1800,300); break;
    case BAT_TIER_CRITICAL: emergencyStop=true; sendMotor("STOP"); toS9("EVENT:BATTERY_CRITICAL"); Serial1.println(F("BAT:CRITICAL")); beep(2500,1000); break;
    default: break;
  }
}

// ════════════════════════════════════════════════════════════════════
//  SAFETY
// ════════════════════════════════════════════════════════════════════
void checkSafety() {
  checkBatteryTiers();
  if (battTemp>BAT_CTEMP&&!emergencyStop) {
    emergencyStop=true; sendMotor("STOP");
    digitalWrite(FAN_BODY_PIN,HIGH);digitalWrite(FAN_HEAD_BLOW_PIN,HIGH);digitalWrite(FAN_HEAD_EXT_PIN,HIGH);
    toS9("EVENT:OVERTEMP");Serial1.println(F("SAFETY:OVERTEMP"));beep(2500,1000);
  }
  if (tiltDetected&&sens.tilt&&!lastTiltDetected) {
    sendMotor("STOP");toS9("EVENT:TILT");Serial1.println(F("SAFETY:TILT"));beep(2000,300);
  }
  lastTiltDetected=(tiltDetected&&sens.tilt);
  // Gas alert handled by checkSmokeAlarm() below
}

// ════════════════════════════════════════════════════════════════════
//  MQ2 SMOKE / ELECTRICAL FIRE DETECTION
// ════════════════════════════════════════════════════════════════════
//
//  Analog-only (A0). No digital threshold pin used.
//  30 s warmup -> detect -> ESTOP + triple blast -> pulse beep every 600 ms
//  -> 10 s of clean air -> auto-clear all-clear tone.
//  SMOKE_CLEAR command overrides manually (denied while gas still high).
//
void checkSmokeAlarm() {
  if (!sens.gas || GAS_AO < 0) return;

  // Warmup gate -- MQ2 heater needs ~30 s before readings are valid
  if (!gasSensorReady) {
    if (millis() - gasWarmupStart < GAS_WARMUP_MS) return;
    gasSensorReady = true;
    toS9(F("EVENT:GAS_SENSOR_READY|END"));
    Serial1.println(F("GAS:SENSOR_READY"));
  }

  // New smoke detection
  if (gasLevel > GAS_SMOKE_THRESHOLD && !smokeAlarm) {
    smokeAlarm     = true;
    emergencyStop  = true;
    autonomousMode = false;
    danceActive    = false;
    defenseActive  = false;
    motorsStop();
    nav.isMoving    = false;
    nav.isReversing = false;
    currentMotorDir = DIR_STOP;
    gasClearSince   = 0;
    String msg = F("EVENT:SMOKE_DETECTED|GAS:");
    msg += String(gasLevel);
    msg += F("|END");
    toS9(msg);
    Serial1.println(F("SAFETY:SMOKE_ALARM"));
    beep(3500,220); delay(110); beep(3500,220); delay(110); beep(3500,440);
  }

  // Auto-clear when smoke dissipates
  if (smokeAlarm) {
    if (gasLevel < GAS_CLEAR_THRESHOLD) {
      if (gasClearSince == 0) gasClearSince = millis();
      else if (millis() - gasClearSince >= GAS_CLEAR_HOLD_MS) {
        smokeAlarm    = false;
        emergencyStop = false;
        estopRetries  = 0;
        estopT        = 0;
        gasClearSince = 0;
        toS9(F("EVENT:SMOKE_CLEARED|AUTO|END"));
        Serial1.println(F("SAFETY:SMOKE_CLEARED"));
        beep(1000,120); delay(100); beep(1300,120); delay(100); beep(1600,250);
      }
    } else {
      gasClearSince = 0;
    }
  }
}

// ════════════════════════════════════════════════════════════════════
//  ESTOP AUTO-RECOVERY
// ════════════════════════════════════════════════════════════════════
void handleEstopRecovery() {
  if (estopT==0) { estopT=millis(); motorsStop(); nav.isMoving=false; return; }
  if (millis()-estopT>5000) {
    estopRetries++;
    if (estopRetries<MAX_ESTOP) {
      if (battVolt>BAT_MIN&&battTemp<BAT_CTEMP) { emergencyStop=false;estopT=0;toS9("ESTOP|CLEARED|Auto-restart");beep(1500,200); }
      else { estopT=millis(); }
    } else { toS9("ESTOP|MANUAL_REQUIRED|Max retries reached");beep(2000,1000);estopT=millis(); }
  }
}

// ════════════════════════════════════════════════════════════════════
//  S9 COMMUNICATION
// ════════════════════════════════════════════════════════════════════
void sendStatusToS9() {
  String s=F("TELE:");
  s+=String(battVolt,2);s+=',';s+=String((int)battPct);s+=',';s+=(nav.isMoving?"1":"0");
  toS9(s);
}

void handleS9Communication() {
  while (Serial.available()) {
    char c=Serial.read();
    if (c=='\n') { processS9Command(s9Buffer); s9Buffer=""; }
    else if (c!='\r') { s9Buffer+=c; if(s9Buffer.length()>80) s9Buffer=""; }
  }
}

void processS9Command(String cmd) {
  cmd.trim();
  if (cmd.startsWith("CMD:")) cmd=cmd.substring(4);
  cmd.trim();
  if (cmd.length()==0) return;
  picoDbg("S9>"+cmd);
  s9Connected=true; s9LastHB=millis();

  if (cmd=="MOTOR:F")     { sendMotor("FORWARD");  toS9("ACK|MOTOR:F|END"); return; }
  if (cmd=="MOTOR:B")     { sendMotor("BACKWARD"); toS9("ACK|MOTOR:B|END"); return; }
  if (cmd=="MOTOR:L")     { sendMotor("LEFT");     toS9("ACK|MOTOR:L|END"); return; }
  if (cmd=="MOTOR:R")     { sendMotor("RIGHT");    toS9("ACK|MOTOR:R|END"); return; }
  if (cmd=="MOTOR:S")     { sendMotor("STOP");     toS9("ACK|MOTOR:S|END"); return; }
  if (cmd=="MOTOR:DANCE") { startDance();   toS9("ACK|DANCE|END");   return; }
  if (cmd=="DEFENSE")     { startDefense(); toS9("ACK|DEFENSE|END"); return; }
  if (cmd.startsWith("SPEED:")) {
    int spd=cmd.substring(6).toInt();
    if(spd>0&&spd<=255){motorSpeedSlot=(uint8_t)spd;applyMotorSpeed();}
    toS9("ACK|"+cmd+"|END"); return;
  }
  if (cmd=="AUTO:ON")     { autonomousMode=true;  toS9("ACK|AUTO_ON|END"); return; }
  if (cmd=="AUTO:OFF")    { autonomousMode=false; sendMotor("STOP"); toS9("ACK|AUTO_OFF|END"); return; }
  if (cmd=="GESTURE:ON")  { gestureMode=true;  toS9("ACK|GESTURE_ON|END");  return; }
  if (cmd=="GESTURE:OFF") { gestureMode=false; toS9("ACK|GESTURE_OFF|END"); return; }
  if (cmd=="EMERGENCY_STOP") { emergencyStop=true; sendMotor("STOP"); toS9("ACK|EMERGENCY_STOP|END"); beep(2000,500); return; }
  if (cmd=="ESTOP_CLEAR")    { emergencyStop=false; estopRetries=0; estopT=0; toS9("ACK|ESTOP_CLEARED|END"); return; }
  if (cmd=="FAN_HEAD:ON")    { fanHeadAuto=false;fanHeadOn=true;  updateFans();toS9("ACK|FAN_HEAD:ON|END");   return; }
  if (cmd=="FAN_HEAD:OFF")   { fanHeadAuto=false;fanHeadOn=false; updateFans();toS9("ACK|FAN_HEAD:OFF|END");  return; }
  if (cmd=="FAN_HEAD:AUTO")  { fanHeadAuto=true;                  updateFans();toS9("ACK|FAN_HEAD:AUTO|END"); return; }
  if (cmd=="FAN_BODY:ON")    { fanBodyAuto=false;fanBodyOn=true;  updateFans();toS9("ACK|FAN_BODY:ON|END");   return; }
  if (cmd=="FAN_BODY:OFF")   { fanBodyAuto=false;fanBodyOn=false; updateFans();toS9("ACK|FAN_BODY:OFF|END");  return; }
  if (cmd=="FAN_BODY:AUTO")  { fanBodyAuto=true;                  updateFans();toS9("ACK|FAN_BODY:AUTO|END"); return; }
  if (cmd=="FAN_ALL:ON")  { fanHeadAuto=false;fanHeadOn=true; fanBodyAuto=false;fanBodyOn=true;  updateFans();toS9("ACK|FAN_ALL:ON|END");  return; }
  if (cmd=="FAN_ALL:OFF") { fanHeadAuto=false;fanHeadOn=false;fanBodyAuto=false;fanBodyOn=false; updateFans();toS9("ACK|FAN_ALL:OFF|END"); return; }
  if (cmd=="UV:ON")   { uvManualOn=true; uvAuto=false; updateUV();toS9("ACK|UV:ON|END");   return; }
  if (cmd=="UV:OFF")  { uvManualOn=false;uvAuto=false; updateUV();toS9("ACK|UV:OFF|END");  return; }
  if (cmd=="UV:AUTO") { uvAuto=true;                   updateUV();toS9("ACK|UV:AUTO|END"); return; }
  if (cmd.startsWith("MODE:")) {
    String mode=cmd.substring(5); toS9("ACK|MODE:"+mode+"|END");
    Serial1.print(F("MODE:")); Serial1.println(mode); return;
  }
  if (cmd.startsWith("TOGGLE_SENSOR:")) { applyToggle(cmd); return; }
  if (cmd=="SENSOR_STATUS") { String ss=sensorStatusString(); toS9(ss); Serial1.println(ss); return; }
  if (cmd.startsWith("FACE:")) { lastFace=cmd.substring(5); return; }
  if (cmd.startsWith("OBJ:"))  { Serial1.println(cmd); return; }
  if (cmd.startsWith("SENS|")) { Serial1.println(cmd); return; }
  if (cmd=="DIAG"||cmd=="DIAG:RUN") {
    String d=F("DIAG|");
    d+="BAT:"+String(battVolt,2)+"V|PCT:"+String((int)battPct)+"%|";
    d+="TEMP:"+String(ambTemp,1)+"C|HTEMP:"+String(headTemp,1)+"C|";
    d+="F:"+String(dFront)+"cm|R:"+String(dRear)+"cm|L:"+String(dLeft)+"cm|Ri:"+String(dRight)+"cm|";
    d+="UV:"+String(uvActive?"ON":"OFF")+"|";
    d+="GPS:"+String(gps_lat,4)+","+String(gps_lon,4)+"|SAT:"+String(gps_sats)+"|";
    d+="S9:"+String(s9Connected?"OK":"NO")+"|AUTO:"+String(autonomousMode?"ON":"OFF")+"|";
    d+="UPT:"+String(uptimeSec)+"s|END";
    toS9(d); toS9(sensorStatusString()); return;
  }
  if (cmd=="AUTODOCK:ON")  { selfChargeEnabled=true;  toS9("ACK|AUTODOCK:ON|END");  Serial1.println(F("AUTODOCK:ON"));  return; }
  if (cmd=="AUTODOCK:OFF") { selfChargeEnabled=false; cancelDocking(); toS9("ACK|AUTODOCK:OFF|END"); Serial1.println(F("AUTODOCK:OFF")); return; }
  if (cmd=="DOCK:START")   { triggerDocking(true);  return; }
  if (cmd=="DOCK:CANCEL")  { cancelDocking();        return; }
  if (cmd=="COB:ON")   { whiteMode=WHITE_M_ON;              updateLeds();toS9("ACK|COB:ON|END");   return; }
  if (cmd=="COB:OFF")  { whiteMode=WHITE_M_OFF;             updateLeds();toS9("ACK|COB:OFF|END");  return; }
  if (cmd=="COB:DIM")  { whiteMode=WHITE_M_ON;ledBright=128;updateLeds();toS9("ACK|COB:DIM|END");  return; }
  if (cmd=="COB:AUTO") { whiteMode=WHITE_M_AUTO;            updateLeds();toS9("ACK|COB:AUTO|END"); return; }
  if (cmd.startsWith("LED:"))  { setLedMode(cmd.substring(4)); return; }
  if (cmd=="DEBUG:ON")  { debugVerbose=true;  return; }
  if (cmd=="DEBUG:OFF") { debugVerbose=false; return; }
  if (cmd=="NOTIFY:PATROL_START") { toS9("ACK|PATROL_START|END"); return; }
  if (cmd=="KEEP_DISTANCE")       { toS9("ACK|KEEP_DISTANCE|END"); return; }
  if (cmd=="SMOKE_CLEAR") {
    if (!smokeAlarm) { toS9(F("ACK|SMOKE_CLEAR:NO_ALARM|END")); return; }
    if (gasLevel >= GAS_CLEAR_THRESHOLD) { toS9("EVENT:SMOKE_CLEAR_DENIED|GAS:"+String(gasLevel)+"_STILL_HIGH|END"); return; }
    smokeAlarm=false;emergencyStop=false;estopRetries=0;estopT=0;gasClearSince=0;
    toS9(F("EVENT:SMOKE_ALARM_CLEARED|MANUAL|END"));
    Serial1.println(F("SAFETY:SMOKE_CLEARED"));
    beep(1000,120);delay(100);beep(1600,200);
    return;
  }
  if (cmd.startsWith("WIFI|"))    { Serial1.println(cmd); toS9("ACK|WIFI_CONNECTING|END"); return; }
}

// ════════════════════════════════════════════════════════════════════
//  PICO DASHBOARD COMMUNICATION  (Serial1 receive)
// ════════════════════════════════════════════════════════════════════
void processPicoCommand(String cmd) {
  if (debugVerbose) { Serial.print(F("[PICO] RX: ")); Serial.println(cmd); }
  picoLinked=true;
  if (cmd.startsWith("CMD:")) {
    String sub=cmd.substring(4); sub.toUpperCase();
    if      (sub=="F")      { autonomousMode=false; sendMotor("FORWARD");  return; }
    else if (sub=="B")      { autonomousMode=false; sendMotor("BACKWARD"); return; }
    else if (sub=="L")      { autonomousMode=false; sendMotor("LEFT");     return; }
    else if (sub=="R")      { autonomousMode=false; sendMotor("RIGHT");    return; }
    else if (sub=="S")      { autonomousMode=false; sendMotor("STOP");     return; }
    else if (sub=="AUTO")   { autonomousMode=!autonomousMode; if(!autonomousMode)sendMotor("STOP"); return; }
    else if (sub=="DANCE")  { startDance();   return; }
    else if (sub=="SLOW")   { sendMotor("SLOW");   return; }
    else if (sub=="NORMAL") { sendMotor("NORMAL"); return; }
    else if (sub=="FAST")   { sendMotor("FAST");   return; }
    else if (sub=="ESTOP")  { emergencyStop=true; sendMotor("STOP"); return; }
    else if (sub=="CLEAR")  { emergencyStop=false; estopRetries=0; estopT=0; return; }
    else if (sub.startsWith("TOGGLE_SENSOR:")) { applyToggle(sub); return; }
    else if (sub.startsWith("LED:"))  { setLedMode(sub.substring(4)); return; }
    else if (sub.startsWith("COB:"))  { processS9Command(sub); return; }
    else if (sub=="UV:ON"||sub=="UV:OFF"||sub=="UV:AUTO") { processS9Command(sub); return; }
    return;
  }
  if (cmd.startsWith("PING_PICO:")) {
    picoLastPingMs=millis(); picoPingSeq=(uint8_t)cmd.substring(10).toInt();
    Serial1.print(F("PONG_PICO:")); Serial1.println(picoPingSeq);
    sendTelemetryToPico(); return;
  }
  if (cmd.startsWith("MODE:"))          { toS9("REQ_MODE:"+cmd.substring(5)); return; }
  if (cmd.startsWith("TOGGLE_SENSOR:")) { applyToggle(cmd); return; }
  if (cmd=="SENSOR_STATUS") { Serial1.println(sensorStatusString()); return; }
  if (cmd=="PING") {
    picoLastPingMs=millis();
    Serial1.print(F("PONG_PICO:")); Serial1.println(picoPingSeq++);
    sendTelemetryToPico(); return;
  }
  if (cmd=="STATUS") { sendTelemetryToPico(); return; }
  if (cmd=="ESTOP"||cmd=="EMERGENCY_STOP") {
    emergencyStop=true; sendMotor("STOP");
    if(cmd=="EMERGENCY_STOP"){toS9("ACK|EMERGENCY_STOP|END");beep(2000,500);}
    return;
  }
  if (cmd=="ESTOP_CLEAR") { emergencyStop=false;estopRetries=0;estopT=0;toS9("ACK|ESTOP_CLEARED|END"); return; }
  if (cmd=="PONG") { return; }
  if (cmd.startsWith("WIFI_IP:")) { toS9(cmd); return; }
  if (cmd=="AUTODOCK:ON")  { selfChargeEnabled=true;  Serial1.println(F("AUTODOCK:ON"));  return; }
  if (cmd=="AUTODOCK:OFF") { selfChargeEnabled=false; cancelDocking(); Serial1.println(F("AUTODOCK:OFF")); return; }
  if (cmd=="SMOKE_CLEAR") {
    if (!smokeAlarm) return;
    if (gasLevel >= GAS_CLEAR_THRESHOLD) { Serial1.println("GAS:CLEAR_DENIED:"+String(gasLevel)); return; }
    smokeAlarm=false;emergencyStop=false;estopRetries=0;estopT=0;gasClearSince=0;
    toS9(F("EVENT:SMOKE_ALARM_CLEARED|MANUAL|END"));
    Serial1.println(F("SAFETY:SMOKE_CLEARED"));
    beep(1000,120);delay(100);beep(1600,200);
    return;
  }
  if (cmd.startsWith("SPEED:")) {
    int spd=cmd.substring(6).toInt();
    if(spd>0&&spd<=255){motorSpeedSlot=(uint8_t)spd;applyMotorSpeed();}
    return;
  }
  if (debugVerbose) { Serial.print(F("[PICO] Unknown: ")); Serial.println(cmd); }
}

void handlePicoCommunication() {
  while (Serial1.available()) {
    char c=Serial1.read();
    if (c=='\n') {
      picoBuf.trim();
      int crcIdx=picoBuf.indexOf("|CRC:");
      if(crcIdx>0) picoBuf=picoBuf.substring(0,crcIdx);
      if(picoBuf.length()>0) processPicoCommand(picoBuf);
      picoBuf="";
    } else if (c!='\r') { picoBuf+=c; if(picoBuf.length()>80) picoBuf=""; }
  }
}

// ════════════════════════════════════════════════════════════════════
//  GPS  (Serial3 -- D14=TX3 / D15=RX3)
// ════════════════════════════════════════════════════════════════════
void handleGPS() {
  if (!sens.gps) return;
  while (Serial3.available()) gps.encode(Serial3.read());
  if (gps.location.isUpdated())   { gps_lat=gps.location.lat(); gps_lon=gps.location.lng(); }
  if (gps.satellites.isUpdated()) { gps_sats=gps.satellites.value(); }
}

// ════════════════════════════════════════════════════════════════════
//  PAJ7620 GESTURE SENSOR
// ════════════════════════════════════════════════════════════════════
void checkGestures() {
  if (GESTURE_INT<0||!gestureMode) return;
  uint8_t data=0; paj7620ReadReg(0x43,1,&data);
  if (data==0) return;
  const char* g=nullptr;
  if      (data==GES_UP_FLAG)        { g="UP";    sendMotor("FORWARD");  }
  else if (data==GES_DOWN_FLAG)      { g="DOWN";  sendMotor("BACKWARD"); }
  else if (data==GES_LEFT_FLAG)      { g="LEFT";  sendMotor("LEFT");     }
  else if (data==GES_RIGHT_FLAG)     { g="RIGHT"; sendMotor("RIGHT");    }
  else if (data==GES_FORWARD_FLAG)   { g="NEAR";  sendMotor("STOP");     }
  else if (data==GES_CLOCKWISE_FLAG) { g="CW";    startDance(); }
  if (g) { Serial1.print(F("GESTURE:")); Serial1.println(g); toS9("GESTURE:"+String(g)); }
}

// ════════════════════════════════════════════════════════════════════
//  MOMENTARY BUTTON
// ════════════════════════════════════════════════════════════════════
void handleButton() {
  if (digitalRead(MOMENTARY_BTN)==LOW) {
    unsigned long now=millis();
    if (!btnPressed&&(now-lastBtn>BTN_DEBOUNCE)) {
      btnPressed=true; lastBtn=now;
      autonomousMode=!autonomousMode;
      if (!autonomousMode) sendMotor("STOP");
      toS9(autonomousMode?"BTN:AUTO_ON":"BTN:AUTO_OFF");
      beep(autonomousMode?1500:1000,120); delay(60); beep(autonomousMode?1800:800,120);
    }
  } else { btnPressed=false; }
}

// ════════════════════════════════════════════════════════════════════
//  AUTONOMOUS NAVIGATION
// ════════════════════════════════════════════════════════════════════
void lookAndDecide() {
  if (lookState==LOOK_IDLE) { sendMotor("STOP"); navTimer=millis()+150; lookState=LOOK_STOP; return; }
  if (lookState==LOOK_STOP&&millis()>=navTimer) {
    bool leftClear=(dLeft>SIDE_MIN||dLeft<0); bool rightClear=(dRight>SIDE_MIN||dRight<0);
    if      (leftClear&&(dLeft>dRight||!rightClear)) { sendMotor("LEFT");     navTimer=millis()+T45; lookState=LOOK_LEFT; }
    else if (rightClear)                              { sendMotor("RIGHT");    navTimer=millis()+T45; lookState=LOOK_RIGHT; }
    else                                              { sendMotor("BACKWARD"); navTimer=millis()+800; lookState=LOOK_BACK_LEFT; }
    return;
  }
  if (lookState==LOOK_LEFT      &&millis()>=navTimer) { sendMotor("FORWARD"); nav.isMoving=true; lookState=LOOK_IDLE; return; }
  if (lookState==LOOK_RIGHT     &&millis()>=navTimer) { sendMotor("FORWARD"); nav.isMoving=true; lookState=LOOK_IDLE; return; }
  if (lookState==LOOK_BACK_LEFT &&millis()>=navTimer) { sendMotor("LEFT"); navTimer=millis()+T90; lookState=LOOK_FORWARD; return; }
  if (lookState==LOOK_FORWARD   &&millis()>=navTimer) { sendMotor("FORWARD"); nav.isMoving=true; lookState=LOOK_IDLE; return; }
}

void handleStuck() {
  if (stuckState==STUCK_IDLE) { sendMotor("STOP");     navTimer=millis()+150;  stuckState=STUCK_STOP; return; }
  if (stuckState==STUCK_STOP &&millis()>=navTimer) { sendMotor("BACKWARD"); navTimer=millis()+1200; stuckState=STUCK_BACK; return; }
  if (stuckState==STUCK_BACK &&millis()>=navTimer) { sendMotor("LEFT");     navTimer=millis()+T90;  stuckState=STUCK_LEFT; return; }
  if (stuckState==STUCK_LEFT &&millis()>=navTimer) {
    sendMotor("FORWARD"); nav.stuckDetected=false; nav.stuckStart=0; stuckState=STUCK_IDLE; return;
  }
}

void makeAutonomousDecision() {
  if (emergencyStop) { sendMotor("STOP"); return; }
  bool isTurning=(randomState==RANDOM_TURN||lookState!=LOOK_IDLE||avoidState==AVOID_TURN);
  if (nav.isMoving&&!nav.isAvoiding&&!isTurning) {
    if (abs(dFront-nav.lastFrontDist)<5) {
      if (nav.stuckStart==0) nav.stuckStart=millis();
      else if (millis()-nav.stuckStart>3000&&stuckState==STUCK_IDLE) { handleStuck(); return; }
    } else { nav.stuckStart=0; nav.stuckDetected=false; }
  }
  nav.lastFrontDist=dFront;
  if (nav.isAvoiding) return;
  if (!nav.isMoving) {
    if      (dFront>OBS_WARN)           sendMotor("FORWARD");
    else if (dFront<0)                  sendMotor("STOP");
    else if (lookState==LOOK_IDLE)      lookAndDecide();
  } else {
    if      (dFront>0&&dFront<OBS_SLOW) sendMotor("SLOW");
    else if (dFront>OBS_SLOW)           sendMotor("NORMAL");
  }
}

void handleRandomTurn() {
  if (randomState==RANDOM_IDLE&&nav.isMoving&&random(1000)>992) {
    (random(2)==0)?sendMotor("LEFT"):sendMotor("RIGHT");
    navTimer=millis()+180; randomState=RANDOM_TURN;
  }
  if (randomState==RANDOM_TURN&&millis()>=navTimer) { sendMotor("FORWARD"); randomState=RANDOM_IDLE; }
}

void collisionAvoidance() {
  if (!autonomousMode) { avoidState=AVOID_IDLE; nav.isAvoiding=false; return; }
  if (millis()-lastSenseTs>600) return;
  if (sens.us&&dFront>0&&dFront<OBS_STOP&&!nav.isAvoiding&&avoidState==AVOID_IDLE) {
    nav.isAvoiding=true;nav.avoidStart=millis();nav.avoidAttempts++;
    sendMotor("STOP");navTimer=millis()+100;avoidState=AVOID_STOP;return;
  }
  if (avoidState==AVOID_STOP  &&millis()>=navTimer) { sendMotor("BACKWARD");navTimer=millis()+700; avoidState=AVOID_BACK;  return; }
  if (avoidState==AVOID_BACK  &&millis()>=navTimer) { sendMotor("STOP");    navTimer=millis()+150; avoidState=AVOID_STOP2; return; }
  if (avoidState==AVOID_STOP2 &&millis()>=navTimer) {
    bool lc2=(dLeft<0||dLeft>SIDE_MIN);bool rc2=(dRight<0||dRight>SIDE_MIN);
    if      (lc2&&(!rc2||dLeft>dRight)) { sendMotor("LEFT");  navTimer=millis()+T90; }
    else if (rc2)                        { sendMotor("RIGHT"); navTimer=millis()+T90; }
    else                                 { sendMotor("LEFT");  navTimer=millis()+(T90*2); }
    avoidState=AVOID_TURN; return;
  }
  if (avoidState==AVOID_TURN&&millis()>=navTimer) {
    if(autonomousMode){sendMotor("FORWARD");nav.isMoving=true;}
    nav.isAvoiding=false;nav.lastAvoidEnd=millis();avoidState=AVOID_IDLE;return;
  }
  if (sens.ir&&irFront&&!nav.isAvoiding&&avoidState==AVOID_IDLE) {
    sendMotor("STOP");navTimer=millis()+100;avoidState=AVOID_STOP;return;
  }
  if (nav.isReversing&&sens.us&&dRear>0&&dRear<15) { sendMotor("STOP");nav.isReversing=false; }
  if (nav.avoidAttempts>5) { autonomousMode=false;nav.avoidAttempts=0;sendMotor("STOP");toS9("EVENT:NAVIGATION_FAILED"); }
  if (nav.avoidAttempts>0&&millis()-nav.lastAvoidEnd>10000) nav.avoidAttempts=0;
}

// ════════════════════════════════════════════════════════════════════
//  DOCK STATE MACHINE
// ════════════════════════════════════════════════════════════════════
bool tsopLeft()   { return digitalRead(TSOP_LEFT)  ==LOW; }
bool tsopCentre() { return digitalRead(TSOP_CENTRE)==LOW; }
bool tsopRight_f(){ return digitalRead(TSOP_RIGHT) ==LOW; }
bool dockLocked() { if(HALL_DOCK<0)return false; return digitalRead(HALL_DOCK)==LOW; }
void relayMotors(bool on){ if(RELAY_MOTORS>=0)digitalWrite(RELAY_MOTORS,on?HIGH:LOW); }

void cancelDocking(){
  dockState=DOCK_IDLE;relayMotors(false);
  motorSpeedSlot=MOTOR_NORMAL;sendMotor("STOP");
  toS9("DOCK:CANCELLED");Serial1.println(F("DOCK:CANCELLED"));
}

void triggerDocking(bool manual){
  if(dockState!=DOCK_IDLE)return;
  dockState=DOCK_SEARCHING;searchTimer=dockTimer=dockSearchDir=millis();
  toS9("DOCK:SEARCHING");Serial1.println(F("DOCK:SEARCHING"));
  sendMotor("STOP");delay(100);motorSpeedSlot=130;delay(30);sendMotor("RIGHT");
}

void runDockingStateMachine(){
  if(dockState==DOCK_IDLE)return;
  bool L=tsopLeft(),C=tsopCentre(),R=tsopRight_f();
  unsigned long now=millis();

  if(dockState==DOCK_SEARCHING){
    if(now-searchTimer>DOCK_TIMEOUT_MS){sendMotor("STOP");dockState=DOCK_FAILED;toS9("DOCK:FAILED:TIMEOUT");Serial1.println(F("DOCK:FAILED"));return;}
    if(now-dockSearchDir>6000){dockSearchDir=now;static bool dir=false;dir=!dir;sendMotor(dir?"LEFT":"RIGHT");}
    if(L||C||R){sendMotor("STOP");delay(100);dockState=DOCK_ALIGNING;dockTimer=now;motorSpeedSlot=130;toS9("DOCK:ALIGNING");Serial1.println(F("DOCK:ALIGNING"));}
    return;
  }
  if(dockState==DOCK_ALIGNING){
    if(!L&&!C&&!R){dockState=DOCK_SEARCHING;searchTimer=dockSearchDir=now;sendMotor("RIGHT");return;}
    if(now-dockTimer>10000){dockState=DOCK_SEARCHING;searchTimer=dockSearchDir=now;sendMotor("RIGHT");return;}
    if(!L&&C&&!R){sendMotor("STOP");delay(80);motorSpeedSlot=70;delay(30);sendMotor("FORWARD");dockState=DOCK_APPROACHING;dockTimer=now;toS9("DOCK:APPROACHING");Serial1.println(F("DOCK:APPROACHING"));return;}
    if(L&&C&&R) {motorSpeedSlot=70;delay(30);sendMotor("FORWARD");dockState=DOCK_APPROACHING;dockTimer=now;return;}
    if     (L&&!R){sendMotor("LEFT"); delay(120);sendMotor("STOP");delay(80);}
    else if(R&&!L){sendMotor("RIGHT");delay(120);sendMotor("STOP");delay(80);}
    else if(L&&C) {sendMotor("RIGHT");delay(60); sendMotor("STOP");delay(60);}
    else if(R&&C) {sendMotor("LEFT"); delay(60); sendMotor("STOP");delay(60);}
    return;
  }
  if(dockState==DOCK_APPROACHING){
    if(!C&&L){sendMotor("STOP");delay(40);sendMotor("LEFT"); delay(80);sendMotor("STOP");delay(40);motorSpeedSlot=70;sendMotor("FORWARD");}
    if(!C&&R){sendMotor("STOP");delay(40);sendMotor("RIGHT");delay(80);sendMotor("STOP");delay(40);motorSpeedSlot=70;sendMotor("FORWARD");}
    if(dockLocked()){sendMotor("STOP");delay(100);relayMotors(true);dockState=DOCK_LOCKED;dockTimer=now;toS9("DOCK:LOCKED:CHARGING");Serial1.println(F("DOCK:LOCKED"));beep(880,100);delay(80);beep(1100,100);delay(80);beep(1320,200);return;}
    if(now-dockTimer>DOCK_APPROACH_MS){sendMotor("STOP");motorSpeedSlot=130;delay(30);sendMotor("BACKWARD");delay(600);sendMotor("STOP");delay(100);dockState=DOCK_ALIGNING;dockTimer=now;}
    return;
  }
  if(dockState==DOCK_LOCKED){
    if(!dockLocked()&&!isCharging){relayMotors(false);delay(100);motorSpeedSlot=70;delay(20);sendMotor("FORWARD");dockState=DOCK_APPROACHING;dockTimer=now;toS9("DOCK:CONTACT_LOST");return;}
    if(battPct>=DOCK_FULL_PCT){dockState=DOCK_COMPLETE;toS9("DOCK:CHARGED");Serial1.println(F("DOCK:CHARGED"));beep(880,200);delay(80);beep(1100,200);delay(80);beep(1320,400);}
    return;
  }
  if(dockState==DOCK_COMPLETE){relayMotors(false);delay(200);motorSpeedSlot=130;delay(30);sendMotor("BACKWARD");delay(1200);sendMotor("STOP");motorSpeedSlot=MOTOR_NORMAL;dockState=DOCK_IDLE;toS9("DOCK:COMPLETE:RESUMING");Serial1.println(F("DOCK:COMPLETE"));return;}
  if(dockState==DOCK_FAILED){motorSpeedSlot=MOTOR_NORMAL;sendMotor("STOP");relayMotors(false);if(now-dockTimer>8000)dockState=DOCK_IDLE;return;}
}

void checkAutoDocktrigger(){
  if(dockState!=DOCK_IDLE||!selfChargeEnabled)return;
  if(battPct<=DOCK_TRIGGER_PCT&&autonomousMode)triggerDocking(false);
}

// ════════════════════════════════════════════════════════════════════
//  RGBW INTERIOR LIGHTING ENGINE  (R=4 G=5 B=7 W=6, all PWM)
// ════════════════════════════════════════════════════════════════════
#define LED_ACTIVE_HIGH 1

void writeChan(int pin, uint8_t v){
  if(pin<0)return;
  uint8_t out=(uint8_t)(((uint16_t)v*ledBright)/255);
#if LED_ACTIVE_HIGH
  analogWrite(pin,out);
#else
  analogWrite(pin,255-out);
#endif
}

void hsvToRgb(uint16_t h,uint8_t s,uint8_t v,uint8_t &r,uint8_t &g,uint8_t &b){
  uint8_t region=h/60;uint16_t rem=(h%60)*255/60;
  uint8_t p=(uint16_t)v*(255-s)/255;
  uint8_t q=(uint16_t)v*(255-((uint16_t)s*rem)/255)/255;
  uint8_t t=(uint16_t)v*(255-((uint16_t)s*(255-rem))/255)/255;
  switch(region%6){
    case 0:r=v;g=t;b=p;break; case 1:r=q;g=v;b=p;break;
    case 2:r=p;g=v;b=t;break; case 3:r=p;g=q;b=v;break;
    case 4:r=t;g=p;b=v;break; default:r=v;g=p;b=q;break;
  }
}

uint8_t whiteLevel(){
  switch(whiteMode){
    case WHITE_M_ON:   return 255;
    case WHITE_M_AUTO: return(sens.light&&lightLevel>=0&&lightLevel<300)?255:0;
    default:           return 0;
  }
}

void updateLeds(){
  unsigned long now=millis();
  uint8_t r=0,g=0,b=0;
  switch(ledMode){
    case LED_OFF:   r=g=b=0; break;
    case LED_SOLID: r=ledR;g=ledG;b=ledB; break;
    case LED_POLICE:
      if(now-ledTimer>120){ledTimer=now;ledPhase=!ledPhase;}
      if(ledPhase){r=255;g=0;b=0;}else{r=0;g=0;b=255;}
      break;
    case LED_ALERT:
      if(now-ledTimer>350){ledTimer=now;ledPhase=!ledPhase;}
      r=ledPhase?255:25;g=0;b=0;
      break;
    case LED_RAINBOW:
      if(now-ledTimer>20){ledTimer=now;ledHue=(ledHue+2)%360;}
      hsvToRgb(ledHue,255,255,r,g,b);
      break;
    case LED_BREATHE:{
      float ph=(sinf(now/700.0f)+1.0f)*0.5f;
      uint8_t lvl=(uint8_t)(ph*255);
      r=(uint16_t)ledR*lvl/255;g=(uint16_t)ledG*lvl/255;b=(uint16_t)ledB*lvl/255;
      break;}
    case LED_PARTY:
      if(now-ledTimer>180){ledTimer=now;hsvToRgb(random(360),255,255,ledR,ledG,ledB);}
      r=ledR;g=ledG;b=ledB;
      break;
  }
  writeChan(LED_R_PIN,r); writeChan(LED_G_PIN,g);
  writeChan(LED_B_PIN,b); writeChan(LED_W_PIN,whiteLevel());
}

void announceLed(){
  const char* m="OFF";
  switch(ledMode){
    case LED_POLICE: m="POLICE"; break; case LED_ALERT:   m="ALERT";   break;
    case LED_RAINBOW:m="RAINBOW";break; case LED_BREATHE: m="BREATHE"; break;
    case LED_PARTY:  m="PARTY";  break; case LED_SOLID:   m="SOLID";   break;
    default:         m="OFF";    break;
  }
  const char* w=(whiteMode==WHITE_M_ON)?"ON":(whiteMode==WHITE_M_AUTO)?"AUTO":"OFF";
  String msg=String("LED|MODE:")+m+"|WHITE:"+w+"|BR:"+String(ledBright)+"|END";
  toS9(msg); Serial1.println(msg);
}

void setSolid(uint8_t r,uint8_t g,uint8_t b){ledR=r;ledG=g;ledB=b;ledMode=LED_SOLID;}

void setLedMode(String m){
  m.trim();m.toUpperCase();
  if      (m=="OFF")        {ledMode=LED_OFF;whiteMode=WHITE_M_OFF;}
  else if (m=="POLICE")     {ledMode=LED_POLICE; ledTimer=millis();}
  else if (m=="ALERT")      {ledMode=LED_ALERT;  ledTimer=millis();}
  else if (m=="RAINBOW")    {ledMode=LED_RAINBOW;ledTimer=millis();}
  else if (m=="BREATHE")    {ledMode=LED_BREATHE;if(ledR==0&&ledG==0&&ledB==0){ledR=0;ledG=180;ledB=255;}}
  else if (m=="PARTY")      {ledMode=LED_PARTY;  ledTimer=millis();}
  else if (m=="RED")        setSolid(255,0,0);
  else if (m=="GREEN")      setSolid(0,255,0);
  else if (m=="BLUE")       setSolid(0,0,255);
  else if (m=="CYAN")       setSolid(0,255,255);
  else if (m=="PURPLE")     setSolid(160,0,255);
  else if (m=="ORANGE")     setSolid(255,90,0);
  else if (m=="YELLOW")     setSolid(255,200,0);
  else if (m=="WHITE")      {ledMode=LED_OFF;whiteMode=WHITE_M_ON;}
  else if (m=="WHITE:ON")   {whiteMode=WHITE_M_ON;}
  else if (m=="WHITE:OFF")  {whiteMode=WHITE_M_OFF;}
  else if (m=="WHITE:AUTO") {whiteMode=WHITE_M_AUTO;}
  else if (m=="WHITE:SOLO") {ledMode=LED_OFF;whiteMode=WHITE_M_ON;}
  else if (m.startsWith("BRIGHT:")) {ledBright=(uint8_t)constrain(m.substring(7).toInt(),0,255);}
  else if (m.startsWith("COLOR:")) {
    String p=m.substring(6);
    int c1=p.indexOf(','),c2=p.indexOf(',',c1+1),c3=p.indexOf(',',c2+1);
    if(c1>0&&c2>0){
      uint8_t rr=p.substring(0,c1).toInt();
      uint8_t gg=p.substring(c1+1,c2).toInt();
      uint8_t bb=p.substring(c2+1,c3>0?c3:p.length()).toInt();
      setSolid(rr,gg,bb);
      if(c3>0&&p.substring(c3+1).toInt()>0)whiteMode=WHITE_M_ON;
    }
  }
  else{toS9("ERR|UNKNOWN_LED:"+m+"|END");return;}
  updateLeds(); announceLed();
}

// ════════════════════════════════════════════════════════════════════
//  MAGNETOMETER
// ════════════════════════════════════════════════════════════════════
void initMagnetometer(){
  Wire.beginTransmission(0x30);Wire.write(0x2F);
  if(Wire.endTransmission()==0){Wire.requestFrom(0x30,(uint8_t)1);if(Wire.available()&&Wire.read()==0x0C){magChip=2;magOk=true;Wire.beginTransmission(0x30);Wire.write(0x08);Wire.write(0x20);Wire.endTransmission();delay(20);Wire.beginTransmission(0x30);Wire.write(0x08);Wire.write(0x40);Wire.endTransmission();delay(20);return;}}
  Wire.beginTransmission(0x1E);Wire.write(0x00);Wire.write(0x70);Wire.endTransmission();
  Wire.beginTransmission(0x1E);Wire.write(0x01);Wire.write(0xA0);Wire.endTransmission();
  Wire.beginTransmission(0x1E);Wire.write(0x02);Wire.write(0x00);Wire.endTransmission();
  Wire.beginTransmission(0x1E);if(Wire.endTransmission()==0){magChip=1;magOk=true;}
}

void readMagnetometer(){
  if(!magOk)return;int16_t mx=0,my=0;
  if(magChip==2){Wire.beginTransmission(0x30);Wire.write(0x08);Wire.write(0x01);Wire.endTransmission();delay(10);Wire.beginTransmission(0x30);Wire.write(0x00);Wire.endTransmission();Wire.requestFrom(0x30,(uint8_t)6);if(Wire.available()<6)return;uint8_t xl=Wire.read(),xh=Wire.read(),yl=Wire.read(),yh=Wire.read();Wire.read();Wire.read();mx=((int16_t)(xh<<8|xl))-32768;my=((int16_t)(yh<<8|yl))-32768;}
  else{Wire.beginTransmission(0x1E);Wire.write(0x03);Wire.endTransmission();Wire.requestFrom(0x1E,(uint8_t)6);if(Wire.available()<6)return;uint8_t xh=Wire.read(),xl=Wire.read();Wire.read();Wire.read();uint8_t yh=Wire.read(),yl=Wire.read();mx=(int16_t)(xh<<8|xl);my=(int16_t)(yh<<8|yl);}
  float h=atan2f((float)my,(float)mx)*180.0f/M_PI;if(h<0)h+=360.0f;magHeading=h;
}

// ════════════════════════════════════════════════════════════════════
//  MANUAL CHARGE DETECTION
// ════════════════════════════════════════════════════════════════════
void checkManualCharging(){
  isCharging=(CHARGE_DETECT_PIN>=0)?(digitalRead(CHARGE_DETECT_PIN)==LOW):false;
  if(isCharging&&!wasCharging){
    wasCharging=true;autonomousMode=false;sendMotor("STOP");
    toS9("CHARGE:MANUAL:CONNECTED");Serial1.println(F("CHARGE:MANUAL:CONNECTED"));
    beep(1000,80);delay(100);beep(1300,80);
  }
  if(!isCharging&&wasCharging){
    wasCharging=false;
    toS9("CHARGE:MANUAL:DISCONNECTED");Serial1.println(F("CHARGE:MANUAL:DISCONNECTED"));
    beep(800,150);
  }
}

// ════════════════════════════════════════════════════════════════════
//  STARTUP
// ════════════════════════════════════════════════════════════════════
void initPins(){
  pinMode(VOLTAGE_SENSOR,  INPUT);  pinMode(TEMP_SENSOR_1,    INPUT);
  pinMode(HEAD_TEMP_SENSOR,INPUT);  pinMode(LDR_AO,           INPUT);
  if(GAS_AO      >=0)pinMode(GAS_AO,      INPUT);
  if(SOUND_AO    >=0)pinMode(SOUND_AO,    INPUT);
  if(CURRENT_SENSOR>=0)pinMode(CURRENT_SENSOR,INPUT);

  pinMode(REAR_IR,     INPUT);  pinMode(FRONT_IR,    INPUT);
  if(LEFT_IR >=0)pinMode(LEFT_IR,  INPUT);
  if(RIGHT_IR>=0)pinMode(RIGHT_IR, INPUT);
  pinMode(TILT_SENSOR, INPUT);
  if(PIR_PIN>=0)pinMode(PIR_PIN,INPUT);
  pinMode(UNHINGED_SW,  INPUT_PULLUP);  pinMode(MOMENTARY_BTN,INPUT_PULLUP);
  if(GESTURE_INT>=0)pinMode(GESTURE_INT,INPUT);

  pinMode(FRONT_ECHO,INPUT); pinMode(LEFT_ECHO, INPUT);
  pinMode(RIGHT_ECHO,INPUT); pinMode(REAR_ECHO, INPUT);

  pinMode(FRONT_TRIG,OUTPUT);digitalWrite(FRONT_TRIG,LOW);
  pinMode(LEFT_TRIG, OUTPUT);digitalWrite(LEFT_TRIG, LOW);
  pinMode(RIGHT_TRIG,OUTPUT);digitalWrite(RIGHT_TRIG,LOW);
  pinMode(REAR_TRIG, OUTPUT);digitalWrite(REAR_TRIG, LOW);

  pinMode(FAN_BODY_PIN,     OUTPUT);digitalWrite(FAN_BODY_PIN,     LOW);
  pinMode(FAN_HEAD_BLOW_PIN,OUTPUT);digitalWrite(FAN_HEAD_BLOW_PIN,LOW);
  pinMode(FAN_HEAD_EXT_PIN, OUTPUT);digitalWrite(FAN_HEAD_EXT_PIN, LOW);
  pinMode(UV_LIGHT_PIN,     OUTPUT);digitalWrite(UV_LIGHT_PIN,     LOW);
  pinMode(BUZZER_PIN,       OUTPUT);digitalWrite(BUZZER_PIN,       LOW);

  pinMode(LED_R_PIN,OUTPUT);pinMode(LED_G_PIN,OUTPUT);
  pinMode(LED_B_PIN,OUTPUT);pinMode(LED_W_PIN,OUTPUT);
  writeChan(LED_R_PIN,0);writeChan(LED_G_PIN,0);
  writeChan(LED_B_PIN,0);writeChan(LED_W_PIN,0);

  if(CHARGE_DETECT_PIN>=0)pinMode(CHARGE_DETECT_PIN,INPUT_PULLUP);
  pinMode(TSOP_LEFT,  INPUT_PULLUP);
  pinMode(TSOP_CENTRE,INPUT_PULLUP);
  pinMode(TSOP_RIGHT, INPUT_PULLUP);
  if(HALL_DOCK   >=0){pinMode(HALL_DOCK,   INPUT_PULLUP);}
  if(RELAY_MOTORS>=0){pinMode(RELAY_MOTORS,OUTPUT);digitalWrite(RELAY_MOTORS,LOW);}
  if(GAS_DO>=0)pinMode(GAS_DO,INPUT);

  // ── KS0509 TB6612 motor driver pins (hardwired on PCB) ──────────────────────
  // Must be driven explicitly -- TB6612 input pins float HIGH causing runaway
  pinMode(MTR_A_IN1,OUTPUT);pinMode(MTR_A_IN2,OUTPUT);pinMode(MTR_A_PWM,OUTPUT);
  pinMode(MTR_B_IN1,OUTPUT);pinMode(MTR_B_IN2,OUTPUT);pinMode(MTR_B_PWM,OUTPUT);
  pinMode(MTR_C_IN1,OUTPUT);pinMode(MTR_C_IN2,OUTPUT);pinMode(MTR_C_PWM,OUTPUT);
  pinMode(MTR_D_IN1,OUTPUT);pinMode(MTR_D_IN2,OUTPUT);pinMode(MTR_D_PWM,OUTPUT);
  motorsStop();
}

void startupSequence(){
  digitalWrite(FAN_HEAD_BLOW_PIN,HIGH);
  digitalWrite(FAN_HEAD_EXT_PIN, HIGH);
  digitalWrite(FAN_BODY_PIN,     HIGH);
  delay(400);
  digitalWrite(FAN_HEAD_BLOW_PIN,LOW);
  digitalWrite(FAN_HEAD_EXT_PIN, LOW);
  digitalWrite(FAN_BODY_PIN,     LOW);
  beep(800,80);delay(100);beep(1200,80);delay(100);beep(1600,150);
}

// ════════════════════════════════════════════════════════════════════
//  SETUP
// ════════════════════════════════════════════════════════════════════
void setup(){
  Serial.begin(115200);
  while(!Serial&&millis()<4000){}
  delay(250);

  Serial1.begin(115200);  // Pico W (D18=TX1 / D19=RX1)
  // Serial2 is FREE in V34 -- was R3 motor link in V33
  Serial3.begin(9600);    // GPS NEO-6M (D14=TX3 / D15=RX3)

  delay(400);
  Wire.begin();
  dht.begin();
  initPins();
  initMagnetometer();

  if(GESTURE_INT>=0){
    if(paj7620Init()==0){dbg("[INIT] PAJ7620 OK");}
    else                {dbg("[INIT] PAJ7620 FAILED");}
  } else {
    dbg("[INIT] PAJ7620 skipped -- not connected");
  }

  readAllSensors();
  startupSequence();

  systemReady    = true;
  autonomousMode = false;
  bootStartTime  = millis();
  gasWarmupStart   = millis();  // MQ2 heater warmup starts now
  nav.lastAvoidEnd = millis();

  toS9("SYSTEM|READY|"+String(FW_VERSION)+"|END");

  {
    String boot=F("MEGA_READY|FW:");
    boot+=FW_VERSION;boot+=F("|END");
    Serial1.println(boot);
  }
  Serial1.println(sensorStatusString());

  // V34: TB6612 on-board -- no R3 serial comm test needed
  {
    String cs=F("CONN_STATUS|R3:N/A|MEGA:OK|FW:");
    cs+=FW_VERSION;cs+=F("|END");
    Serial1.println(cs);
  }

  dbg("[READY] BuddyBot " FW_VERSION);
}

// ════════════════════════════════════════════════════════════════════
//  MAIN LOOP
// ════════════════════════════════════════════════════════════════════
void loop(){
  if(millis()-bootStartTime<BOOT_LOCK_TIME){
    static bool bootStopSent=false;
    if(!bootStopSent){motorsStop();bootStopSent=true;}
    handleS9Communication();
    handlePicoCommunication();
    checkManualCharging();
    return;
  }

  if(!systemReady){delay(50);return;}
  unsigned long now=millis();

  static bool standbyAnnounced=false;
  if(!standbyAnnounced){
    standbyAnnounced=true;
    sendMotor("STOP");
    toS9("SYSTEM|STANDBY|AWAITING_COMMAND|END");
    picoDbg("SYS:STANDBY - awaiting command");
    Serial1.println(F("SYSTEM:STANDBY"));
    dbg("[STANDBY] Boot complete -- awaiting command. autonomousMode=OFF");
  }

  if(emergencyStop&&estopRetries<MAX_ESTOP) handleEstopRecovery();

  unhingedMode=(digitalRead(UNHINGED_SW)==LOW);

  handleS9Communication();
  handlePicoCommunication();
  handleGPS();

  if(millis()-lastGPSTx>15000&&gps.location.isValid()){
    lastGPSTx=millis();
    String gm=F("GPS:");gm+=String(gps_lat,6);gm+=",";gm+=String(gps_lon,6);gm+=",";gm+=String(gps_sats);
    toS9(gm);Serial1.println(gm);
  }
  if(magOk&&millis()-lastHDGTx>500){
    lastHDGTx=millis();
    String hm=F("HDG:");hm+=String(magHeading,1);
    toS9(hm);Serial1.println(hm);
  }

  updateLeds();
  runDance();
  runDefense();

  // Non-blocking smoke alarm pulse (300 ms on / 300 ms off)
  if(smokeAlarm&&gasSensorReady){if(now-smokeAlarmBeepTs>600){smokeAlarmBeepTs=now;beep(2800,300);}}

  if(s9Connected&&(now-s9LastHB>S9_TIMEOUT)){s9Connected=false;dbg("[S9] Disconnected");}
  if(picoLinked&&picoLastPingMs>0&&(now-picoLastPingMs>30000)){picoLinked=false;picoLastPingMs=0;}

  if(dockState!=DOCK_IDLE) runDockingStateMachine();

  if(now-lastSense>500){
    lastSense=now;
    readAllSensors();updatePower();updateFans();updateUV();
    checkSafety();checkSmokeAlarm();checkGestures();handleButton();checkManualCharging();
    checkAutoDocktrigger();readMagnetometer();
  }

  if(now-lastTelem>1000){lastTelem=now;sendTelemetryToPico();uptimeSec++;}

  if(s9Connected&&(now-s9LastSent>2000)){s9LastSent=now;sendStatusToS9();}

  if(autonomousMode&&!emergencyStop&&(now-lastNavDec>200)){
    lastNavDec=now;
    makeAutonomousDecision();
    if(lookState !=LOOK_IDLE)  lookAndDecide();
    if(stuckState!=STUCK_IDLE) handleStuck();
    handleRandomTurn();
  }

  if(autonomousMode&&(now-lastAvoid>50)){lastAvoid=now;collisionAvoidance();}

  if(nav.isReversing){
    static unsigned long lastBeepT=0;
    if(now-lastBeepT>500){lastBeepT=now;beep(1000,80);}
  }
}