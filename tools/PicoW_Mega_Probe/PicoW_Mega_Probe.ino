/*
 * ═══════════════════════════════════════════════════════════════════════
 *  BUDDYBOT — Pico W-2023 <-> Mega V37 Link Probe
 * ═══════════════════════════════════════════════════════════════════════
 *  Board target : rp2040:rp2040:rpipicow  (Earle Philhower arduino-pico)
 *  UART1        : GP4 (TX) / GP5 (RX) @ 115200 baud
 *
 *  PURPOSE
 *  ─────────
 *  Skips loopback. Assumes UART1 hardware is already proven working.
 *  Focuses entirely on diagnosing what the Mega is (or is not) sending.
 *
 *  WIRING REQUIRED (Mega KS0509 <-> Pico W-2023):
 *  ─────────────────────────────────────────────
 *      Mega D18 (TX1) --> 1kΩ --+-- 2kΩ --> GND
 *                                |
 *                                +-------> Pico GP5 (RX)
 *      Mega D19 (RX1) <--- direct wire ---- Pico GP4 (TX)
 *      Mega GND      <=== common ground === Pico GND
 *
 *  BEHAVIOUR
 *  ──────────
 *  Every 1000 ms:  transmits  "PING_PICO:<n>\n" on UART1
 *  Every 5000 ms:  prints a status line (pings sent, bytes received)
 *  Continuously:   parses incoming UART1 lines and classifies them
 *
 *  DIAGNOSTIC CLASSIFIER
 *  ──────────────────────
 *  For each line received from the Mega, this sketch classifies it as
 *  one of the following, giving you an instant read on Mega V37 health:
 *
 *      [BOOT]   = "SYSTEM|READY|V37.0|END"      -> Mega just booted
 *      [STAT]   = "STAT:..."                    -> Sensor telemetry OK
 *      [US]     = "US:..."                      -> Ultrasonic OK
 *      [PONG]   = "PONG_PICO:..."               -> Pico link ACK
 *      [ACK]    = "ACK|...|END"                 -> Command ACK
 *      [ALERT]  = "ALERT:..."                   -> Sensor alert
 *      [DBG]    = "DBG:..."                     -> Debug message
 *      [ECHO]   = "PING_PICO:..." echo of us    -> Self-hearing (bad wiring)
 *      [OTHER]  = anything else                 -> Unclassified line
 *      [GARBAGE]= mostly non-printable bytes    -> Baud or ground fault
 *
 *  ANY [BOOT] / [STAT] / [US] confirms Mega V37 is running and
 *  transmitting normally over UART1.
 *
 *  ═══════════════════════════════════════════════════════════════════════
 */

const uint8_t  PIN_TX  = 4;         // GP4 -> UART1 TX -> Mega D19 (RX1)
const uint8_t  PIN_RX  = 5;         // GP5 <- UART1 RX <- Mega D18 (TX1) via divider
const uint32_t BAUD    = 115200;

// ── Rolling counters ──────────────────────────────────────────────────
uint32_t pingCount    = 0;
uint32_t linesRx      = 0;
uint32_t bytesRx      = 0;
uint32_t garbageBytes = 0;
uint32_t bootSeen     = 0;
uint32_t statSeen     = 0;
uint32_t usSeen       = 0;
uint32_t pongSeen     = 0;
uint32_t ackSeen      = 0;
uint32_t alertSeen    = 0;
uint32_t dbgSeen      = 0;
uint32_t echoSeen     = 0;
uint32_t otherSeen    = 0;

// ── Line buffer ───────────────────────────────────────────────────────
char     rxBuf[256];
uint16_t rxLen = 0;

unsigned long tStart    = 0;
unsigned long tLastPing = 0;
unsigned long tLastHb   = 0;
unsigned long tFirstRx  = 0;

// ─────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(2500);

  Serial.println();
  Serial.println("+-------------------------------------------------+");
  Serial.println("|  BuddyBot Pico W <-> Mega V37 Link Probe        |");
  Serial.println("|  UART1: GP4 (TX) / GP5 (RX) @ 115200            |");
  Serial.println("+-------------------------------------------------+");
  Serial.println();
  Serial.println("[WIRING] Confirm the following BEFORE proceeding:");
  Serial.println("  Mega D18 -> 1k -> node -> 2k -> GND");
  Serial.println("                     |");
  Serial.println("                     +---> Pico GP5");
  Serial.println("  Mega D19 <---- Pico GP4");
  Serial.println("  Mega GND ===== Pico GND");
  Serial.println();
  Serial.println("[SETUP] Bringing up UART1 on GP4/GP5...");

  // Explicit pin mux MUST come before begin()
  Serial2.setTX(PIN_TX);
  Serial2.setRX(PIN_RX);
  Serial2.begin(BAUD);
  delay(200);

  Serial.println("[SETUP] UART1 up. Starting probe.");
  Serial.println();
  Serial.println("Legend: [BOOT][STAT][US][PONG][ACK][ALERT][DBG][ECHO][OTHER][GARBAGE]");
  Serial.println("---------------------------------------------------");

  tStart = millis();
}

// ─────────────────────────────────────────────────────────────────────
// Line classifier — call once per complete line received
void classifyLine(const char* line, uint16_t len) {
  linesRx++;

  // Empty
  if (len == 0) return;

  // Count printable ratio to detect garbage
  uint16_t printable = 0;
  for (uint16_t i = 0; i < len; i++) {
    char c = line[i];
    if (c >= 0x20 && c <= 0x7E) printable++;
  }
  if (printable < (len / 2)) {
    garbageBytes += len;
    Serial.print("[GARBAGE] len=");
    Serial.print(len);
    Serial.print("  bytes=");
    for (uint16_t i = 0; i < len && i < 16; i++) {
      Serial.print("0x");
      if ((uint8_t)line[i] < 0x10) Serial.print('0');
      Serial.print((uint8_t)line[i], HEX);
      Serial.print(' ');
    }
    Serial.println();
    return;
  }

  // Classify
  const char* tag = "OTHER";
  if      (strncmp(line, "SYSTEM|READY|",     13) == 0) { tag = "BOOT";  bootSeen++;  }
  else if (strncmp(line, "STAT:",              5) == 0) { tag = "STAT";  statSeen++;  }
  else if (strncmp(line, "US:",                3) == 0) { tag = "US";    usSeen++;    }
  else if (strncmp(line, "PONG_PICO:",        10) == 0) { tag = "PONG";  pongSeen++;  }
  else if (strncmp(line, "ACK|",               4) == 0) { tag = "ACK";   ackSeen++;   }
  else if (strncmp(line, "ALERT:",             6) == 0) { tag = "ALERT"; alertSeen++; }
  else if (strncmp(line, "DBG:",               4) == 0) { tag = "DBG";   dbgSeen++;   }
  else if (strncmp(line, "PING_PICO:",        10) == 0) { tag = "ECHO";  echoSeen++;  }
  else                                                  { tag = "OTHER"; otherSeen++; }

  Serial.print("[");
  Serial.print(tag);
  Serial.print("] ");
  Serial.println(line);
}

// ─────────────────────────────────────────────────────────────────────
void printStatus() {
  unsigned long up = (millis() - tStart) / 1000;
  Serial.println();
  Serial.print("[STATUS @ ");
  Serial.print(up);
  Serial.println("s]");
  Serial.print("  TX pings   = "); Serial.println(pingCount);
  Serial.print("  RX lines   = "); Serial.println(linesRx);
  Serial.print("  RX bytes   = "); Serial.println(bytesRx);
  Serial.print("  Garbage    = "); Serial.println(garbageBytes);
  Serial.println("  Classified:");
  Serial.print("    BOOT     = "); Serial.println(bootSeen);
  Serial.print("    STAT     = "); Serial.println(statSeen);
  Serial.print("    US       = "); Serial.println(usSeen);
  Serial.print("    PONG     = "); Serial.println(pongSeen);
  Serial.print("    ACK      = "); Serial.println(ackSeen);
  Serial.print("    ALERT    = "); Serial.println(alertSeen);
  Serial.print("    DBG      = "); Serial.println(dbgSeen);
  Serial.print("    ECHO     = "); Serial.println(echoSeen);
  Serial.print("    OTHER    = "); Serial.println(otherSeen);

  // Diagnostic verdict
  Serial.println();
  Serial.print("  VERDICT: ");
  if (bytesRx == 0 && up > 15) {
    Serial.println("NO DATA - check wiring / Mega V37 running / common GND");
  } else if (garbageBytes > bytesRx / 4 && bytesRx > 20) {
    Serial.println("HIGH GARBAGE RATIO - likely baud mismatch or bad GND");
  } else if (echoSeen > 0 && (statSeen + usSeen + bootSeen) == 0) {
    Serial.println("ONLY ECHOES - Pico hearing itself. Check wiring for TX/RX short");
  } else if (statSeen > 0 || usSeen > 0 || bootSeen > 0) {
    Serial.println("LINK HEALTHY - Mega V37 transmitting normally");
  } else {
    Serial.println("PARTIAL - some data but no V37 telemetry yet");
  }
  Serial.println("---------------------------------------------------");
}

// ─────────────────────────────────────────────────────────────────────
void loop() {
  unsigned long now = millis();

  // ── TX: PING every 1 second ─────────────────────────────────────────
  if (now - tLastPing >= 1000) {
    tLastPing = now;
    pingCount++;
    Serial2.print("PING_PICO:");
    Serial2.println(pingCount);
  }

  // ── RX: read available UART1 bytes ──────────────────────────────────
  while (Serial2.available()) {
    char c = Serial2.read();
    bytesRx++;
    if (tFirstRx == 0) {
      tFirstRx = now;
      Serial.print("[FIRST RX] First byte received at t=");
      Serial.print((now - tStart));
      Serial.println("ms");
    }
    if (c == '\n' || c == '\r') {
      if (rxLen > 0) {
        rxBuf[rxLen] = 0;
        classifyLine(rxBuf, rxLen);
        rxLen = 0;
      }
    } else if (rxLen < sizeof(rxBuf) - 1) {
      rxBuf[rxLen++] = c;
    } else {
      // Overflow — reset buffer, count as garbage
      garbageBytes += rxLen;
      rxLen = 0;
    }
  }

  // ── Status heartbeat every 5 seconds ────────────────────────────────
  if (now - tLastHb >= 5000) {
    tLastHb = now;
    printStatus();
  }
}
