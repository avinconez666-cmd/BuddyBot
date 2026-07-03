#include <SoftwareSerial.h>

#define MOTORLATCH   12
#define MOTORCLK      4
#define MOTORENABLE   7
#define MOTORDATA     8

#define MOTOR1_A      2
#define MOTOR1_B      3
#define MOTOR2_A      1
#define MOTOR2_B      4
#define MOTOR3_A      5
#define MOTOR3_B      7
#define MOTOR4_A      0
#define MOTOR4_B      6

#define MOTOR1_PWM   11
#define MOTOR2_PWM    3
#define MOTOR3_PWM    6
#define MOTOR4_PWM    5

SoftwareSerial megaSerial(A0, A1); // RX=A0, TX=A1
String cmdBuf = "";
uint8_t currentSpeed = 150;

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
    default: return;
  }

  shiftWrite(output, high_low);

  // Keep the same Timer0 avoidance for Motor 3 as the production sketch
  if (speed >= 0 && speed <= 255 && motorPWM != 6) {
    analogWrite(motorPWM, speed);
  }
}

void motor(int n, int command, int speed) {
  int mA, mB;
  switch (n) {
    case 1: mA = MOTOR1_A; mB = MOTOR1_B; break;
    case 2: mA = MOTOR2_A; mB = MOTOR2_B; break;
    case 3: mA = MOTOR3_A; mB = MOTOR3_B; break;
    case 4: mA = MOTOR4_A; mB = MOTOR4_B; break;
    default: return;
  }

  switch (command) {
    case 1: // forward
      motor_output(mA, HIGH, speed);
      motor_output(mB, LOW,  -1);
      break;
    case 2: // backward
      motor_output(mA, LOW,  speed);
      motor_output(mB, HIGH, -1);
      break;
    case 3: // brake
      motor_output(mA, LOW,  255);
      motor_output(mB, LOW,  -1);
      break;
    case 4: // release
      motor_output(mA, LOW,  0);
      motor_output(mB, LOW,  -1);
      break;
  }
}

void stopAll() {
  motor(1, 4, 0);
  motor(2, 4, 0);
  motor(3, 4, 0);
  motor(4, 4, 0);
}

void runMotorTest(int motorNum) {
  Serial.print("TEST: Motor ");
  Serial.print(motorNum);
  Serial.println(" forward 150");
  motor(motorNum, 1, 150);
  delay(200);
  stopAll();
  delay(100);
}

void processCommand(const String &raw) {
  String cmd = raw;
  cmd.trim();
  Serial.print("RECEIVED RAW: \"");
  Serial.print(raw);
  Serial.println("\"");
  Serial.print("PARSED CMD: \"");
  Serial.print(cmd);
  Serial.println("\"");

  bool known = false;

  if (cmd == "PING") {
    known = true;
    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
    megaSerial.println("PONG");
    Serial.println("MATCH: PING -> PONG");
  }
  else if (cmd == "MOTOR|F") {
    known = true;
    motor(1, 1, currentSpeed);
    motor(2, 1, currentSpeed);
    motor(3, 1, currentSpeed);
    motor(4, 1, currentSpeed);
    Serial.println("MATCH: MOTOR|F");
  }
  else if (cmd == "MOTOR|B") {
    known = true;
    motor(1, 2, currentSpeed);
    motor(2, 2, currentSpeed);
    motor(3, 2, currentSpeed);
    motor(4, 2, currentSpeed);
    Serial.println("MATCH: MOTOR|B");
  }
  else if (cmd == "MOTOR|L") {
    known = true;
    motor(1, 2, currentSpeed);
    motor(3, 2, currentSpeed);
    motor(2, 1, currentSpeed);
    motor(4, 1, currentSpeed);
    Serial.println("MATCH: MOTOR|L");
  }
  else if (cmd == "MOTOR|R") {
    known = true;
    motor(1, 1, currentSpeed);
    motor(3, 1, currentSpeed);
    motor(2, 2, currentSpeed);
    motor(4, 2, currentSpeed);
    Serial.println("MATCH: MOTOR|R");
  }
  else if (cmd == "MOTOR|S") {
    known = true;
    stopAll();
    Serial.println("MATCH: MOTOR|S");
  }
  else if (cmd == "MOTOR|DANCE") {
    known = true;
    Serial.println("MATCH: MOTOR|DANCE (ignored)");
  }
  else if (cmd == "DEFENSE") {
    known = true;
    Serial.println("MATCH: DEFENSE (ignored)");
  }
  else if (cmd == "SPEED:SLOW") {
    known = true;
    currentSpeed = 120;
    Serial.println("MATCH: SPEED:SLOW");
  }
  else if (cmd == "SPEED:NORMAL") {
    known = true;
    currentSpeed = 200;
    Serial.println("MATCH: SPEED:NORMAL");
  }
  else if (cmd == "SPEED:FAST") {
    known = true;
    currentSpeed = 255;
    Serial.println("MATCH: SPEED:FAST");
  }
  else if (cmd.startsWith("SPEED:")) {
    int spd = cmd.substring(6).toInt();
    if (spd >= 0 && spd <= 255) {
      known = true;
      currentSpeed = spd;
      Serial.print("MATCH: ");
      Serial.println(cmd);
    }
  }

  if (!known) {
    Serial.println("MATCH: UNKNOWN");
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  pinMode(MOTORENABLE, OUTPUT);
  digitalWrite(MOTORENABLE, HIGH);  // Disable motor outputs first

  pinMode(MOTORLATCH, OUTPUT);
  digitalWrite(MOTORLATCH, LOW);
  pinMode(MOTORCLK, OUTPUT);
  digitalWrite(MOTORCLK, LOW);
  pinMode(MOTORDATA, OUTPUT);
  digitalWrite(MOTORDATA, LOW);

  shiftOut(MOTORDATA, MOTORCLK, MSBFIRST, 0x00);
  delayMicroseconds(5);
  digitalWrite(MOTORLATCH, HIGH);
  delayMicroseconds(5);
  digitalWrite(MOTORLATCH, LOW);

  Serial.println("TEST: init complete, MOTORENABLE HIGH");
  megaSerial.begin(9600);
  Serial.println("TEST: SoftwareSerial started");

  digitalWrite(MOTORENABLE, LOW);   // Enable outputs for test
  Serial.println("TEST: MOTORENABLE LOW, starting motor checks");

  runMotorTest(1);
  runMotorTest(2);
  runMotorTest(3);
  runMotorTest(4);

  stopAll();
  digitalWrite(MOTORENABLE, HIGH);  // Disable again after the test
  Serial.println("TEST: motor diagnostics complete, MOTORENABLE HIGH");
  Serial.println("READY");
}

void loop() {
  while (megaSerial.available()) {
    char c = megaSerial.read();
    digitalWrite(LED_BUILTIN, HIGH);
    delay(20);
    digitalWrite(LED_BUILTIN, LOW);

    if (c == '\n' || c == '\r') {
      if (cmdBuf.length() > 0) {
        processCommand(cmdBuf);
        cmdBuf = "";
      }
    } else {
      cmdBuf += c;
      if (cmdBuf.length() > 64) {
        cmdBuf = "";
        Serial.println("WARNING: cmdBuf overflow, cleared");
      }
    }
  }
}