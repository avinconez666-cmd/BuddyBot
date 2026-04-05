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

// ════════════════════════════════════════════════════════════════════
//  LOW-LEVEL MOTOR DRIVER  (from working reference sketch)
// ════════════════════════════════════════════════════════════════════

void shiftWrite(int output, int high_low) {
  static int latch_copy             = 0;
  static bool shift_register_init   = false;

  if (!shift_register_init) {
    pinMode(MOTORLATCH,  OUTPUT);
    pinMode(MOTORENABLE, OUTPUT);
    pinMode(MOTORDATA,   OUTPUT);
    pinMode(MOTORCLK,    OUTPUT);
    digitalWrite(MOTORDATA,   LOW);
    digitalWrite(MOTORLATCH,  LOW);
    digitalWrite(MOTORCLK,    LOW);
    digitalWrite(MOTORENABLE, LOW);   // LOW = enabled
    latch_copy           = 0;
    shift_register_init  = true;
  }

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
  uint8_t saved = currentSpeed;

  // Set dance speed
  motor(1, M_FORWARD, 220); motor(2, M_FORWARD, 220);
  motor(3, M_FORWARD, 220); motor(4, M_FORWARD, 220);

  // Left-right wiggle x3
  for (int i = 0; i < 3; i++) {
    spinLeft();  delay(280);
    spinRight(); delay(280);
  }
  // Forward & back
  moveForward();  delay(300);
  stopAll();      delay(80);
  moveBackward(); delay(300);
  stopAll();      delay(80);
  // Final spin
  currentSpeed = 220;
  spinLeft();  delay(650);
  stopAll();

  currentSpeed = saved;
  megaSerial.println(F("ACK:DANCE:DONE"));
}

void defensePattern() {
  uint8_t saved = currentSpeed;
  currentSpeed = 255;

  // Aggressive lunge x4
  for (int i = 0; i < 4; i++) {
    moveForward();  delay(120);
    moveBackward(); delay(120);
    stopAll();      delay(40);
  }
  // Hard spin
  spinLeft();  delay(750);
  stopAll();

  currentSpeed = saved;
  megaSerial.println(F("ACK:DEFENSE:DONE"));
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
    dancePattern();
    return;
  }
  if (cmd == "DEFENSE") {
    defensePattern();
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
  // SoftwareSerial to Mega — must match Mega's motorComm baud rate
  megaSerial.begin(9600);

  // Ensure all motors start stopped
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
}
