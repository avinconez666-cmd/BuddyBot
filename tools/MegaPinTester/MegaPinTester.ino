/*
 * ════════════════════════════════════════════════════════════════════
 *  BUDDYBOT — MEGA PIN TESTER  ·  DIAGNOSTIC UTILITY
 * ════════════════════════════════════════════════════════════════════
 *
 *  Upload to Mega, open Serial Monitor at 115200 baud.
 *  Type commands and press Enter.
 *
 *  COMMANDS
 *  ────────────────────────────────────────────────────────────────
 *  HELP                  Show this command list
 *  HIGH <pin>            Set pin HIGH (OUTPUT mode)
 *  LOW  <pin>            Set pin LOW  (OUTPUT mode)
 *  READ <pin>            Read digital value of any pin (INPUT_PULLUP)
 *  AREAD <pin>           Read analog value (0-1023) from any pin
 *  PULSE <pin> <ms>      Pulse pin HIGH for <ms> milliseconds then LOW
 *  WATCH <pin>           Continuously print digital state (1/sec) — Enter to stop
 *  AWATCH <pin>          Continuously print analog value (1/sec) — Enter to stop
 *  SERIAL2 <text>        Send raw text to Serial2 (R3 on pins 16/17)
 *  SERIAL1 <text>        Send raw text to Serial1 (Pico on pins 18/19)
 *  SERIAL3 <text>        Send raw text to Serial3 (ESP32 on pins 14/15)
 *  LISTEN2               Listen on Serial2 for 5 seconds and print everything
 *  LISTEN1               Listen on Serial1 for 5 seconds and print everything
 *  LISTEN3               Listen on Serial3 for 5 seconds and print everything
 *  SCAN                  Scan all digital pins — report HIGH/LOW for each
 *  ASCAN                 Scan all analog pins — report raw ADC values
 *  I2C                   Scan I2C bus — report all device addresses found
 *  PWM <pin> <0-255>     Write PWM value to a PWM-capable pin
 *  BLINK <pin> <ms>      Blink pin at <ms> interval — Enter to stop
 *  TONE <pin> <hz> <ms>  Play tone on pin for duration
 *  PINS                  List all BuddyBot V31 pin assignments for reference
 *
 *  NOTES
 *  ────────────────────────────────────────────────────────────────
 *  - Analog pins: use A0-A15 or their digital numbers (54-69)
 *  - Pins 0/1 are USB Serial — do not set HIGH/LOW on these
 *  - Serial2 baud: 9600 (R3),  Serial1: 115200 (Pico),  Serial3: 115200 (ESP32)
 *
 * ════════════════════════════════════════════════════════════════════
 */

#include <Wire.h>

// ── BuddyBot V31 pin reference table ─────────────────────────────────────────
struct PinRef { const char* name; int pin; };
const PinRef PIN_REF[] PROGMEM = {
  {"VOLTAGE_SENSOR",    A6},
  {"TEMP_SENSOR_1",     A2},
  {"HEAD_TEMP_SENSOR",  A13},
  {"LDR_AO",            A4},
  {"GAS_AO",            A14},
  {"GESTURE_INT",       -1},
  {"SOUND_AO",          -1},
  {"FAN_BODY_PIN",      12},
  {"FAN_HEAD_BLOW",      9},
  {"FAN_HEAD_EXT",       8},
  {"UV_LIGHT_PIN",       4},
  {"BUZZER_PIN",        23},
  {"MOMENTARY_BTN",     24},
  {"LDR_DO",             5},
  {"UNHINGED_SW",       A1},
  {"TILT_SENSOR",       52},
  {"PIR_PIN",           -1},
  {"DHT_PIN",           33},
  {"GAS_DO",            -1},
  {"RF_PIN",             2},
  {"CURRENT_SENSOR",     3},
  {"CHARGE_DETECT",     27},
  {"REAR_IR",           25},
  {"FRONT_IR",          A8},
  {"LEFT_IR",           -1},
  {"RIGHT_IR",          -1},
  {"FRONT_TRIG",        35},
  {"FRONT_ECHO",        34},
  {"LEFT_TRIG",         29},
  {"LEFT_ECHO",         28},
  {"RIGHT_TRIG",        38},
  {"RIGHT_ECHO",        40},
  {"REAR_TRIG",         42},
  {"REAR_ECHO",         43},
  {"Serial1_TX(Pico)",  18},
  {"Serial1_RX(Pico)",  19},
  {"Serial2_TX(R3)",    17},
  {"Serial2_RX(R3)",    16},
  {"Serial3_TX(ESP32)", 14},
  {"Serial3_RX(ESP32)", 15},
};
const int PIN_REF_COUNT = sizeof(PIN_REF) / sizeof(PinRef);

// ── PWM-capable pins on Mega ──────────────────────────────────────────────────
const int PWM_PINS[] = {2,3,4,5,6,7,8,9,10,11,12,13,44,45,46};
const int PWM_COUNT  = 15;

String cmdBuf = "";

// ════════════════════════════════════════════════════════════════════
//  HELPERS
// ════════════════════════════════════════════════════════════════════

bool isPWMPin(int pin) {
  for (int i = 0; i < PWM_COUNT; i++) if (PWM_PINS[i] == pin) return true;
  return false;
}

// Parse "A0"-"A15" or plain integer
int parsePin(String s) {
  s.trim();
  if (s.startsWith("A") || s.startsWith("a")) {
    int n = s.substring(1).toInt();
    if (n >= 0 && n <= 15) return A0 + n;
  }
  return s.toInt();
}

void divider() {
  Serial.println(F("────────────────────────────────────────────────────────"));
}

void prompt() {
  Serial.print(F("\n> "));
}

// ════════════════════════════════════════════════════════════════════
//  COMMAND HANDLERS
// ════════════════════════════════════════════════════════════════════

void cmdHelp() {
  divider();
  Serial.println(F("  BUDDYBOT MEGA PIN TESTER — COMMANDS"));
  divider();
  Serial.println(F("  HIGH <pin>           Set pin HIGH"));
  Serial.println(F("  LOW  <pin>           Set pin LOW"));
  Serial.println(F("  READ <pin>           Digital read (INPUT_PULLUP)"));
  Serial.println(F("  AREAD <pin>          Analog read (0-1023)"));
  Serial.println(F("  PULSE <pin> <ms>     Pulse HIGH for ms then LOW"));
  Serial.println(F("  WATCH <pin>          Live digital watch (Enter to stop)"));
  Serial.println(F("  AWATCH <pin>         Live analog watch  (Enter to stop)"));
  Serial.println(F("  PWM <pin> <0-255>    Write PWM value"));
  Serial.println(F("  BLINK <pin> <ms>     Blink pin (Enter to stop)"));
  Serial.println(F("  TONE <pin> <hz> <ms> Play tone"));
  Serial.println(F("  SCAN                 Scan all digital pins"));
  Serial.println(F("  ASCAN                Scan all analog pins"));
  Serial.println(F("  I2C                  Scan I2C bus"));
  Serial.println(F("  SERIAL1 <text>       Send to Pico  (Serial1 115200)"));
  Serial.println(F("  SERIAL2 <text>       Send to R3    (Serial2 9600)"));
  Serial.println(F("  SERIAL3 <text>       Send to ESP32 (Serial3 115200)"));
  Serial.println(F("  LISTEN1              Listen Serial1 5 seconds"));
  Serial.println(F("  LISTEN2              Listen Serial2 5 seconds"));
  Serial.println(F("  LISTEN3              Listen Serial3 5 seconds"));
  Serial.println(F("  PINS                 Show V31 pin assignments"));
  divider();
}

void cmdHigh(int pin) {
  if (pin == 0 || pin == 1) { Serial.println(F("ERR: pins 0/1 are USB — skipped")); return; }
  pinMode(pin, OUTPUT);
  digitalWrite(pin, HIGH);
  Serial.print(F("  Pin ")); Serial.print(pin); Serial.println(F(" → HIGH ✓"));
}

void cmdLow(int pin) {
  if (pin == 0 || pin == 1) { Serial.println(F("ERR: pins 0/1 are USB — skipped")); return; }
  pinMode(pin, OUTPUT);
  digitalWrite(pin, LOW);
  Serial.print(F("  Pin ")); Serial.print(pin); Serial.println(F(" → LOW ✓"));
}

void cmdRead(int pin) {
  pinMode(pin, INPUT_PULLUP);
  delay(5);
  int v = digitalRead(pin);
  Serial.print(F("  Pin ")); Serial.print(pin);
  Serial.print(F(" = ")); Serial.println(v == HIGH ? F("HIGH (1)") : F("LOW  (0)"));
}

void cmdAread(int pin) {
  int v = analogRead(pin);
  float volts = (v / 1023.0f) * 5.0f;
  Serial.print(F("  Pin ")); Serial.print(pin);
  Serial.print(F(" = ")); Serial.print(v);
  Serial.print(F("  ("));  Serial.print(volts, 2); Serial.println(F("V)"));
}

void cmdPulse(int pin, int ms) {
  if (pin == 0 || pin == 1) { Serial.println(F("ERR: pins 0/1 are USB — skipped")); return; }
  pinMode(pin, OUTPUT);
  digitalWrite(pin, HIGH);
  Serial.print(F("  Pin ")); Serial.print(pin); Serial.print(F(" HIGH for ")); Serial.print(ms); Serial.println(F("ms..."));
  delay(ms);
  digitalWrite(pin, LOW);
  Serial.println(F("  → LOW ✓"));
}

void cmdWatch(int pin, bool analog) {
  Serial.print(F("  Watching pin ")); Serial.print(pin);
  Serial.println(F(" — send any character to stop"));
  if (!analog) pinMode(pin, INPUT_PULLUP);
  while (true) {
    if (Serial.available()) { Serial.read(); Serial.println(F("\n  Stopped.")); return; }
    Serial.print(F("  [")); Serial.print(millis()/1000); Serial.print(F("s] Pin ")); Serial.print(pin); Serial.print(F(" = "));
    if (analog) {
      int v = analogRead(pin);
      Serial.print(v); Serial.print(F("  (")); Serial.print((v/1023.0f)*5.0f, 2); Serial.println(F("V)"));
    } else {
      Serial.println(digitalRead(pin) == HIGH ? F("HIGH") : F("LOW"));
    }
    delay(1000);
  }
}

void cmdPWM(int pin, int val) {
  if (!isPWMPin(pin)) {
    Serial.print(F("  WARN: pin ")); Serial.print(pin); Serial.println(F(" may not be PWM-capable on Mega"));
  }
  pinMode(pin, OUTPUT);
  analogWrite(pin, val);
  Serial.print(F("  Pin ")); Serial.print(pin); Serial.print(F(" PWM = ")); Serial.print(val);
  Serial.print(F("  (")); Serial.print((val/255.0f)*100.0f, 0); Serial.println(F("%)"));
}

void cmdBlink(int pin, int ms) {
  if (pin == 0 || pin == 1) { Serial.println(F("ERR: pins 0/1 are USB — skipped")); return; }
  pinMode(pin, OUTPUT);
  Serial.print(F("  Blinking pin ")); Serial.print(pin); Serial.print(F(" every ")); Serial.print(ms); Serial.println(F("ms — send any character to stop"));
  bool state = false;
  unsigned long last = 0;
  while (true) {
    if (Serial.available()) {
      Serial.read();
      digitalWrite(pin, LOW);
      Serial.println(F("\n  Stopped — pin set LOW."));
      return;
    }
    if (millis() - last >= (unsigned long)ms) {
      last = millis();
      state = !state;
      digitalWrite(pin, state ? HIGH : LOW);
    }
  }
}

void cmdTone(int pin, int hz, int ms) {
  Serial.print(F("  Tone on pin ")); Serial.print(pin); Serial.print(F(" @ ")); Serial.print(hz); Serial.print(F("Hz for ")); Serial.print(ms); Serial.println(F("ms"));
  tone(pin, hz, ms);
  delay(ms + 10);
}

void cmdScan() {
  Serial.println(F("  Scanning all digital pins (INPUT_PULLUP)..."));
  divider();
  for (int pin = 2; pin <= 53; pin++) {
    if (pin == 0 || pin == 1) continue;
    pinMode(pin, INPUT_PULLUP);
    delay(2);
    int v = digitalRead(pin);
    Serial.print(F("  D")); if (pin < 10) Serial.print('0');
    Serial.print(pin); Serial.print(F(": "));
    Serial.println(v == HIGH ? F("HIGH") : F("LOW ← possible connection"));
  }
  divider();
}

void cmdAscan() {
  Serial.println(F("  Scanning all analog pins A0-A15..."));
  divider();
  for (int i = 0; i <= 15; i++) {
    int v = analogRead(A0 + i);
    float volts = (v / 1023.0f) * 5.0f;
    Serial.print(F("  A")); Serial.print(i); Serial.print(F(":\t"));
    Serial.print(v); Serial.print(F("\t(")); Serial.print(volts, 2); Serial.println(F("V)"));
  }
  divider();
}

void cmdI2C() {
  Serial.println(F("  Scanning I2C bus (SDA=D20, SCL=D21)..."));
  divider();
  uint8_t found = 0;
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.print(F("  FOUND: 0x")); if (addr < 16) Serial.print('0');
      Serial.print(addr, HEX);
      // Known addresses
      if (addr == 0x73 || addr == 0x77) Serial.print(F("  ← PAJ7620 gesture sensor"));
      if (addr == 0x68)                  Serial.print(F("  ← MPU6050 / DS3231 RTC"));
      if (addr == 0x3C || addr == 0x3D)  Serial.print(F("  ← OLED display"));
      if (addr == 0x27 || addr == 0x3F)  Serial.print(F("  ← LCD I2C backpack"));
      Serial.println();
      found++;
    }
    delay(5);
  }
  if (found == 0) Serial.println(F("  No devices found — check SDA/SCL wiring and pull-ups"));
  else { Serial.print(F("  Total: ")); Serial.print(found); Serial.println(F(" device(s)")); }
  divider();
}

void cmdSerialSend(int port, String text) {
  text.trim();
  switch (port) {
    case 1: Serial1.println(text); Serial.print(F("  → Serial1 (Pico):  ")); break;
    case 2: Serial2.println(text); Serial.print(F("  → Serial2 (R3):    ")); break;
    case 3: Serial3.println(text); Serial.print(F("  → Serial3 (ESP32): ")); break;
  }
  Serial.println(text);
}

void cmdListen(int port) {
  int duration = (port == 2) ? 15000 : 5000;  // Serial2 (R3) gets 15s, others 5s
  Serial.print(F("  Listening Serial")); Serial.print(port);
  Serial.print(F(" for ")); Serial.print(duration/1000); Serial.println(F(" seconds..."));
  divider();
  unsigned long t = millis();
  int count = 0;
  while (millis() - t < (unsigned long)duration) {
    bool avail = false;
    switch (port) {
      case 1: avail = Serial1.available(); break;
      case 2: avail = Serial2.available(); break;
      case 3: avail = Serial3.available(); break;
    }
    if (avail) {
      char c;
      switch (port) {
        case 1: c = Serial1.read(); break;
        case 2: c = Serial2.read(); break;
        case 3: c = Serial3.read(); break;
        default: c = 0;
      }
      Serial.print(c);
      count++;
    }
  }
  divider();
  if (count == 0) Serial.println(F("  No data received."));
  else { Serial.print(F("\n  Total bytes: ")); Serial.println(count); }
}

void cmdPins() {
  Serial.println(F("  BUDDYBOT V31 PIN ASSIGNMENTS"));
  divider();
  for (int i = 0; i < PIN_REF_COUNT; i++) {
    Serial.print(F("  "));
    Serial.print(PIN_REF[i].name);
    Serial.print(F("\t→ "));
    if (PIN_REF[i].pin == -1) Serial.println(F("not connected"));
    else { Serial.print(F("pin ")); Serial.println(PIN_REF[i].pin); }
  }
  divider();
}

// ════════════════════════════════════════════════════════════════════
//  COMMAND PARSER
// ════════════════════════════════════════════════════════════════════

void processCommand(String raw) {
  raw.trim();
  if (raw.length() == 0) return;

  // Uppercase the command word only (preserve text args)
  int sp = raw.indexOf(' ');
  String cmd  = (sp == -1) ? raw : raw.substring(0, sp);
  String args = (sp == -1) ? "" : raw.substring(sp + 1);
  cmd.toUpperCase();

  Serial.println();

  if (cmd == "HELP") { cmdHelp(); return; }
  if (cmd == "PINS") { cmdPins(); return; }
  if (cmd == "SCAN") { cmdScan(); return; }
  if (cmd == "ASCAN"){ cmdAscan(); return; }
  if (cmd == "I2C")  { cmdI2C();  return; }

  if (cmd == "HIGH") { cmdHigh(parsePin(args)); return; }
  if (cmd == "LOW")  { cmdLow(parsePin(args));  return; }
  if (cmd == "READ") { cmdRead(parsePin(args));  return; }
  if (cmd == "AREAD"){ cmdAread(parsePin(args)); return; }

  if (cmd == "WATCH") { cmdWatch(parsePin(args), false); return; }
  if (cmd == "AWATCH"){ cmdWatch(parsePin(args), true);  return; }

  if (cmd == "PULSE") {
    int s2 = args.indexOf(' ');
    if (s2 == -1) { Serial.println(F("  Usage: PULSE <pin> <ms>")); return; }
    cmdPulse(parsePin(args.substring(0, s2)), args.substring(s2+1).toInt());
    return;
  }

  if (cmd == "PWM") {
    int s2 = args.indexOf(' ');
    if (s2 == -1) { Serial.println(F("  Usage: PWM <pin> <0-255>")); return; }
    cmdPWM(parsePin(args.substring(0, s2)), args.substring(s2+1).toInt());
    return;
  }

  if (cmd == "BLINK") {
    int s2 = args.indexOf(' ');
    if (s2 == -1) { Serial.println(F("  Usage: BLINK <pin> <ms>")); return; }
    cmdBlink(parsePin(args.substring(0, s2)), args.substring(s2+1).toInt());
    return;
  }

  if (cmd == "TONE") {
    // TONE <pin> <hz> <ms>
    int s2 = args.indexOf(' ');
    int s3 = args.indexOf(' ', s2+1);
    if (s2 == -1 || s3 == -1) { Serial.println(F("  Usage: TONE <pin> <hz> <ms>")); return; }
    cmdTone(parsePin(args.substring(0,s2)), args.substring(s2+1,s3).toInt(), args.substring(s3+1).toInt());
    return;
  }

  if (cmd == "SERIAL1") { cmdSerialSend(1, args); return; }
  if (cmd == "SERIAL2") { cmdSerialSend(2, args); return; }
  if (cmd == "SERIAL3") { cmdSerialSend(3, args); return; }
  if (cmd == "LISTEN1") { cmdListen(1); return; }
  if (cmd == "LISTEN2") { cmdListen(2); return; }
  if (cmd == "LISTEN3") { cmdListen(3); return; }

  Serial.print(F("  Unknown command: ")); Serial.println(cmd);
  Serial.println(F("  Type HELP for command list."));
}

// ════════════════════════════════════════════════════════════════════
//  SETUP & LOOP
// ════════════════════════════════════════════════════════════════════

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) {}
  Serial1.begin(115200);
  Serial2.begin(9600);
  Serial3.begin(115200);
  Wire.begin();

  divider();
  Serial.println(F("  BUDDYBOT MEGA PIN TESTER — Ready"));
  Serial.println(F("  Type HELP for command list"));
  divider();
  prompt();
}

void loop() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (cmdBuf.length() > 0) {
        processCommand(cmdBuf);
        cmdBuf = "";
        prompt();
      }
    } else {
      cmdBuf += c;
    }
  }
}
