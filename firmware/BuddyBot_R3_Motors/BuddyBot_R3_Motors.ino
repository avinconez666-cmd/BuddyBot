/*
 * ════════════════════════════════════════════════════════════════════
 *  BUDDYBOT — UNO R3  ·  MOTOR CONTROLLER  ·  PRODUCTION V1.0
 * ════════════════════════════════════════════════════════════════════
 *
 *  MOTOR SHIELD:  Adafruit Motor Shield V1  (74HC595 shift-register)
 *                 Uses the proven direct register approach — NO
 *                 AFMotor library required or used.
 *
 *  SERIAL LINK:   SoftwareSerial on A0 (RX) / A1 (TX) @ 9600 baud
 *                 ← Matches Mega SoftwareSerial pins 10 (TX) / 11 (RX)
 *
 *  WIRING:
 *    Mega Pin 10 (TX) ──────────────► R3 Pin A0 (RX)
 *    Mega Pin 11 (RX) ◄────────────── R3 Pin A1 (TX)
 *    Mega GND         ─────────────── R3 GND          ← MUST share!
 *
 *  ⚠  UPLOAD NOTE:
 *    A0/A1 are NOT the hardware serial port, so you can leave
 *    the Mega wires connected during upload. Safe to upload anytime.
 *
 *  MOTOR LAYOUT (assumed — correct in code if different):
 *    M1 = Front-Left     M2 = Front-Right
 *    M3 = Rear-Left      M4 = Rear-Right
 *
 *  COMMANDS ACCEPTED FROM MEGA:
 *    MOTOR|F       All forward
 *    MOTOR|B       All backward
 *    MOTOR|L       Spin left  (tank style)
 *    MOTOR|R       Spin right (tank style)
 *    MOTOR|S       Stop (release)
 *    MOTOR|DANCE   Dance pattern
 *    DEFENSE       Defense spin pattern
 *    SPEED:SLOW    Speed preset  120
 *    SPEED:NORMAL  Speed preset  200
 *    SPEED:FAST    Speed preset  255
 *    SPEED:nnn     Custom speed  0-255
 *    PING          Alive check → replies PONG
 *    STATUS        Replies current speed + running state
 *
 * ════════════════════════════════════════════════════════════════════
 */

#include <SoftwareSerial.h>

// ── SoftwareSerial: A0=RX (from Mega TX pin 10), A1=TX (to Mega RX pin 11)
SoftwareSerial megaSerial(A0, A1);

// ════════════════════════════════════════════════════════════════════
//  MOTOR SHIELD PINS  (Adafruit V1 — 74HC595 shift register)
//  DO NOT CHANGE — these are hardwired on the shield PCB
// ════════════════════════════════════════════════════════════════════
#define MOTORLATCH   12
#define MOTORCLK      4
#define MOTORENABLE   7
#define MOTORDATA     8

// Shift-register bit positions for each motor terminal
#define MOTOR1_A      2
#define MOTOR1_B      3
#define MOTOR2_A      1
#define MOTOR2_B      4
#define MOTOR3_A      5
#define MOTOR3_B      7
#define MOTOR4_A      0
#define MOTOR4_B      6

// PWM speed pins (direct Arduino PWM outputs)
#define MOTOR1_PWM   11
#define MOTOR2_PWM    3
#define MOTOR3_PWM    6
#define MOTOR4_PWM    5

// ── Motor command constants
#define M_FORWARD   1
#define M_BACKWARD  2
#define M_BRAKE     3
#define M_RELEASE   4

// ── State
uint8_t  currentSpeed  = 200;
bool     motorsRunning = false;
String   cmdBuf        = "";

// Enums for pattern states
enum DanceState { DANCE_IDLE, DANCE_SET_SPEED, DANCE_WIGGLE, DANCE_FORWARD, DANCE_STOP1, DANCE_BACKWARD, DANCE_STOP2, DANCE_SPIN, DANCE_DONE };
enum DefenseState { DEFENSE_IDLE, DEFENSE_SET_SPEED, DEFENSE_LUNGE, DEFENSE_SPIN, DEFENSE_DONE };

// Global state variables
DanceState danceState = DANCE_IDLE;
DefenseState defenseState = DEFENSE_IDLE;
unsigned long patternTimer = 0;
int patternCounter = 0;
uint8_t savedSpeed = 200;

// ════════════════════════════════════════════════════════════════════
//  LOW-LEVEL MOTOR DRIVER  (from working reference sketch)
// ════════════════════════════════════════════════════════════════════

void shiftWrite(int output, int high_low) {
  static int latch_copy = 0;

  bitWrite(latch_copy, output, high_low);
  shiftOut(MOTORDATA, MOTORCLK, MSBFIRST, latch_copy);
  delayMicroseconds(5);
  digitalWrite(MOTORLATCH, HIGH);
  delayMicroseconds(5);
  digitalWrite(MOTORLATCH, LOW);
}

void motor_output(int output, int high_low, int speed) {
  int motorPWM;
  switch (output) {
    case MOTOR1_A: case MOTOR1_B: motorPWM = MOTOR1_PWM; break;
    case MOTOR2_A: case MOTOR2_B: motorPWM = MOTOR2_PWM; break;
    case MOTOR3_A: case MOTOR3_B: motorPWM = MOTOR3_PWM; break;
    case MOTOR4_A: case MOTOR4_B: motorPWM = MOTOR4_PWM; break;
    default: return; // invalid pin — do nothing
  }
  shiftWrite(output, high_low);
  if (speed >= 0 && speed <= 255) {
    analogWrite(motorPWM, speed);
  }
}

void motor(int n, int command, int speed) {
  if (n < 1 || n > 4) return;

  int mA, mB;
  switch (n) {
    case 1: mA = MOTOR1_A; mB = MOTOR1_B; break;
    case 2: mA = MOTOR2_A; mB = MOTOR2_B; break;
    case 3: mA = MOTOR3_A; mB = MOTOR3_B; break;
    case 4: mA = MOTOR4_A; mB = MOTOR4_B; break;
    default: return;
  }

  switch (command) {
    case M_FORWARD:
      motor_output(mA, HIGH, speed);
      motor_output(mB, LOW,  -1);
      break;
    case M_BACKWARD:
      motor_output(mA, LOW,  speed);
      motor_output(mB, HIGH, -1);
      break;
    case M_BRAKE:
      motor_output(mA, LOW, 255);
      motor_output(mB, LOW, -1);
      break;
    case M_RELEASE:
      motor_output(mA, LOW, 0);
      motor_output(mB, LOW, -1);
      break;
  }
}

// ════════════════════════════════════════════════════════════════════
//  HIGH-LEVEL MOVEMENT COMMANDS
// ════════════════════════════════════════════════════════════════════

void stopAll() {
  motor(1, M_RELEASE, 0);
  motor(2, M_RELEASE, 0);
  motor(3, M_RELEASE, 0);
  motor(4, M_RELEASE, 0);
  motorsRunning = false;
}

void moveForward() {
  motor(1, M_FORWARD, currentSpeed);
  motor(2, M_FORWARD, currentSpeed);
  motor(3, M_FORWARD, currentSpeed);
  motor(4, M_FORWARD, currentSpeed);
  motorsRunning = true;
}

void moveBackward() {
  motor(1, M_BACKWARD, currentSpeed);
  motor(2, M_BACKWARD, currentSpeed);
  motor(3, M_BACKWARD, currentSpeed);
  motor(4, M_BACKWARD, currentSpeed);
  motorsRunning = true;
}

// Tank-turn left: left wheels back, right wheels forward
void spinLeft() {
  motor(1, M_BACKWARD, currentSpeed);   // Front-Left  back
  motor(3, M_BACKWARD, currentSpeed);   // Rear-Left   back
  motor(2, M_FORWARD,  currentSpeed);   // Front-Right forward
  motor(4, M_FORWARD,  currentSpeed);   // Rear-Right  forward
  motorsRunning = true;
}

// Tank-turn right: right wheels back, left wheels forward
void spinRight() {
  motor(1, M_FORWARD,  currentSpeed);   // Front-Left  forward
  motor(3, M_FORWARD,  currentSpeed);   // Rear-Left   forward
  motor(2, M_BACKWARD, currentSpeed);   // Front-Right back
  motor(4, M_BACKWARD, currentSpeed);   // Rear-Right  back
  motorsRunning = true;
}

void setSpeed(uint8_t spd) {
  currentSpeed = spd;
  // If motors are already moving, update speed live
  if (motorsRunning) {
    motor(1, M_FORWARD, currentSpeed);
    motor(2, M_FORWARD, currentSpeed);
    motor(3, M_FORWARD, currentSpeed);
    motor(4, M_FORWARD, currentSpeed);
  }
}

// ════════════════════════════════════════════════════════════════════
//  PATTERN LIBRARY
// ════════════════════════════════════════════════════════════════════

void dancePattern() {
  if (danceState == DANCE_IDLE) {
    savedSpeed = currentSpeed;
    motor(1, M_FORWARD, 220); motor(2, M_FORWARD, 220);
    motor(3, M_FORWARD, 220); motor(4, M_FORWARD, 220);
    patternCounter = 0;
    patternTimer = millis() + 280;
    danceState = DANCE_WIGGLE;
    spinLeft();
    return;
  }
  if (danceState == DANCE_WIGGLE && millis() >= patternTimer) {
    patternCounter++;
    if (patternCounter < 6) {  // 3 left + 3 right
      if (patternCounter % 2 == 1) {
        spinRight();
      } else {
        spinLeft();
      }
      patternTimer = millis() + 280;
    } else {
      moveForward();
      patternTimer = millis() + 300;
      danceState = DANCE_FORWARD;
    }
    return;
  }
  if (danceState == DANCE_FORWARD && millis() >= patternTimer) {
    stopAll();
    patternTimer = millis() + 80;
    danceState = DANCE_STOP1;
    return;
  }
  if (danceState == DANCE_STOP1 && millis() >= patternTimer) {
    moveBackward();
    patternTimer = millis() + 300;
    danceState = DANCE_BACKWARD;
    return;
  }
  if (danceState == DANCE_BACKWARD && millis() >= patternTimer) {
    stopAll();
    patternTimer = millis() + 80;
    danceState = DANCE_STOP2;
    return;
  }
  if (danceState == DANCE_STOP2 && millis() >= patternTimer) {
    currentSpeed = 220;
    spinLeft();
    patternTimer = millis() + 650;
    danceState = DANCE_SPIN;
    return;
  }
  if (danceState == DANCE_SPIN && millis() >= patternTimer) {
    stopAll();
    currentSpeed = savedSpeed;
    megaSerial.println(F("ACK:DANCE:DONE"));
    danceState = DANCE_IDLE;
    return;
  }
}

void defensePattern() {
  if (defenseState == DEFENSE_IDLE) {
    savedSpeed = currentSpeed;
    currentSpeed = 255;
    patternCounter = 0;
    patternTimer = millis() + 120;
    defenseState = DEFENSE_LUNGE;
    moveForward();
    return;
  }
  if (defenseState == DEFENSE_LUNGE && millis() >= patternTimer) {
    patternCounter++;
    if (patternCounter < 16) {  // 4 cycles of forward/back/stop
      int step = patternCounter % 4;
      if (step == 0) {
        moveBackward();
        patternTimer = millis() + 120;
      } else if (step == 1) {
        stopAll();
        patternTimer = millis() + 40;
      } else if (step == 2) {
        moveForward();
        patternTimer = millis() + 120;
      } else {
        stopAll();
        patternTimer = millis() + 40;
      }
    } else {
      spinLeft();
      patternTimer = millis() + 750;
      defenseState = DEFENSE_SPIN;
    }
    return;
  }
  if (defenseState == DEFENSE_SPIN && millis() >= patternTimer) {
    stopAll();
    currentSpeed = savedSpeed;
    megaSerial.println(F("ACK:DEFENSE:DONE"));
    defenseState = DEFENSE_IDLE;
    return;
  }
}

// ════════════════════════════════════════════════════════════════════
//  COMMAND PARSER
// ════════════════════════════════════════════════════════════════════

void processCommand(String cmd) {
  cmd.trim();
  // Accept both upper and mixed case from Mega
  // (Mega sends uppercase commands, but just in case)

  // ── MOVEMENT ───────────────────────────────────────────────────
  if (cmd == "MOTOR|F") {
    moveForward();
    megaSerial.println(F("ACK:MOTOR|F"));
    return;
  }
  if (cmd == "MOTOR|B") {
    moveBackward();
    megaSerial.println(F("ACK:MOTOR|B"));
    return;
  }
  if (cmd == "MOTOR|L") {
    spinLeft();
    megaSerial.println(F("ACK:MOTOR|L"));
    return;
  }
  if (cmd == "MOTOR|R") {
    spinRight();
    megaSerial.println(F("ACK:MOTOR|R"));
    return;
  }
  if (cmd == "MOTOR|S") {
    stopAll();
    megaSerial.println(F("ACK:MOTOR|S"));
    return;
  }

  // ── PATTERNS ───────────────────────────────────────────────────
  if (cmd == "MOTOR|DANCE") {
    danceState = DANCE_IDLE;  // Will start on next loop
    return;
  }
  if (cmd == "DEFENSE") {
    defenseState = DEFENSE_IDLE;  // Will start on next loop
    return;
  }

  // ── SPEED PRESETS ──────────────────────────────────────────────
  if (cmd == "SPEED:SLOW") {
    setSpeed(120);
    megaSerial.println(F("ACK:SPEED:120"));
    return;
  }
  if (cmd == "SPEED:NORMAL") {
    setSpeed(200);
    megaSerial.println(F("ACK:SPEED:200"));
    return;
  }
  if (cmd == "SPEED:FAST") {
    setSpeed(255);
    megaSerial.println(F("ACK:SPEED:255"));
    return;
  }

  // ── SPEED:nnn (custom) ─────────────────────────────────────────
  if (cmd.startsWith("SPEED:")) {
    int spd = cmd.substring(6).toInt();
    if (spd >= 0 && spd <= 255) {
      setSpeed((uint8_t)spd);
      megaSerial.print(F("ACK:SPEED:"));
      megaSerial.println(spd);
    } else {
      megaSerial.println(F("ERR:SPEED_RANGE"));
    }
    return;
  }

  // ── DIAGNOSTICS ────────────────────────────────────────────────
  if (cmd == "PING") {
    megaSerial.print(F("PONG:R3:SPD:"));
    megaSerial.print(currentSpeed);
    megaSerial.print(F(":RUN:"));
    megaSerial.println(motorsRunning ? F("Y") : F("N"));
    return;
  }

  if (cmd == "STATUS") {
    megaSerial.println(F("R3:STATUS:BEGIN"));
    megaSerial.print(F("  Speed:"));   megaSerial.println(currentSpeed);
    megaSerial.print(F("  Running:")); megaSerial.println(motorsRunning ? F("YES") : F("NO"));
    megaSerial.println(F("R3:STATUS:END"));
    return;
  }

  // ── UNKNOWN ────────────────────────────────────────────────────
  megaSerial.print(F("ERR:UNKNOWN:"));
  megaSerial.println(cmd);
}

// ════════════════════════════════════════════════════════════════════
//  SETUP
// ════════════════════════════════════════════════════════════════════

void setup() {
  // Disable motor outputs immediately, before anything else
  // Motor stop will be sent via SoftwareSerial after initialization
  megaSerial.begin(9600);
  
  // ✅ FIX: Wait for serial link to stabilize before transmitting
  delay(1000);
  pinMode(MOTORENABLE, OUTPUT);
  digitalWrite(MOTORENABLE, HIGH);  // HIGH = disabled

  pinMode(MOTORLATCH, OUTPUT);
  digitalWrite(MOTORLATCH, LOW);
  pinMode(MOTORCLK,   OUTPUT);
  digitalWrite(MOTORCLK, LOW);
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

// ════════════════════════════════════════════════════════════════════
//  LOOP
// ════════════════════════════════════════════════════════════════════

void loop() {
  // Accumulate bytes from Mega into cmdBuf, fire on newline
  while (megaSerial.available()) {
    char c = megaSerial.read();
    if (c == '\n' || c == '\r') {
      if (cmdBuf.length() > 0) {
        processCommand(cmdBuf);
        cmdBuf = "";
      }
    } else {
      cmdBuf += c;
      // Overflow guard — max realistic command is ~20 chars
      if (cmdBuf.length() > 32) cmdBuf = "";
    }
  }

  // Run pattern state machines
  if (danceState != DANCE_IDLE) dancePattern();
  if (defenseState != DEFENSE_IDLE) defensePattern();
}
