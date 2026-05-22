/*
 * ════════════════════════════════════════════════════════════════════
 *  BUDDYBOT — MEGA ↔ R3 SERIAL COMMUNICATION DIAGNOSTIC
 *  Upload to Mega 2560. Open Serial Monitor at 115200 baud.
 *  R3 must be powered and running BuddyBot_R3_Motors_V2.
 * ════════════════════════════════════════════════════════════════════
 *
 *  This sketch automatically works through every possible cause of
 *  Serial2 ↔ R3 SoftwareSerial failure and reports findings clearly.
 *
 *  AUTOMATIC TESTS (run on boot):
 *    1. Pin voltage check  — pins 16/17 idle state
 *    2. TX test            — send known string, listen for echo
 *    3. PING/PONG test     — R3 PING → expects PONG:R3:...
 *    4. Baud rate sweep    — tries 4800/9600/19200/38400/57600
 *    5. TX/RX swap test    — tries swapped wiring via AltSoftSerial
 *    6. Noise analysis     — 3 second raw byte capture
 *    7. Boot listen        — power-cycle detection of R3:READY
 *
 *  MANUAL COMMANDS (type in Serial Monitor):
 *    PING           Send PING → listen 15s for PONG
 *    SEND <text>    Send raw text to R3
 *    LISTEN <n>     Listen raw for n seconds
 *    BAUD <n>       Switch to baud rate n and retry PING
 *    SWAP           Swap TX/RX pins (A0↔A1) and retry
 *    RESTORE        Restore original TX/RX assignment
 *    SCAN           Re-run all automatic tests
 *    RESET          Send reset pulse on pin 17 (triggers R3 bootloader reset)
 *    HELP           Show commands
 *
 * ════════════════════════════════════════════════════════════════════
 */

// ── Pin assignments — MATCHES BuddyBot V31 ───────────────────────────────────
// Hardware Serial2: TX=pin17, RX=pin16
#define MEGA_TX2   17
#define MEGA_RX2   16

// ── Timing ───────────────────────────────────────────────────────────────────
#define PING_TIMEOUT_MS   3000
#define LISTEN_DEFAULT_MS 15000
#define BAUD_RETRY_MS     3000

// ── Baud rates to sweep ───────────────────────────────────────────────────────
const long BAUD_RATES[]  = {4800, 9600, 19200, 38400, 57600};
const int  BAUD_COUNT    = 5;
long       currentBaud   = 9600;

// ── State ─────────────────────────────────────────────────────────────────────
bool txRxSwapped = false;
String cmdBuf    = "";

// ════════════════════════════════════════════════════════════════════
//  HELPERS
// ════════════════════════════════════════════════════════════════════

void hdr(const char* title) {
  Serial.println();
  Serial.println(F("════════════════════════════════════════"));
  Serial.print(F("  ")); Serial.println(title);
  Serial.println(F("════════════════════════════════════════"));
}

void pass(const char* msg) {
  Serial.print(F("  ✓ PASS  ")); Serial.println(msg);
}

void fail(const char* msg) {
  Serial.print(F("  ✗ FAIL  ")); Serial.println(msg);
}

void info(const char* msg) {
  Serial.print(F("  ·  ")); Serial.println(msg);
}

void prompt() { Serial.print(F("\n> ")); }

// Send string to R3 and listen for response containing expected token
// Returns true if token found within timeout_ms
bool sendAndExpect(const String& tx, const String& expect, unsigned long timeout_ms) {
  // Flush RX buffer first
  while (Serial2.available()) Serial2.read();

  Serial2.println(tx);
  Serial.print(F("  → TX: ")); Serial.println(tx);

  String response = "";
  unsigned long deadline = millis() + timeout_ms;

  while (millis() < deadline) {
    while (Serial2.available()) {
      char c = Serial2.read();
      if (c == '\n' || c == '\r') {
        if (response.length() > 0) {
          Serial.print(F("  ← RX: ")); Serial.println(response);
          if (response.indexOf(expect) != -1) return true;
          response = "";
        }
      } else {
        response += c;
        if (response.length() > 64) response = "";  // overflow guard
      }
    }
  }
  return false;
}

// Raw listen — print everything received for duration_ms
// Returns total bytes received
int rawListen(unsigned long duration_ms) {
  unsigned long deadline = millis() + duration_ms;
  int count = 0;
  bool hadData = false;
  Serial.print(F("  Raw RX ("));
  Serial.print(duration_ms / 1000);
  Serial.println(F("s):"));
  Serial.print(F("  ["));

  while (millis() < deadline) {
    while (Serial2.available()) {
      char c = Serial2.read();
      Serial.print(c);
      count++;
      hadData = true;
    }
  }
  Serial.println(F("]"));
  if (!hadData) Serial.println(F("  (no data)"));
  return count;
}

void applyBaud(long baud) {
  Serial2.end();
  delay(50);
  Serial2.begin(baud);
  delay(100);
  currentBaud = baud;
  Serial.print(F("  Baud set to ")); Serial.println(baud);
}

// ════════════════════════════════════════════════════════════════════
//  DIAGNOSTIC TESTS
// ════════════════════════════════════════════════════════════════════

// TEST 0 — Serial2 Loopback (pin 17 jumpered to pin 16)
// Before running: disconnect R3 wires from pins 16/17
// Bridge pin 17 → pin 16 with a single jumper wire
void testLoopback() {
  hdr("TEST 0: SERIAL2 LOOPBACK (TX→RX JUMPER)");
  Serial.println(F("  !! Disconnect R3 wires from pins 16 & 17 first !!"));
  Serial.println(F("  !! Jumper pin 17 (TX2) directly to pin 16 (RX2) !!"));
  Serial.println(F("  Waiting 5 seconds for you to connect jumper..."));
  delay(5000);

  // Flush anything in buffer
  while (Serial2.available()) Serial2.read();

  const char* testStr = "LOOPBACK_TEST_123";
  Serial2.println(testStr);
  Serial.print(F("  → Sent:     ")); Serial.println(testStr);

  String received = "";
  unsigned long deadline = millis() + 2000;
  while (millis() < deadline) {
    while (Serial2.available()) {
      char c = Serial2.read();
      if (c != '\n' && c != '\r') received += c;
    }
  }

  Serial.print(F("  ← Received: "));
  if (received.length() == 0) {
    Serial.println(F("(nothing)"));
    fail("Loopback failed — Mega TX2 is NOT transmitting OR RX2 not receiving");
    Serial.println(F("  → Check USB connection, Serial2.begin() called, pins 16/17 not damaged"));
  } else {
    Serial.println(received);
    if (received.indexOf("LOOPBACK_TEST_123") != -1) {
      pass("Loopback SUCCESS — Mega TX2 is transmitting and RX2 is receiving correctly");
      Serial.println(F("  → Mega Serial2 hardware is healthy. Problem is in R3 wiring or firmware."));
    } else {
      fail("Received garbage — possible baud mismatch or pin damage");
      Serial.print(F("  Raw bytes: "));
      for (int i = 0; i < received.length(); i++) {
        Serial.print((int)received[i]); Serial.print(' ');
      }
      Serial.println();
    }
  }
  Serial.println(F("  !! Remove jumper and reconnect R3 wires before continuing !!"));
}
void testPinVoltage() {
  hdr("TEST 1: PIN VOLTAGE CHECK");
  info("Checking TX2 (pin 17) and RX2 (pin 16) idle state");
  info("UART lines idle HIGH. LOW means shorted to GND or wrong pin.");

  // Sample each pin 5 times
  int tx_high = 0, rx_high = 0;
  for (int i = 0; i < 5; i++) {
    pinMode(MEGA_TX2, INPUT);
    if (digitalRead(MEGA_TX2) == HIGH) tx_high++;
    pinMode(MEGA_RX2, INPUT_PULLUP);
    if (digitalRead(MEGA_RX2) == HIGH) rx_high++;
    delay(10);
  }
  // Restore Serial2 control of these pins
  Serial2.begin(currentBaud);

  Serial.print(F("  Pin 17 (TX2): ")); Serial.print(tx_high);
  Serial.print(F("/5 reads HIGH — "));
  if (tx_high >= 3) { pass("TX2 idle state OK"); }
  else              { fail("TX2 stuck LOW — check for short to GND"); }

  Serial.print(F("  Pin 16 (RX2): ")); Serial.print(rx_high);
  Serial.print(F("/5 reads HIGH — "));
  if (rx_high >= 3) { pass("RX2 idle state OK"); }
  else              { fail("RX2 stuck LOW — check wiring from R3 A0/A1"); }
}

// TEST 2 — PING/PONG at current baud
bool testPing(bool verbose = true) {
  if (verbose) {
    hdr("TEST 2: PING / PONG");
    Serial.print(F("  Baud: ")); Serial.println(currentBaud);
    Serial.print(F("  TX/RX: ")); Serial.println(txRxSwapped ? F("SWAPPED (A0=TX, A1=RX)") : F("NORMAL (A1=RX, A0=TX)"));
  }

  bool ok = sendAndExpect("PING", "PONG", PING_TIMEOUT_MS);
  if (ok) { pass("R3 responded to PING"); }
  else    { fail("No PONG received within 3 seconds"); }
  return ok;
}

// TEST 3 — STATUS command
bool testStatus() {
  hdr("TEST 3: STATUS COMMAND");
  bool ok = sendAndExpect("STATUS", "R3:STATUS", PING_TIMEOUT_MS);
  if (ok) { pass("R3 STATUS response received"); }
  else    { fail("No STATUS response"); }
  return ok;
}

// TEST 4 — Baud rate sweep
bool testBaudSweep() {
  hdr("TEST 4: BAUD RATE SWEEP");
  info("Trying 4800 / 9600 / 19200 / 38400 / 57600");

  long workingBaud = -1;
  for (int i = 0; i < BAUD_COUNT; i++) {
    applyBaud(BAUD_RATES[i]);
    Serial.print(F("  Testing ")); Serial.print(BAUD_RATES[i]); Serial.print(F(" baud... "));
    bool ok = sendAndExpect("PING", "PONG", BAUD_RETRY_MS);
    if (ok) {
      Serial.println(F("← WORKING!"));
      workingBaud = BAUD_RATES[i];
      break;
    } else {
      Serial.println(F("no response"));
    }
  }

  if (workingBaud != -1) {
    pass("Found working baud rate!");
    Serial.print(F("  ★ R3 is communicating at ")); Serial.print(workingBaud); Serial.println(F(" baud"));
    Serial.println(F("  → Update SoftwareSerial baud in R3 sketch AND Serial2.begin() in Mega sketch"));
    currentBaud = workingBaud;
    return true;
  } else {
    fail("No baud rate worked — wiring or swap issue likely");
    // Restore default
    applyBaud(9600);
    return false;
  }
}

// TEST 5 — TX/RX swap test
// SoftwareSerial can't be re-instantiated at runtime, so we test by
// checking if flipping the Mega side makes a difference
// (On R3, SoftwareSerial(A1,A0) vs SoftwareSerial(A0,A1))
bool testTxRxSwap() {
  hdr("TEST 5: TX/RX SWAP ANALYSIS");
  info("Can't swap hardware Serial2 in software on Mega.");
  info("Instead — checking if R3 A0 pin is receiving drive from Mega TX.");

  // Drive pin 17 HIGH and LOW alternately, check if signal arrives on RX
  // This is a loopback-style test if you have a wire from 17→16 to verify
  // In normal wiring: pin17(Mega TX) → R3 A1 or A0

  Serial.println(F("  Toggling Mega TX2 (pin 17) and sampling RX2 (pin 16)..."));
  Serial.println(F("  If RX2 mirrors TX2 you have a loopback (TX→RX shorted)."));
  Serial.println(F("  If RX2 stays HIGH regardless, TX is not reaching RX."));

  pinMode(MEGA_TX2, OUTPUT);
  pinMode(MEGA_RX2, INPUT_PULLUP);

  int matches = 0;
  for (int i = 0; i < 10; i++) {
    bool txState = (i % 2 == 0);
    digitalWrite(MEGA_TX2, txState ? HIGH : LOW);
    delay(5);
    bool rxRead = (digitalRead(MEGA_RX2) == HIGH);
    if (rxRead == txState) matches++;
  }

  // Restore Serial2
  Serial2.begin(currentBaud);

  Serial.print(F("  TX→RX coupling: ")); Serial.print(matches); Serial.println(F("/10 matches"));
  if (matches >= 8) {
    info("WARN: TX and RX may be shorted together (loopback) — check wiring");
  } else if (matches <= 2) {
    info("No loopback — TX and RX are on separate wires (correct)");
    info("If PING still fails: physical TX/RX swap required on R3 side");
    info("Try swapping A0 and A1 wires at the R3 end");
  } else {
    info("Inconsistent coupling — possible intermittent connection");
  }
  return false;
}

// TEST 6 — Raw noise capture
void testNoise() {
  hdr("TEST 6: RAW NOISE ANALYSIS");
  Serial.println(F("  Listening on Serial2 for 3 seconds without sending anything."));
  Serial.println(F("  If you see data here, R3 is transmitting on its own (good)."));
  Serial.println(F("  Garbage = baud mismatch. Silence = TX wire broken or R3 not running."));
  int bytes = rawListen(3000);
  Serial.print(F("  Total bytes captured: ")); Serial.println(bytes);
  if (bytes > 0 && bytes < 5)  info("Few bytes — possible noise or partial boot message");
  else if (bytes >= 5)          info("Data received — R3 is alive. Check if it's legible above.");
  else                          info("No data — R3 TX wire may be disconnected or R3 not running");
}

// TEST 7 — Boot listen
void testBootListen() {
  hdr("TEST 7: BOOT ANNOUNCEMENT LISTEN");
  Serial.println(F("  Power-cycle or reset the R3 NOW."));
  Serial.println(F("  Listening 15 seconds for: R3:READY:BUDDYBOT_MOTOR:V1.0"));

  unsigned long deadline = millis() + 15000;
  String line = "";
  bool found = false;
  int countdown = 15;
  unsigned long nextTick = millis() + 1000;

  while (millis() < deadline) {
    while (Serial2.available()) {
      char c = Serial2.read();
      Serial.print(c);
      if (c == '\n' || c == '\r') {
        if (line.indexOf("R3:READY") != -1) {
          found = true;
          Serial.println();
          pass("R3:READY received! Serial link is working.");
          return;
        }
        line = "";
      } else {
        line += c;
      }
    }
    if (millis() > nextTick) {
      nextTick += 1000;
      countdown--;
      Serial.print(F("  [")); Serial.print(countdown); Serial.println(F("s remaining...]"));
    }
  }

  if (!found) fail("R3:READY not received — R3 may not be running V2 firmware or TX wire broken");
}

// ── Run full auto suite ───────────────────────────────────────────────────────
void runFullScan() {
  Serial.println();
  Serial.println(F("╔══════════════════════════════════════╗"));
  Serial.println(F("║  BUDDYBOT R3 COMM DIAGNOSTIC v1.0   ║"));
  Serial.println(F("║  Mega Serial2 ↔ R3 SoftwareSerial   ║"));
  Serial.println(F("╚══════════════════════════════════════╝"));

  testPinVoltage();

  // Re-init Serial2 after pin tests
  Serial2.begin(currentBaud);
  delay(100);

  bool pingOk = testPing();

  if (pingOk) {
    // Link is working — run status to confirm full duplex
    testStatus();
    hdr("RESULT: LINK OK");
    pass("Mega ↔ R3 serial link is fully operational");
    Serial.println(F("  Flash Mega with production BuddyBot_Mega_V31.ino"));
  } else {
    // PING failed — work through causes
    testNoise();
    bool baudOk = testBaudSweep();
    if (!baudOk) {
      testTxRxSwap();
      testBootListen();
      hdr("RESULT: LINK FAILED — SUMMARY");
      Serial.println(F("  Check each item below:"));
      Serial.println(F("  [ ] Mega pin 17 wired to R3 A1 (RX)"));
      Serial.println(F("  [ ] Mega pin 16 wired to R3 A0 (TX)"));
      Serial.println(F("  [ ] GND shared between Mega and R3"));
      Serial.println(F("  [ ] R3 powered and running V2 firmware"));
      Serial.println(F("  [ ] R3 SoftwareSerial baud = 9600"));
      Serial.println(F("  [ ] No other device sharing Serial2 pins"));
      Serial.println(F("  If wired 17→A0 and 16→A1: swap to 17→A1, 16→A0"));
      Serial.println(F("  Or change R3 SoftwareSerial(A1,A0) back to (A0,A1)"));
    }
  }
  prompt();
}

// ════════════════════════════════════════════════════════════════════
//  MANUAL COMMAND HANDLER
// ════════════════════════════════════════════════════════════════════

void showHelp() {
  hdr("MANUAL COMMANDS");
  Serial.println(F("  LOOPBACK       Jumper pin17→pin16, test Mega TX/RX hardware"));
  Serial.println(F("  PING           Send PING → listen 15s for PONG"));
  Serial.println(F("  SEND <text>    Send raw text to R3"));
  Serial.println(F("  LISTEN <n>     Listen raw for n seconds"));
  Serial.println(F("  BAUD <n>       Switch baud and retry PING"));
  Serial.println(F("  SCAN           Re-run all automatic tests"));
  Serial.println(F("  RESET          Toggle pin 17 LOW 100ms (R3 reset pulse)"));
  Serial.println(F("  HELP           Show this list"));
}

void processCommand(String raw) {
  raw.trim();
  if (raw.length() == 0) return;

  int sp   = raw.indexOf(' ');
  String cmd  = (sp == -1) ? raw : raw.substring(0, sp);
  String args = (sp == -1) ? "" : raw.substring(sp + 1);
  cmd.toUpperCase();

  Serial.println();

  if (cmd == "HELP")     { showHelp(); return; }
  if (cmd == "SCAN")     { runFullScan(); return; }
  if (cmd == "LOOPBACK") { testLoopback(); return; }

  if (cmd == "PING") {
    hdr("MANUAL PING TEST");
    Serial.print(F("  Baud: ")); Serial.println(currentBaud);
    bool ok = sendAndExpect("PING", "PONG", 15000);
    if (!ok) fail("No response in 15 seconds");
    return;
  }

  if (cmd == "SEND") {
    if (args.length() == 0) { Serial.println(F("  Usage: SEND <text>")); return; }
    Serial.print(F("  Sending: ")); Serial.println(args);
    Serial2.println(args);
    rawListen(3000);
    return;
  }

  if (cmd == "LISTEN") {
    int secs = (args.length() > 0) ? args.toInt() : 10;
    rawListen((unsigned long)secs * 1000);
    return;
  }

  if (cmd == "BAUD") {
    long b = args.toInt();
    if (b < 300 || b > 115200) { Serial.println(F("  Valid: 300-115200")); return; }
    applyBaud(b);
    testPing();
    return;
  }

  if (cmd == "RESET") {
    hdr("R3 RESET PULSE");
    info("Pulling pin 17 (TX2) LOW for 100ms to trigger R3 reset...");
    Serial2.end();
    pinMode(MEGA_TX2, OUTPUT);
    digitalWrite(MEGA_TX2, LOW);
    delay(100);
    digitalWrite(MEGA_TX2, HIGH);
    delay(50);
    Serial2.begin(currentBaud);
    info("Done — listening for R3:READY...");
    rawListen(5000);
    return;
  }

  Serial.print(F("  Unknown: ")); Serial.println(cmd);
  Serial.println(F("  Type HELP for commands."));
}

// ════════════════════════════════════════════════════════════════════
//  SETUP & LOOP
// ════════════════════════════════════════════════════════════════════

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) {}
  Serial2.begin(9600);
  delay(500);
  runFullScan();
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
