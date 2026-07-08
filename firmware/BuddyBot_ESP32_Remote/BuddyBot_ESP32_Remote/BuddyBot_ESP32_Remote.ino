/*
 * BuddyBot ESP32-WROOM Multi-Mode Remote Control  V2
 * Hardware: Keyestudio KS0413 ESP32 + XC3726 OLED + XC3736 Encoder + 6 Buttons
 * Dual Profile: Robot Drive Mode <-> Pico W Game Controller Mode
 *
 * Auto-connects to Pico W softAP "BuddyBot-RC" on 192.168.4.1
 * No router needed — direct ESP32 → Pico W link.
 *
 * V2 fixes:
 *   - Auto-connect to Pico W softAP (fixed SSID/pass/IP)
 *   - Button RELEASE sends MOTOR:S (auto-stop when you let go)
 *   - Per-button debounce (no shared timer blocking inputs)
 *   - WiFi reconnect with exponential backoff
 *   - Connection status on OLED with signal strength
 */

#include <WiFi.h>
#include <HTTPClient.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1351.h>
#include <SPI.h>

// ── Auto-connect to Pico W softAP (matches RC_AP_SSID/RC_AP_PASS in PicoW_Dash) ──
const char* ssid     = "BuddyBot-RC";
const char* password = "BuddyBot2025";
const String robotIP = "192.168.4.1";   // Pico W softAP gateway — always this IP
// ── XC3726 1.54" Color OLED (SSD1351 SPI) ───────────────────────────────────
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 128
#define OLED_CS_PIN    5
#define OLED_DC_PIN   21
#define OLED_RST_PIN  22

Adafruit_SSD1351 tft = Adafruit_SSD1351(SCREEN_WIDTH, SCREEN_HEIGHT, &SPI, OLED_CS_PIN, OLED_DC_PIN, OLED_RST_PIN);

// ── RGB565 Palette ──────────────────────────────────────────────────────────
#define COL_BG      0x0000
#define COL_CYAN    0x07FF
#define COL_GREEN   0x07E0
#define COL_RED     0xF800
#define COL_YELLOW  0xFFE0
#define COL_MAGENTA 0xF81F
#define COL_WHITE   0xFFFF
#define COL_DGRAY   0x4208
#define COL_ORANGE  0xFD00

// ── XC3736 Rotary Encoder ───────────────────────────────────────────────────
#define ENC_CLK 32
#define ENC_DT  33
#define ENC_SW  25

// ── 6x Tactile Buttons ─────────────────────────────────────────────────────
#define BTN_FWD   13   // D-Pad Up
#define BTN_BWD    4   // D-Pad Down
#define BTN_LEFT  16   // D-Pad Left
#define BTN_RIGHT 17   // D-Pad Right
#define BTN_STOP   2   // Fire / E-Stop
#define BTN_MODE  15   // Profile Toggle
const uint8_t btnPins[] = {BTN_FWD, BTN_BWD, BTN_LEFT, BTN_RIGHT, BTN_STOP, BTN_MODE};
#define NUM_BTNS 6

// ── Per-button debounce state (fixes shared-timer missed inputs) ────────────
bool     btnState[NUM_BTNS]     = {false};  // true = currently pressed
uint32_t btnDebounce[NUM_BTNS]  = {0};
#define  DEBOUNCE_MS  60

// ── Operating modes ─────────────────────────────────────────────────────────
enum OpMode { MODE_DRIVE, MODE_GAME };
OpMode mode = MODE_DRIVE;

// Drive mode: button press sends command, button RELEASE sends MOTOR:S
const char* driveCmds[] = {"MOTOR:F","MOTOR:B","MOTOR:L","MOTOR:R","MOTOR:S",""};
// Game mode: button press sends command (no auto-stop needed)
const char* gameCmds[]  = {"GAME:UP","GAME:DOWN","GAME:LEFT","GAME:RIGHT","GAME:FIRE",""};

// ── Speed control via encoder ───────────────────────────────────────────────
int  lastClk;
int  speedIdx = 1;
const char* speedCmds[]   = {"SPEED:SLOW","SPEED:NORMAL","SPEED:FAST"};
const char* speedLabels[] = {"SLOW (130)","NORMAL (200)","FAST (240)"};

// ── Display state ───────────────────────────────────────────────────────────
String lastCmd = "IDLE";
bool   wifiWasConnected = false;
uint32_t lastWifiCheck  = 0;
uint32_t reconnectDelay = 2000;
// ═══════════════════════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  for (int i = 0; i < NUM_BTNS; i++) pinMode(btnPins[i], INPUT_PULLUP);
  pinMode(ENC_CLK, INPUT);
  pinMode(ENC_DT,  INPUT);
  pinMode(ENC_SW,  INPUT_PULLUP);
  lastClk = digitalRead(ENC_CLK);

  tft.begin();
  tft.fillScreen(COL_BG);
  tft.setRotation(0);
  drawUI();

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  drawStatus("CONNECTING...", COL_YELLOW);
}

void loop() {
  handleWiFi();
  readEncoder();
  readButtons();
}

// ═══════════════════════════════════════════════════════════════════════════
//  WiFi — auto-connect with exponential backoff
// ═══════════════════════════════════════════════════════════════════════════
void handleWiFi() {
  if (millis() - lastWifiCheck < 1000) return;
  lastWifiCheck = millis();

  bool connected = (WiFi.status() == WL_CONNECTED);
  if (connected && !wifiWasConnected) {
    wifiWasConnected = true;
    reconnectDelay = 2000;
    drawStatus("LINKED", COL_GREEN);
    int rssi = WiFi.RSSI();
    drawSignal(rssi);
  } else if (!connected && wifiWasConnected) {
    wifiWasConnected = false;
    drawStatus("LOST — RETRY", COL_RED);
  }
  if (!connected) {
    static uint32_t lastReconnect = 0;
    if (millis() - lastReconnect > reconnectDelay) {
      lastReconnect = millis();
      WiFi.disconnect();
      WiFi.begin(ssid, password);
      drawStatus("RECONNECTING", COL_YELLOW);
      reconnectDelay = min(reconnectDelay * 2, 30000UL);
    }
  }
}
// ═══════════════════════════════════════════════════════════════════════════
//  Buttons — per-button debounce + auto-stop on release (Drive mode)
// ═══════════════════════════════════════════════════════════════════════════
void readButtons() {
  uint32_t now = millis();
  for (int i = 0; i < NUM_BTNS; i++) {
    if (now - btnDebounce[i] < DEBOUNCE_MS) continue;
    bool pressed = (digitalRead(btnPins[i]) == LOW);
    if (pressed == btnState[i]) continue;   // no change
    btnDebounce[i] = now;
    btnState[i] = pressed;

    if (i == 5) {   // BTN_MODE — toggle on press only
      if (pressed) {
        mode = (mode == MODE_DRIVE) ? MODE_GAME : MODE_DRIVE;
        sendCmd(mode == MODE_GAME ? "GAME:START" : "GAME:EXIT");
        drawUI();
      }
      continue;
    }

    if (pressed) {
      // Button DOWN — send the appropriate command
      sendCmd(mode == MODE_DRIVE ? driveCmds[i] : gameCmds[i]);
    } else if (mode == MODE_DRIVE && i < 4) {
      // Button UP in drive mode — auto-stop (buttons 0-3 are direction)
      sendCmd("MOTOR:S");
    }
  }
}

// ═══════════════════════════════════════════════════════════════════════════
//  Rotary Encoder — speed in Drive, scroll/paddle in Game
// ═══════════════════════════════════════════════════════════════════════════
void readEncoder() {
  int clk = digitalRead(ENC_CLK);
  if (clk != lastClk && clk == LOW) {
    bool cw = (digitalRead(ENC_DT) != clk);
    if (mode == MODE_DRIVE) {
      speedIdx = constrain(speedIdx + (cw ? 1 : -1), 0, 2);
      sendCmd(speedCmds[speedIdx]);
      drawSpeedLabel();
    } else {
      sendCmd(cw ? "GAME:ENC_CW" : "GAME:ENC_CCW");
    }
  }
  lastClk = clk;

  // Encoder click
  static uint32_t encDebounce = 0;
  if (digitalRead(ENC_SW) == LOW && millis() - encDebounce > DEBOUNCE_MS) {
    encDebounce = millis();
    sendCmd(mode == MODE_DRIVE ? "MOTOR:S" : "GAME:SELECT");
  }
}
// ═══════════════════════════════════════════════════════════════════════════
//  HTTP command sender — fire-and-forget, 200ms timeout
// ═══════════════════════════════════════════════════════════════════════════
void sendCmd(const char* cmd) {
  if (WiFi.status() != WL_CONNECTED || !cmd || !cmd[0]) return;
  HTTPClient http;
  String url = "http://" + robotIP + "/cmd?c=" + cmd;
  http.begin(url);
  http.setTimeout(200);
  int code = http.GET();
  http.end();
  if (code > 0) {
    lastCmd = cmd;
    drawLastCmd();
  }
}

// ═══════════════════════════════════════════════════════════════════════════
//  OLED Display
// ═══════════════════════════════════════════════════════════════════════════
void drawUI() {
  tft.fillScreen(COL_BG);
  tft.setTextSize(1);
  if (mode == MODE_DRIVE) {
    tft.setTextColor(COL_CYAN);
    tft.setCursor(10, 5); tft.print("-- ROBOT DRIVE --");
    tft.setTextColor(COL_WHITE);
    tft.setCursor(5, 55); tft.print("Speed:");
    drawSpeedLabel();
  } else {
    tft.setTextColor(COL_MAGENTA);
    tft.setCursor(10, 5); tft.print("-- PICO GAMEPAD --");
    tft.setTextColor(COL_GREEN);
    tft.setCursor(5, 55); tft.print("D-PAD + FIRE READY");
  }
  drawStatus(wifiWasConnected ? "LINKED" : "SEARCHING", wifiWasConnected ? COL_GREEN : COL_YELLOW);
  drawLastCmd();
}
void drawStatus(const char* msg, uint16_t col) {
  tft.fillRect(0, 25, 128, 12, COL_BG);
  tft.setTextSize(1); tft.setTextColor(col);
  tft.setCursor(5, 25); tft.print("Link: "); tft.print(msg);
}

void drawSpeedLabel() {
  tft.fillRect(5, 70, 120, 12, COL_BG);
  tft.setTextSize(1); tft.setTextColor(COL_YELLOW);
  tft.setCursor(5, 70); tft.print(speedLabels[speedIdx]);
}

void drawLastCmd() {
  tft.fillRect(0, 110, 128, 18, COL_BG);
  tft.setTextSize(1); tft.setTextColor(COL_WHITE);
  tft.setCursor(5, 112); tft.print("TX: ");
  tft.setTextColor(COL_CYAN);
  tft.print(lastCmd);
}

void drawSignal(int rssi) {
  tft.fillRect(0, 40, 128, 12, COL_BG);
  tft.setTextSize(1); tft.setTextColor(COL_DGRAY);
  tft.setCursor(5, 40);
  tft.print("RSSI: "); tft.print(rssi); tft.print(" dBm");
  // Simple bar
  int bars = rssi > -50 ? 4 : rssi > -65 ? 3 : rssi > -75 ? 2 : 1;
  for (int b = 0; b < 4; b++) {
    uint16_t c = (b < bars) ? COL_GREEN : COL_DGRAY;
    tft.fillRect(100 + b * 6, 43 - b * 2, 4, 4 + b * 2, c);
  }
}