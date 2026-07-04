/*
 * ═══════════════════════════════════════════════════════════════════════
 *  BUDDYBOT — Pico W-2023 UART1 Diagnostic Sketch
 * ═══════════════════════════════════════════════════════════════════════
 *  Board target: rp2040:rp2040:rpipicow (Earle Philhower arduino-pico)
 *
 *  TWO PHASES (both run automatically):
 *
 *  PHASE 1 — LOOPBACK TEST (5 attempts)
 *    Jumper GP4 ↔ GP5 directly on the Pico (no Mega, no divider).
 *    PASS ≥ 3/5 → UART1 hardware is confirmed alive on GP4/GP5.
 *    FAIL       → UART1 not initialising; try reflashing or move to
 *                 GP8/GP9 as a last resort.
 *
 *  PHASE 2 — MEGA LINK PROBE (indefinite)
 *    Remove jumper, wire Mega ↔ Pico:
 *       Mega D18 (TX1) → 1kΩ ── node ── 2kΩ ── GND
 *                                 └─────→ Pico GP5 (RX)
 *       Mega D19 (RX1) ←──── Pico GP4 (TX)
 *       Mega GND ─── common ─── Pico GND
 *    Sends PING_PICO:N every second. Prints anything Mega returns.
 *
 *  USE
 *    1. Flash to Pico W-2023
 *    2. Open Serial Monitor at 115200 (Pico USB)
 *    3. Follow on-screen prompts
 * ═══════════════════════════════════════════════════════════════════════
 */

const uint8_t  PIN_TX = 4;        // GP4 → UART1 TX
const uint8_t  PIN_RX = 5;        // GP5 → UART1 RX
const uint32_t BAUD   = 115200;

enum Phase : uint8_t { PHASE_LOOPBACK, PHASE_MEGA_PROBE };
Phase phase = PHASE_LOOPBACK;

unsigned long tPhaseStart = 0;
unsigned long tLastPing   = 0;
uint32_t      pingCount   = 0;
uint32_t      rxBytes     = 0;
char          rxBuf[128];
uint8_t       rxLen       = 0;

void banner(const char* label, bool pass) {
  Serial.println();
  Serial.println("===============================================");
  Serial.print("  ");
  Serial.print(label);
  Serial.print(": ");
  Serial.println(pass ? "PASS" : "FAIL");
  Serial.println("===============================================");
}

void setup() {
  Serial.begin(115200);
  delay(2500);

  Serial.println();
  Serial.println("+---------------------------------------------+");
  Serial.println("|  BuddyBot Pico W-2023 UART1 Diagnostic      |");
  Serial.println("|  UART1: GP4 (TX) / GP5 (RX) @ 115200        |");
  Serial.println("+---------------------------------------------+");
  Serial.println();

  Serial2.setTX(PIN_TX);
  Serial2.setRX(PIN_RX);
  Serial2.begin(BAUD);
  delay(200);

  Serial.println("[SETUP] Serial2 up on GP4/GP5 @ 115200");
  Serial.println();
  Serial.println("===============================================");
  Serial.println("  PHASE 1 - LOOPBACK TEST");
  Serial.println("===============================================");
  Serial.println("  Jumper GP4 <-> GP5 on the Pico.");
  Serial.println("  Disconnect Mega wiring before this test.");
  Serial.println();
  Serial.println("  Starting in 3 seconds...");
  delay(3000);
  tPhaseStart = millis();
}

void loop() {
  switch (phase) {

    case PHASE_LOOPBACK: {
      static uint8_t  attempt      = 0;
      static uint8_t  successfulRx = 0;
      static uint32_t tLast        = 0;

      if (millis() - tLast < 500) break;
      tLast = millis();

      const char marker[] = "PICO_LB_";
      char probe[32];
      snprintf(probe, sizeof(probe), "%s%u\n", marker, attempt);
      Serial2.print(probe);
      Serial2.flush();

      Serial.print("[TX #");
      Serial.print(attempt);
      Serial.print("] ");
      Serial.print(probe);

      delay(50);

      String received = "";
      while (Serial2.available()) received += (char)Serial2.read();

      if (received.length() > 0) {
        Serial.print("[RX #");
        Serial.print(attempt);
        Serial.print("] ");
        Serial.println(received);
        if (received.indexOf(marker) >= 0) successfulRx++;
      } else {
        Serial.println("[RX] (nothing)");
      }

      attempt++;
      if (attempt >= 5) {
        bool pass = (successfulRx >= 3);
        banner("PHASE 1 LOOPBACK", pass);

        if (!pass) {
          Serial.println("[FAIL] UART1 not looping back on GP4/GP5.");
          Serial.println("  - Jumper GP4<->GP5 fitted?");
          Serial.println("  - Correct arduino-pico core?");
          Serial.println("  - setTX/setRX called before begin()?");
          Serial.println();
          Serial.println("[HOLD] Halted. Fix jumper and reset Pico.");
          while (true) delay(1000);
        }

        Serial.println();
        Serial.println("===============================================");
        Serial.println("  PHASE 2 - MEGA LINK PROBE");
        Serial.println("===============================================");
        Serial.println("  ACTION:");
        Serial.println("  1. Remove GP4<->GP5 jumper");
        Serial.println("  2. Wire Mega <-> Pico:");
        Serial.println("       Mega D18(TX1) -> 1k -> node -> 2k -> GND");
        Serial.println("                                |");
        Serial.println("                                +-> Pico GP5");
        Serial.println("       Mega D19(RX1) <----- Pico GP4");
        Serial.println("       Mega GND ----- common ----- Pico GND");
        Serial.println("  3. Confirm Mega V37 is flashed and running");
        Serial.println();
        Serial.println("  Starting probe in 10 seconds...");
        delay(10000);

        phase = PHASE_MEGA_PROBE;
        tPhaseStart = millis();
        tLastPing = 0;
      }
      break;
    }

    case PHASE_MEGA_PROBE: {
      if (millis() - tLastPing >= 1000) {
        tLastPing = millis();
        pingCount++;
        Serial2.print("PING_PICO:");
        Serial2.println(pingCount);
        Serial.print("[TX #");
        Serial.print(pingCount);
        Serial.print("] PING_PICO:");
        Serial.println(pingCount);
      }

      while (Serial2.available()) {
        char c = Serial2.read();
        rxBytes++;
        if (c == '\n' || c == '\r') {
          if (rxLen > 0) {
            rxBuf[rxLen] = 0;
            Serial.print("[RX] Mega -> ");
            Serial.println(rxBuf);
            rxLen = 0;
          }
        } else if (rxLen < sizeof(rxBuf) - 1) {
          rxBuf[rxLen++] = c;
        }
      }

      static uint32_t tHb = 0;
      if (millis() - tHb >= 5000) {
        tHb = millis();
        Serial.print("[STATUS] pings=");
        Serial.print(pingCount);
        Serial.print(" bytes_rx=");
        Serial.print(rxBytes);
        Serial.print(" uptime=");
        Serial.print(millis() / 1000);
        Serial.println("s");

        if (rxBytes == 0 && millis() - tPhaseStart > 15000) {
          Serial.println();
          Serial.println("!!! NO BYTES RECEIVED FROM MEGA IN 15+ SEC !!!");
          Serial.println("  Check in this order:");
          Serial.println("   1. Mega D18 idle voltage ~5.0V");
          Serial.println("   2. Pico GP5 idle voltage ~3.3V");
          Serial.println("   3. Mega GND <-> Pico GND continuity");
          Serial.println("   4. Mega V37 sketch running (power LED, USB output)");
          Serial.println("   5. 1k + 2k divider fitted correctly");
        }
      }
      break;
    }
  }
}
