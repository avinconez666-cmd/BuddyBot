/*
 * ════════════════════════════════════════════════════════════════════
 *  BUDDYBOT — Raspberry Pi Pico 2  ·  Orbital HMI Dashboard  ·  V1.0
 * ════════════════════════════════════════════════════════════════════
 *
 *  HARDWARE
 *  ─────────────────────────────────────────────────────────────────
 *  Board   : Raspberry Pi Pico 2 (RP2350) via arduino-pico package
 *  Display : MSP4031  ST7796S  4.0" SPI TFT  320×480
 *            (landscape rotation = 480×320 logical)
 *  Touch   : FT6336U  Capacitive I2C  (address 0x38)
 *
 *  REPLACES: UNO R4 WiFi dashboard — identical Mega Serial1 protocol.
 *  No changes needed to BuddyBot_Mega_V29 firmware.
 *
 *  LIBRARY REQUIREMENTS  (Arduino Library Manager)
 *  ─────────────────────────────────────────────────────────────────
 *  1. TFT_eSPI by Bodmer
 *     → Copy User_Setup.h (in this sketch folder) into the library root
 *       before compiling.  It replaces the generic setup file.
 *  2. arduino-pico board package by Earle F. Philhower III
 *     → Boards Manager URL: https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json
 *     → Board: "Raspberry Pi Pico 2"
 *     → USB Stack: "Adafruit TinyUSB" or "Pico SDK"
 *
 *  WIRING  (Pico 2 GP → Module Pin label)
 *  ─────────────────────────────────────────────────────────────────
 *  DISPLAY (SPI0)
 *    GP18 → SCK    (7)      GP19 → SDI/MOSI  (6)
 *    GP16 → SDO/MISO(9)     GP17 → LCD_CS    (3)
 *    GP20 → LCD_RST(4)      GP21 → LCD_RS    (5)
 *    GP22 → LED    (8)   ← tie to 3.3V for always-on backlight
 *    3.3V → VCC    (1)      GND  → GND       (2)
 *
 *  TOUCH (I2C1)
 *    GP26 → CTP_SDA(12)     GP27 → CTP_SCL  (10)
 *    GP28 → CTP_INT(13)     GP29 → CTP_RST  (11)
 *
 *  MEGA SERIAL LINK  (replaces R4 Serial1 connection)
 *    GP0  (TX) → Mega pin 19 (Serial1 RX)   [3.3V — direct OK]
 *    GP1  (RX) ← Mega pin 18 (Serial1 TX)   [⚠ 5V! Use divider]
 *                 Voltage divider: 1kΩ Mega→junction, 2kΩ junction→GND
 *                 Junction → GP1.  Gives 3.33 V — safe for RP2350.
 *    GND       → Mega GND   (shared ground is mandatory)
 *
 * ════════════════════════════════════════════════════════════════════
 */

#include <TFT_eSPI.h>
#include <Wire.h>
#include <SPI.h>

// ════════════════════════════════════════════════════════════════════
//  COLOUR PALETTE  (BuddyBot Orbital HMI theme — RGB565)
// ════════════════════════════════════════════════════════════════════
#define C_BG     0x0209u   // Deep-space background
#define C_SURF   0x0862u   // Raised surface
#define C_SURF2  0x10A3u   // Card highlight
#define C_LINE   0x2124u   // Subtle hairline
#define C_CYAN   0x07FFu   // Electric cyan  (primary accent)
#define C_DCYAN  0x0398u   // Dim cyan
#define C_MINT   0x07E4u   // Mint green (success / normal mode)
#define C_AMBER  0xFD20u   // Amber (warning / bodyguard mode)
#define C_CORAL  0xFB0Cu   // Coral (danger / E-stop)
#define C_MAG    0xF81Fu   // Magenta (unhinged mode)
#define C_PURP   0x801Fu   // Purple (party mode)
#define C_YLLOW  0xFFE0u   // Yellow (stars in counting game)
#define C_WHITE  0xFFFFu
#define C_LGRAY  0x8C71u   // Light gray text
#define C_MGRAY  0x528Au   // Medium gray
#define C_BLACK  0x0000u
#define C_RED    0xF800u
#define C_GREEN  0x07E0u

// ════════════════════════════════════════════════════════════════════
//  LAYOUT  (landscape 480×320)
// ════════════════════════════════════════════════════════════════════
#define SCR_W  480
#define SCR_H  320
#define HDR_H   42   // header bar height
#define FTR_H   38   // footer bar height
#define CNT_Y  HDR_H
#define CNT_H  (SCR_H - HDR_H - FTR_H)   // 240 px usable

// ════════════════════════════════════════════════════════════════════
//  HARDWARE OBJECTS
// ════════════════════════════════════════════════════════════════════
TFT_eSPI tft = TFT_eSPI();

// ── Pins ──────────────────────────────────────────────────────────
#define PIN_BL      22   // backlight (HIGH = on)
#define PIN_RST     20   // display hardware reset (mirrors TFT_RST in User_Setup.h)
#define PIN_CTP_SDA 26
#define PIN_CTP_SCL 27
#define PIN_CTP_INT 28
#define PIN_CTP_RST 29

// ── Buzzer (optional) — connect a passive buzzer to GP2 + GND ──────
#define PIN_BUZZ    2
// Set HAVE_BUZZER 1 if a buzzer is wired, 0 for silent visual-only mode
#define HAVE_BUZZER 0

// ════════════════════════════════════════════════════════════════════
//  FT6336U CAPACITIVE TOUCH DRIVER  (no external library needed)
// ════════════════════════════════════════════════════════════════════
#define CTP_ADDR  0x38
struct Touch { int16_t x, y; bool pressed; };

Touch readTouch() {
  Touch t = {0, 0, false};
  Wire.beginTransmission(CTP_ADDR);
  Wire.write(0x02);                        // TD_STATUS register
  if (Wire.endTransmission(false) != 0) return t;
  Wire.requestFrom(CTP_ADDR, 6);
  if (Wire.available() < 6) return t;
  uint8_t n  = Wire.read() & 0x0F;        // number of touch points
  uint8_t xH = Wire.read();
  uint8_t xL = Wire.read();
  uint8_t yH = Wire.read();
  uint8_t yL = Wire.read();
  Wire.read();                             // weight / misc
  if (n == 0 || n > 2) return t;
  // FT6336U reports in native portrait coords (x 0-319, y 0-479).
  // Transform to landscape rotation=1: display_x=raw_y, display_y=319-raw_x
  int16_t rx = ((xH & 0x0F) << 8) | xL;
  int16_t ry = ((yH & 0x0F) << 8) | yL;
  t.x = ry;
  t.y = 319 - rx;
  t.pressed = true;
  return t;
}

// ════════════════════════════════════════════════════════════════════
//  DATA STRUCTURES
// ════════════════════════════════════════════════════════════════════
struct Telemetry {
  int   gas    = 0;
  float temp   = 0.0f;
  float hum    = 0.0f;
  bool  haz    = false;
  bool  pir    = false;
  bool  tilt   = false;
  bool  flame  = false;
  bool  ir     = false;
  float volt   = 8.4f;
  int   pct    = 100;
  float amps   = 0.0f;
  float boost  = 0.0f;
  long  dFront = -1, dRear = -1, dLeft = -1, dRight = -1;
  bool  estop  = false;
  bool  autoM  = false;
  String mode  = "NORMAL";
  String fw    = "";
  bool  r3ok   = false;
  bool  espok  = false;
  bool  s9ok   = false;
} telem;

struct SensorFlags {
  bool dht=true, light=true, sound=true, gas=true, flame=true;
  bool pir=false, tilt=true, ir=true, us=true, current=true, gps=true;
} sens;

// ════════════════════════════════════════════════════════════════════
//  SCREEN STATE MACHINE
// ════════════════════════════════════════════════════════════════════
enum Screen {
  SCR_BOOT,
  SCR_MAIN,
  SCR_RADAR,
  SCR_GAMES,
  SCR_GAME_COLOR,
  SCR_GAME_SHAPE,
  SCR_GAME_COUNT,
  SCR_INFO,
  SCR_SENSORS
};
Screen currentScreen = SCR_BOOT;
bool   screenDirty   = true;
bool   alertActive   = false;
String alertTitle    = "";
String alertMsg      = "";
uint16_t alertColor  = C_CORAL;
unsigned long alertTs = 0;

// ════════════════════════════════════════════════════════════════════
//  MEGA LINK STATE
// ════════════════════════════════════════════════════════════════════
#define MEGA_SERIAL     Serial1
#define MEGA_BAUD       115200
String        megaBuf     = "";
bool          megaLinked  = false;
unsigned long lastMegaRx  = 0;
unsigned long lastPing    = 0;
uint16_t      pingSeq     = 0;

// ════════════════════════════════════════════════════════════════════
//  GAME STATE
// ════════════════════════════════════════════════════════════════════
const uint16_t GCOLS[]   = {C_RED, C_GREEN, 0x001Fu, C_YLLOW};
const char*    GCOLNAMES[]= {"RED", "GREEN", "BLUE", "YELLOW"};
int gTarget = 0, gScore = 0;
unsigned long gFeedbackUntil = 0;
bool gFeedbackCorrect = false;

// ════════════════════════════════════════════════════════════════════
//  TIMING
// ════════════════════════════════════════════════════════════════════
unsigned long lastTelemDraw = 0;
unsigned long lastTouchMs   = 0;


// ════════════════════════════════════════════════════════════════════
//  UI HELPERS
// ════════════════════════════════════════════════════════════════════

// Simple tone wrapper — silent when no buzzer fitted
void boop(int freq, int ms) {
#if HAVE_BUZZER
  tone(PIN_BUZZ, freq, ms);
#endif
}

void drawButton(int x, int y, int w, int h,
                uint16_t bg, uint16_t border, const char* label,
                uint8_t sz = 2) {
  tft.fillRoundRect(x, y, w, h, 8, bg);
  tft.drawRoundRect(x, y, w, h, 8, border);
  tft.setTextColor(border, bg);
  tft.setTextSize(sz);
  int tw = tft.textWidth(label);
  int th = sz * 8;
  tft.setCursor(x + (w - tw) / 2, y + (h - th) / 2);
  tft.print(label);
}

bool hit(const Touch& t, int x, int y, int w, int h) {
  return t.pressed && t.x >= x && t.x < x + w && t.y >= y && t.y < y + h;
}

// ── Header — drawn on every screen ───────────────────────────────────
void drawHeader(const char* title) {
  tft.fillRect(0, 0, SCR_W, HDR_H, C_SURF);
  tft.drawFastHLine(0, HDR_H - 1, SCR_W, C_CYAN);

  // Left: mode badge
  uint16_t mc = C_CYAN;
  if      (telem.estop)                     mc = C_CORAL;
  else if (telem.mode == "BODYGUARD")       mc = C_AMBER;
  else if (telem.mode == "DOG")             mc = C_MINT;
  else if (telem.mode == "UNHINGED")        mc = C_MAG;
  else if (telem.mode == "PARTY")           mc = C_PURP;

  const char* mstr = telem.estop ? "E-STOP" : telem.mode.c_str();
  tft.fillRect(0, 0, 108, HDR_H, mc);
  tft.setTextColor(C_BLACK, mc);
  tft.setTextSize(2);
  int tw = tft.textWidth(mstr);
  tft.setCursor((108 - tw) / 2, (HDR_H - 16) / 2);
  tft.print(mstr);

  // Centre: screen title
  tft.setTextColor(C_WHITE, C_SURF);
  tft.setTextSize(2);
  tw = tft.textWidth(title);
  tft.setCursor((SCR_W - tw) / 2, (HDR_H - 16) / 2);
  tft.print(title);

  // Right: battery + link dot
  char bat[18];
  snprintf(bat, sizeof(bat), "%d%%  %.1fV", telem.pct, telem.volt);
  uint16_t bc = telem.pct > 50 ? C_MINT : (telem.pct > 20 ? C_AMBER : C_CORAL);
  tft.setTextSize(1);
  tft.setTextColor(bc, C_SURF);
  tw = tft.textWidth(bat);
  tft.setCursor(SCR_W - tw - 14, (HDR_H - 8) / 2);
  tft.print(bat);
  tft.fillCircle(SCR_W - 7, HDR_H / 2, 4, megaLinked ? C_MINT : C_CORAL);
}

// ── Footer — E-STOP always reachable ─────────────────────────────────
void drawFooter() {
  int fy = SCR_H - FTR_H;
  tft.fillRect(0, fy, SCR_W, FTR_H, C_SURF);
  tft.drawFastHLine(0, fy, SCR_W, C_LINE);

  uint16_t ec = telem.estop ? C_AMBER : C_CORAL;
  const char* el = telem.estop ? "CLR STP" : "E-STOP";
  drawButton(4, fy + 4, 88, FTR_H - 8, ec, C_WHITE, el, 1);

  // Hazard icons — inline right of E-STOP
  int ix = 100;
  tft.setTextSize(1);
  if (telem.flame) { tft.setTextColor(C_CORAL, C_SURF); tft.setCursor(ix, fy + 13); tft.print("FLAME"); ix += 46; }
  if (telem.tilt)  { tft.setTextColor(C_AMBER, C_SURF); tft.setCursor(ix, fy + 13); tft.print("TILT");  ix += 36; }
  if (telem.haz)   { tft.setTextColor(C_AMBER, C_SURF); tft.setCursor(ix, fy + 13); tft.print("HAZ");  }

  // Right: Mega link age
  tft.setTextSize(1);
  if (megaLinked) {
    unsigned long age = (millis() - lastMegaRx) / 1000;
    char buf[22];
    if (age < 5) {
      snprintf(buf, sizeof(buf), "Mega: LIVE");
      tft.setTextColor(C_MINT, C_SURF);
    } else {
      snprintf(buf, sizeof(buf), "Mega: %lus", age);
      tft.setTextColor(C_AMBER, C_SURF);
    }
    tft.setCursor(SCR_W - tft.textWidth(buf) - 4, fy + 13);
    tft.print(buf);
  } else {
    tft.setTextColor(C_CORAL, C_SURF);
    const char* nc = "Mega: --";
    tft.setCursor(SCR_W - tft.textWidth(nc) - 4, fy + 13);
    tft.print(nc);
  }
}

// ── Back button helper ────────────────────────────────────────────────
bool drawBackBtn(Touch& t) {
  drawButton(112, CNT_Y + 4, 68, 26, C_SURF, C_DCYAN, "< BACK", 1);
  return hit(t, 112, CNT_Y + 4, 68, 26);
}


// ════════════════════════════════════════════════════════════════════
//  BOOT SCREEN
// ════════════════════════════════════════════════════════════════════
void drawBoot() {
  static unsigned long lastDot = 0;
  static uint8_t dotIdx = 0;

  if (screenDirty) {
    tft.fillScreen(C_BG);
    tft.setTextColor(C_CYAN, C_BG);
    tft.setTextSize(4);
    const char* title = "BuddyBot";
    tft.setCursor((SCR_W - tft.textWidth(title)) / 2, 72);
    tft.print(title);

    tft.setTextColor(C_LGRAY, C_BG);
    tft.setTextSize(2);
    const char* sub = "Orbital HMI  v1.0  |  Pico 2";
    tft.setCursor((SCR_W - tft.textWidth(sub)) / 2, 120);
    tft.print(sub);

    tft.drawFastHLine(60, 148, SCR_W - 120, C_LINE);

    tft.setTextColor(C_DCYAN, C_BG);
    tft.setTextSize(1);
    const char* hw = "ST7796S  480x320  |  FT6336U Touch  |  RP2350";
    tft.setCursor((SCR_W - tft.textWidth(hw)) / 2, 158);
    tft.print(hw);

    tft.setTextColor(C_AMBER, C_BG);
    tft.setTextSize(2);
    const char* wait = "Waiting for Mega...";
    tft.setCursor((SCR_W - tft.textWidth(wait)) / 2, 200);
    tft.print(wait);

    screenDirty = false;
  }

  // Animated dots
  if (millis() - lastDot > 300) {
    lastDot = millis();
    dotIdx = (dotIdx + 1) % 6;
    int cx = SCR_W / 2 - 48;
    for (int i = 0; i < 6; i++) {
      tft.fillCircle(cx + i * 20, 248, 6, (i <= dotIdx) ? C_CYAN : C_LINE);
    }
  }
}

// ════════════════════════════════════════════════════════════════════
//  MAIN DASHBOARD
// ════════════════════════════════════════════════════════════════════
void drawMain(Touch& t) {
  // Only clear the content area on first draw; footer/header always redrawn
  if (screenDirty) {
    tft.fillRect(0, CNT_Y, SCR_W, CNT_H, C_BG);
    screenDirty = false;
  }

  drawHeader("BuddyBot Dashboard");
  drawFooter();

  const int NAV_W = 108, NAV_GAP = 4;
  const int BTN_H = (CNT_H - NAV_GAP * 3) / 4;
  const int cy = CNT_Y;

  // ── Left: Nav buttons ──────────────────────────────────────────────
  struct { const char* lbl; uint16_t col; Screen scr; } nav[] = {
    {"RADAR",   C_CYAN,  SCR_RADAR},
    {"GAMES",   C_GREEN, SCR_GAMES},
    {"SENSORS", C_AMBER, SCR_SENSORS},
    {"INFO",    C_PURP,  SCR_INFO},
  };
  for (int i = 0; i < 4; i++) {
    int by = cy + i * (BTN_H + NAV_GAP);
    drawButton(4, by, NAV_W, BTN_H, C_SURF, nav[i].col, nav[i].lbl, 2);
    if (hit(t, 4, by, NAV_W, BTN_H)) {
      currentScreen = nav[i].scr; screenDirty = true; t.pressed = false; return;
    }
  }

  // ── Centre: Mode selector ─────────────────────────────────────────
  const int MX = NAV_W + 10, MW = 118;
  const int MH = (CNT_H - NAV_GAP * 2) / 3;
  struct { const char* lbl; uint16_t col; const char* cmd; } modes[] = {
    {"NORMAL",    C_CYAN,  "NORMAL"},
    {"BODYGUARD", C_AMBER, "BODYGUARD"},
    {"DOG GUARD", C_GREEN, "DOG"},
  };
  for (int i = 0; i < 3; i++) {
    int by = cy + i * (MH + NAV_GAP);
    bool active = (telem.mode == modes[i].cmd);
    uint16_t bg = active ? modes[i].col : C_SURF;
    uint16_t fg = active ? C_BLACK      : modes[i].col;
    tft.fillRoundRect(MX, by, MW, MH, 8, bg);
    tft.drawRoundRect(MX, by, MW, MH, 8, modes[i].col);
    tft.setTextColor(fg, bg);
    tft.setTextSize(1);
    int tw = tft.textWidth(modes[i].lbl);
    tft.setCursor(MX + (MW - tw) / 2, by + (MH - 8) / 2);
    tft.print(modes[i].lbl);
    if (hit(t, MX, by, MW, MH)) {
      MEGA_SERIAL.print("MODE:"); MEGA_SERIAL.println(modes[i].cmd);
      telem.mode = modes[i].cmd; t.pressed = false;
    }
  }

  // ── Right: Live telemetry panel ───────────────────────────────────
  const int TX = MX + MW + 8, TW = SCR_W - TX - 4;
  tft.fillRect(TX, cy, TW, CNT_H, C_SURF);
  tft.drawRect(TX, cy, TW, CNT_H, C_LINE);

  int row = cy + 5;
  auto telRow = [&](const char* label, const char* val, uint16_t vc) {
    tft.setTextColor(C_MGRAY, C_SURF); tft.setTextSize(1);
    tft.setCursor(TX + 4, row); tft.print(label);
    tft.setTextColor(vc, C_SURF);
    tft.setCursor(TX + TW - tft.textWidth(val) - 4, row);
    tft.print(val);
    tft.drawFastHLine(TX + 2, row + 11, TW - 4, C_LINE);
    row += 15;
  };

  char buf[28];
  snprintf(buf, sizeof(buf), "%.1fC  %.0f%%", telem.temp, telem.hum);
  telRow("Temp / Hum", buf, C_CYAN);
  snprintf(buf, sizeof(buf), "F:%ldcm", telem.dFront);
  telRow("Front dist", buf, telem.dFront > 0 && telem.dFront < 30 ? C_CORAL : C_CYAN);
  snprintf(buf, sizeof(buf), "R:%ld L:%ld Ri:%ld", telem.dRear, telem.dLeft, telem.dRight);
  telRow("R/L/Ri dist", buf, C_CYAN);
  snprintf(buf, sizeof(buf), "%d", telem.gas);
  telRow("Gas level", buf, telem.gas > 300 ? C_CORAL : C_MINT);
  telRow("Flame", telem.flame ? "ALERT" : "clear", telem.flame ? C_CORAL : C_MINT);
  telRow("PIR / IR", telem.pir ? "MOTION" : (telem.ir ? "IR blk" : "clear"), telem.pir || telem.ir ? C_AMBER : C_MINT);
  snprintf(buf, sizeof(buf), "%.2fA  %.1fV boost", telem.amps, telem.boost);
  telRow("Power", buf, C_LGRAY);
  snprintf(buf, sizeof(buf), "%s %s %s",
    telem.r3ok ? "R3:OK" : "R3:--",
    telem.espok ? "ESP" : "ESP:--",
    telem.s9ok ? "S9" : "S9:--");
  telRow("Links", buf, C_LGRAY);
  telRow("Auto mode", telem.autoM ? "ON" : "OFF", telem.autoM ? C_GREEN : C_MGRAY);
  snprintf(buf, sizeof(buf), "%s", telem.fw.length() ? telem.fw.c_str() : "--");
  telRow("FW", buf, C_DCYAN);
}


// ════════════════════════════════════════════════════════════════════
//  RADAR SCREEN
// ════════════════════════════════════════════════════════════════════
void drawRadar(Touch& t) {
  if (screenDirty) { tft.fillRect(0, CNT_Y, SCR_W, CNT_H, C_BG); screenDirty = false; }
  drawHeader("Proximity Radar");
  drawFooter();
  if (drawBackBtn(t)) { currentScreen = SCR_MAIN; screenDirty = true; t.pressed = false; return; }

  const int CX = 240, CY = CNT_Y + CNT_H / 2 + 4;
  const int MAXR = 88;

  // Range rings
  for (int r = 1; r <= 4; r++) {
    uint16_t rc = (r == 1) ? C_CORAL : (r == 2) ? C_AMBER : C_LINE;
    tft.drawCircle(CX, CY, (MAXR * r) / 4, rc);
    char rb[8]; snprintf(rb, sizeof(rb), "%dcm", r * 25);
    tft.setTextColor(C_MGRAY, C_BG); tft.setTextSize(1);
    tft.setCursor(CX + (MAXR * r) / 4 + 3, CY - 5);
    tft.print(rb);
  }
  tft.drawFastHLine(CX - MAXR, CY, MAXR * 2, C_SURF);
  tft.drawFastVLine(CX, CY - MAXR, MAXR * 2, C_SURF);
  tft.fillCircle(CX, CY, 5, C_CYAN);

  // Plot a sensor dot
  auto plotSensor = [&](long d, float angleDeg, const char* lbl) {
    float a = angleDeg * 0.01745329f;
    float frac = (d > 0 && d < 100) ? (1.0f - d / 100.0f) : 0.0f;
    int r = (int)(frac * MAXR);
    int px = CX + (int)(sinf(a) * r);
    int py = CY - (int)(cosf(a) * r);
    uint16_t col = (d > 0 && d < 25) ? C_CORAL : (d > 0 && d < 50) ? C_AMBER : C_MINT;
    if (r > 0) { tft.fillCircle(px, py, 7, col); tft.drawCircle(px, py, 9, col); }

    char dbuf[14];
    if (d > 0) snprintf(dbuf, sizeof(dbuf), "%s:%ldcm", lbl, d);
    else        snprintf(dbuf, sizeof(dbuf), "%s:--", lbl);
    tft.setTextColor(col, C_BG); tft.setTextSize(1);
    int lx = CX + (int)(sinf(a) * (MAXR + 16)) - tft.textWidth(dbuf) / 2;
    int ly = CY - (int)(cosf(a) * (MAXR + 16)) - 4;
    lx = constrain(lx, 2, SCR_W - tft.textWidth(dbuf) - 2);
    ly = constrain(ly, CNT_Y + 34, SCR_H - FTR_H - 12);
    tft.setCursor(lx, ly); tft.print(dbuf);
  };

  plotSensor(telem.dFront,    0, "F");
  plotSensor(telem.dRear,   180, "R");
  plotSensor(telem.dLeft,   -90, "L");
  plotSensor(telem.dRight,   90, "Ri");

  // IR hit indicators (small squares outside ring)
  if (telem.ir) {
    tft.fillRect(CX - 8, CY - MAXR - 14, 16, 10, C_AMBER);
    tft.setTextColor(C_BLACK, C_AMBER); tft.setTextSize(1);
    tft.setCursor(CX - 6, CY - MAXR - 13); tft.print("IR");
  }
}

// ════════════════════════════════════════════════════════════════════
//  GAMES MENU
// ════════════════════════════════════════════════════════════════════
void drawGamesMenu(Touch& t) {
  if (screenDirty) { tft.fillRect(0, CNT_Y, SCR_W, CNT_H, C_BG); screenDirty = false; }
  drawHeader("Games for AJ \xab");
  drawFooter();
  if (drawBackBtn(t)) { currentScreen = SCR_MAIN; screenDirty = true; t.pressed = false; return; }

  struct { const char* name; Screen scr; uint16_t col; const char* desc; } g[] = {
    {"COLOURS",  SCR_GAME_COLOR, C_RED,   "Match the colour!"},
    {"SHAPES",   SCR_GAME_SHAPE, C_CYAN,  "Find the shape!"},
    {"COUNTING", SCR_GAME_COUNT, C_GREEN, "Count the stars!"},
  };

  int gw = (SCR_W - 24) / 3 - 6;
  int gy = CNT_Y + 36, gh = CNT_H - 44;

  for (int i = 0; i < 3; i++) {
    int gx = 8 + i * (gw + 8);
    tft.fillRoundRect(gx, gy, gw, gh, 14, C_SURF);
    tft.drawRoundRect(gx, gy, gw, gh, 14, g[i].col);
    tft.drawRoundRect(gx + 1, gy + 1, gw - 2, gh - 2, 13, g[i].col);  // double border

    tft.setTextColor(g[i].col, C_SURF); tft.setTextSize(2);
    int tw = tft.textWidth(g[i].name);
    tft.setCursor(gx + (gw - tw) / 2, gy + 14); tft.print(g[i].name);

    tft.setTextColor(C_LGRAY, C_SURF); tft.setTextSize(1);
    tw = tft.textWidth(g[i].desc);
    tft.setCursor(gx + (gw - tw) / 2, gy + gh - 16); tft.print(g[i].desc);

    // Big letter icon
    tft.setTextColor(g[i].col, C_SURF); tft.setTextSize(6);
    const char* icon = (i == 0) ? "C" : (i == 1) ? "S" : "*";
    tw = tft.textWidth(icon);
    tft.setCursor(gx + (gw - tw) / 2, gy + gh / 2 - 24); tft.print(icon);

    if (hit(t, gx, gy, gw, gh)) {
      gTarget = random(i == 2 ? 5 : (i == 0 ? 4 : 3));
      if (i == 2) gTarget++;   // count game: 1-5
      gScore = 0; gFeedbackUntil = 0;
      currentScreen = g[i].scr; screenDirty = true; t.pressed = false; return;
    }
  }
}


// ════════════════════════════════════════════════════════════════════
//  GAME HELPERS — feedback flash + next round
// ════════════════════════════════════════════════════════════════════
void gameCorrect(uint8_t s, int newMax) {
  gScore++;
  tft.fillRect(0, CNT_Y + 34, SCR_W, CNT_H - 34, C_MINT);
  tft.setTextColor(C_BLACK, C_MINT); tft.setTextSize(4);
  const char* yay = "CORRECT!";
  tft.setCursor((SCR_W - tft.textWidth(yay)) / 2, SCR_H / 2 - 20);
  tft.print(yay);
  char sb[16]; snprintf(sb, sizeof(sb), "Score: %d", gScore);
  tft.setTextSize(2); tft.setCursor((SCR_W - tft.textWidth(sb)) / 2, SCR_H / 2 + 24);
  tft.print(sb);
  boop(1500, 120); delay(80); boop(2000, 150);
  gTarget = random(newMax) + (s == SCR_GAME_COUNT ? 1 : 0);
  gFeedbackUntil = millis() + 800;
  gFeedbackCorrect = true;
}

void gameWrong() {
  tft.fillRect(0, CNT_Y + 34, SCR_W, CNT_H - 34, C_CORAL);
  tft.setTextColor(C_WHITE, C_CORAL); tft.setTextSize(4);
  const char* nope = "Try again!";
  tft.setCursor((SCR_W - tft.textWidth(nope)) / 2, SCR_H / 2 - 20);
  tft.print(nope);
  boop(400, 300);
  gFeedbackUntil = millis() + 700;
  gFeedbackCorrect = false;
}

// ════════════════════════════════════════════════════════════════════
//  COLOUR MATCHING GAME
// ════════════════════════════════════════════════════════════════════
void drawGameColor(Touch& t) {
  if (millis() < gFeedbackUntil) return;   // show feedback frame
  if (screenDirty) { tft.fillRect(0, CNT_Y, SCR_W, CNT_H, C_BG); screenDirty = false; }
  drawHeader("Colour Match");
  drawFooter();
  if (drawBackBtn(t)) { currentScreen = SCR_GAMES; screenDirty = true; t.pressed = false; return; }

  // Score
  char sb[16]; snprintf(sb, sizeof(sb), "Score: %d", gScore);
  tft.setTextColor(C_CYAN, C_BG); tft.setTextSize(2);
  tft.setCursor(SCR_W - tft.textWidth(sb) - 8, CNT_Y + 6); tft.print(sb);

  // Question
  tft.setTextColor(C_WHITE, C_BG); tft.setTextSize(2);
  const char* q = "Touch the colour:";
  tft.setCursor((SCR_W - tft.textWidth(q)) / 2, CNT_Y + 38); tft.print(q);

  // Target name in its own colour — big
  tft.setTextColor(GCOLS[gTarget], C_BG); tft.setTextSize(4);
  const char* cn = GCOLNAMES[gTarget];
  tft.setCursor((SCR_W - tft.textWidth(cn)) / 2, CNT_Y + 64); tft.print(cn);

  // 4 colour choice buttons
  int bw = (SCR_W - 24) / 2 - 4, bh = 50;
  for (int i = 0; i < 4; i++) {
    int bx = 8 + (i % 2) * (bw + 8);
    int by = (i < 2) ? (CNT_Y + 118) : (CNT_Y + 174);
    tft.fillRoundRect(bx, by, bw, bh, 10, GCOLS[i]);
    tft.drawRoundRect(bx, by, bw, bh, 10, C_WHITE);
    if (hit(t, bx, by, bw, bh)) {
      t.pressed = false;
      (i == gTarget) ? gameCorrect((uint8_t)SCR_GAME_COLOR, 4) : gameWrong();
      screenDirty = true; return;
    }
  }
}

// ════════════════════════════════════════════════════════════════════
//  SHAPE RECOGNITION GAME
// ════════════════════════════════════════════════════════════════════
void drawShapeIcon(int cx, int cy, int r, int shape, uint16_t col) {
  switch (shape) {
    case 0: tft.fillCircle(cx, cy, r, col); break;
    case 1: tft.fillRect(cx - r, cy - r, r * 2, r * 2, col); break;
    case 2: tft.fillTriangle(cx, cy - r, cx - r, cy + r, cx + r, cy + r, col); break;
  }
}

void drawGameShape(Touch& t) {
  const char* names[] = {"CIRCLE", "SQUARE", "TRIANGLE"};
  if (millis() < gFeedbackUntil) return;
  if (screenDirty) { tft.fillRect(0, CNT_Y, SCR_W, CNT_H, C_BG); screenDirty = false; }
  drawHeader("Shape Match");
  drawFooter();
  if (drawBackBtn(t)) { currentScreen = SCR_GAMES; screenDirty = true; t.pressed = false; return; }

  char sb[16]; snprintf(sb, sizeof(sb), "Score: %d", gScore);
  tft.setTextColor(C_CYAN, C_BG); tft.setTextSize(2);
  tft.setCursor(SCR_W - tft.textWidth(sb) - 8, CNT_Y + 6); tft.print(sb);

  tft.setTextColor(C_WHITE, C_BG); tft.setTextSize(2);
  const char* q = "Find the shape:";
  tft.setCursor((SCR_W - tft.textWidth(q)) / 2, CNT_Y + 36); tft.print(q);

  tft.setTextColor(C_CYAN, C_BG); tft.setTextSize(4);
  const char* sn = names[gTarget];
  tft.setCursor((SCR_W - tft.textWidth(sn)) / 2, CNT_Y + 62); tft.print(sn);

  int bw = (SCR_W - 24) / 3 - 4, bh = 96;
  int by = CNT_Y + 118;
  for (int i = 0; i < 3; i++) {
    int bx = 8 + i * (bw + 6);
    tft.fillRoundRect(bx, by, bw, bh, 8, C_SURF);
    tft.drawRoundRect(bx, by, bw, bh, 8, C_CYAN);
    drawShapeIcon(bx + bw / 2, by + bh / 2, bh / 3 - 4, i, C_CYAN);
    if (hit(t, bx, by, bw, bh)) {
      t.pressed = false;
      (i == gTarget) ? gameCorrect((uint8_t)SCR_GAME_SHAPE, 3) : gameWrong();
      screenDirty = true; return;
    }
  }
}

// ════════════════════════════════════════════════════════════════════
//  COUNTING GAME  (count stars 1-5)
// ════════════════════════════════════════════════════════════════════
void drawGameCount(Touch& t) {
  if (millis() < gFeedbackUntil) return;
  if (screenDirty) { tft.fillRect(0, CNT_Y, SCR_W, CNT_H, C_BG); screenDirty = false; }
  drawHeader("Counting Stars");
  drawFooter();
  if (drawBackBtn(t)) { currentScreen = SCR_GAMES; screenDirty = true; t.pressed = false; return; }

  char sb[16]; snprintf(sb, sizeof(sb), "Score: %d", gScore);
  tft.setTextColor(C_CYAN, C_BG); tft.setTextSize(2);
  tft.setCursor(SCR_W - tft.textWidth(sb) - 8, CNT_Y + 6); tft.print(sb);

  tft.setTextColor(C_WHITE, C_BG); tft.setTextSize(2);
  const char* q = "How many stars?";
  tft.setCursor((SCR_W - tft.textWidth(q)) / 2, CNT_Y + 36); tft.print(q);

  // Star display area
  tft.fillRoundRect(20, CNT_Y + 62, SCR_W - 40, 72, 8, C_SURF);
  int spread = (SCR_W - 80) / max(gTarget, 1);
  tft.setTextColor(C_YLLOW, C_SURF); tft.setTextSize(4);
  for (int i = 0; i < gTarget; i++) {
    int sx = 36 + i * spread;
    tft.setCursor(sx, CNT_Y + 74); tft.print("*");
  }

  // Number buttons 1-5
  int bw = (SCR_W - 24) / 5 - 3, bh = 52;
  int by = CNT_Y + 148;
  for (int i = 0; i < 5; i++) {
    int bx = 8 + i * (bw + 4);
    char nb[3]; snprintf(nb, sizeof(nb), "%d", i + 1);
    uint16_t bc = (i + 1 == gTarget) ? C_YLLOW : C_SURF;  // subtle hint colour after first wrong
    tft.fillRoundRect(bx, by, bw, bh, 8, bc);
    tft.drawRoundRect(bx, by, bw, bh, 8, C_YLLOW);
    tft.setTextColor(C_YLLOW, bc); tft.setTextSize(4);
    int tw = tft.textWidth(nb);
    tft.setCursor(bx + (bw - tw) / 2, by + (bh - 32) / 2); tft.print(nb);
    if (hit(t, bx, by, bw, bh)) {
      t.pressed = false;
      (i + 1 == gTarget) ? gameCorrect((uint8_t)SCR_GAME_COUNT, 5) : gameWrong();
      screenDirty = true; return;
    }
  }
}


// ════════════════════════════════════════════════════════════════════
//  SYSTEM INFO SCREEN
// ════════════════════════════════════════════════════════════════════
void drawInfo(Touch& t) {
  if (screenDirty) { tft.fillRect(0, CNT_Y, SCR_W, CNT_H, C_BG); screenDirty = false; }
  drawHeader("System Info");
  drawFooter();
  if (drawBackBtn(t)) { currentScreen = SCR_MAIN; screenDirty = true; t.pressed = false; return; }

  int row = CNT_Y + 36;
  auto infoRow = [&](const char* lbl, const char* val, uint16_t vc) {
    tft.setTextColor(C_MGRAY, C_BG); tft.setTextSize(1);
    tft.setCursor(10, row); tft.print(lbl);
    tft.setTextColor(vc, C_BG);
    tft.setCursor(210, row); tft.print(val);
    tft.drawFastHLine(8, row + 12, SCR_W - 16, C_LINE);
    row += 17;
  };

  char buf[28];
  infoRow("Mega firmware",    telem.fw.length() ? telem.fw.c_str() : "--", C_CYAN);
  snprintf(buf, sizeof(buf), "%.2f V", telem.volt);
  infoRow("Battery voltage",  buf, telem.volt > 7.0f ? C_MINT : C_CORAL);
  snprintf(buf, sizeof(buf), "%d %%", telem.pct);
  infoRow("Battery percent",  buf, telem.pct > 50 ? C_MINT : C_AMBER);
  snprintf(buf, sizeof(buf), "%.2f A", telem.amps);
  infoRow("Current draw",     buf, C_LGRAY);
  snprintf(buf, sizeof(buf), "%.2f V", telem.boost);
  infoRow("Boost output",     buf, C_LGRAY);
  infoRow("R3 motor shield",  telem.r3ok  ? "OK"   : "FAIL", telem.r3ok  ? C_MINT : C_CORAL);
  infoRow("ESP32 bridge",     telem.espok ? "OK"   : "WAIT", telem.espok ? C_MINT : C_AMBER);
  infoRow("S9 Android app",   telem.s9ok  ? "OK"   : "WAIT", telem.s9ok  ? C_MINT : C_AMBER);
  infoRow("Autonomous mode",  telem.autoM ? "ON"   : "OFF",  telem.autoM ? C_GREEN : C_MGRAY);
  snprintf(buf, sizeof(buf), "%lu s", millis() / 1000);
  infoRow("Pico 2 uptime",    buf, C_LGRAY);
  snprintf(buf, sizeof(buf), "%lu s ago", (millis() - lastMegaRx) / 1000);
  infoRow("Last Mega packet", buf, megaLinked ? C_MINT : C_CORAL);
}

// ════════════════════════════════════════════════════════════════════
//  SENSOR TOGGLE SCREEN
// ════════════════════════════════════════════════════════════════════
void drawSensors(Touch& t) {
  if (screenDirty) { tft.fillRect(0, CNT_Y, SCR_W, CNT_H, C_BG); screenDirty = false; }
  drawHeader("Sensor Config");
  drawFooter();
  if (drawBackBtn(t)) { currentScreen = SCR_MAIN; screenDirty = true; t.pressed = false; return; }

  struct SBtn { const char* id; bool* flag; const char* lbl; };
  SBtn sb[] = {
    {"DHT",     &sens.dht,     "DHT Temp"},
    {"GAS",     &sens.gas,     "Gas"},
    {"FLAME",   &sens.flame,   "Flame"},
    {"PIR",     &sens.pir,     "PIR Motion"},
    {"TILT",    &sens.tilt,    "Tilt"},
    {"IR",      &sens.ir,      "IR Obstacle"},
    {"US",      &sens.us,      "Ultrasonic"},
    {"CURRENT", &sens.current, "Current"},
  };

  int cols = 2, n = 8;
  int bw = (SCR_W - 20) / cols - 8, bh = (CNT_H - 44) / (n / cols) - 4;

  for (int i = 0; i < n; i++) {
    int col = i % cols, row = i / cols;
    int bx = 8 + col * (bw + 8);
    int by = CNT_Y + 40 + row * (bh + 4);
    bool on = *sb[i].flag;
    tft.fillRoundRect(bx, by, bw, bh, 8, on ? 0x0440u : C_SURF);    // dark green or surface
    tft.drawRoundRect(bx, by, bw, bh, 8, on ? C_MINT : C_CORAL);
    tft.setTextColor(on ? C_MINT : C_CORAL, on ? 0x0440u : C_SURF);
    tft.setTextSize(1);
    int tw = tft.textWidth(sb[i].lbl);
    tft.setCursor(bx + (bw - tw) / 2, by + (bh - 16) / 2);
    tft.print(sb[i].lbl);
    tft.setTextSize(1);
    const char* st = on ? "ON" : "OFF";
    tw = tft.textWidth(st);
    tft.setCursor(bx + (bw - tw) / 2, by + (bh - 16) / 2 + 12);
    tft.print(st);

    if (hit(t, bx, by, bw, bh)) {
      *sb[i].flag = !(*sb[i].flag);
      MEGA_SERIAL.print("TOGGLE_SENSOR:");
      MEGA_SERIAL.print(sb[i].id);
      MEGA_SERIAL.println(*sb[i].flag ? ":ON" : ":OFF");
      t.pressed = false;
    }
  }
}

// ════════════════════════════════════════════════════════════════════
//  ALERT OVERLAY  (drawn on top of current screen)
// ════════════════════════════════════════════════════════════════════
void drawAlert(Touch& t) {
  const int AX = 48, AY = 64, AW = SCR_W - 96, AH = SCR_H - 128;
  tft.fillRoundRect(AX, AY, AW, AH, 12, alertColor);
  tft.drawRoundRect(AX, AY, AW, AH, 12, C_WHITE);
  tft.drawRoundRect(AX+2, AY+2, AW-4, AH-4, 10, C_WHITE);

  tft.setTextColor(C_WHITE, alertColor); tft.setTextSize(3);
  int tw = tft.textWidth(alertTitle.c_str());
  tft.setCursor(AX + (AW - tw) / 2, AY + 20); tft.print(alertTitle);

  tft.setTextSize(2);
  tw = tft.textWidth(alertMsg.c_str());
  tft.setCursor(AX + (AW - tw) / 2, AY + 68); tft.print(alertMsg);

  int bx = AX + (AW - 110) / 2, by = AY + AH - 50;
  drawButton(bx, by, 110, 34, C_WHITE, alertColor, "DISMISS", 2);
  if (hit(t, bx, by, 110, 34) || (alertTs > 0 && millis() - alertTs > 10000)) {
    alertActive = false; alertTs = 0; screenDirty = true; t.pressed = false;
  }
}


// ════════════════════════════════════════════════════════════════════
//  MEGA PROTOCOL PARSER  (identical wire format to R4 V25)
// ════════════════════════════════════════════════════════════════════

void raisAlert(const char* title, const char* msg, uint16_t col) {
  alertTitle = title; alertMsg = msg; alertColor = col;
  alertActive = true; alertTs = millis();
}

void parseStat(const String& s) {
  // STAT:gas:temp:hum:haz:pir:tilt:flame:ir:volt:pct:amps[:boost]
  String f[13]; int n = 0, st = 5;
  for (int i = 5; i <= (int)s.length() && n < 13; i++) {
    if (i == (int)s.length() || s[i] == ':') {
      f[n++] = s.substring(st, i); st = i + 1;
    }
  }
  if (n < 11) return;
  telem.gas   = f[0].toInt();
  telem.temp  = f[1].toFloat();
  telem.hum   = f[2].toFloat();
  telem.haz   = f[3].toInt();
  telem.pir   = f[4].toInt();
  telem.tilt  = f[5].toInt();
  telem.flame = f[6].toInt();
  telem.ir    = f[7].toInt();
  telem.volt  = f[8].toFloat();
  telem.pct   = f[9].toInt();
  telem.amps  = f[10].toFloat();
  if (n > 11) telem.boost = f[11].toFloat();
}

void parseUS(const String& s) {
  // US:front,rear,left,right
  String tmp = s.substring(3);
  String f[4]; int n = 0, st = 0;
  for (int i = 0; i <= (int)tmp.length() && n < 4; i++) {
    if (i == (int)tmp.length() || tmp[i] == ',') {
      f[n++] = tmp.substring(st, i); st = i + 1;
    }
  }
  if (n < 4) return;
  telem.dFront = f[0].toInt();
  telem.dRear  = f[1].toInt();
  telem.dLeft  = f[2].toInt();
  telem.dRight = f[3].toInt();
}

void parseStatusPipe(const String& s) {
  // STATUS|ESTOP:YES|AUTO:ON|BAT:8.40|PCT:100|R3:OK|ESP:OK|S9:OK|FW:V30.0
  telem.estop = (s.indexOf("ESTOP:YES") >= 0);
  telem.autoM = (s.indexOf("AUTO:ON")   >= 0);
  telem.r3ok  = (s.indexOf("R3:OK")     >= 0);
  telem.espok = (s.indexOf("ESP:OK")    >= 0);
  telem.s9ok  = (s.indexOf("S9:OK")     >= 0);
  int fi = s.indexOf("FW:");
  if (fi >= 0) {
    int fe = s.indexOf('|', fi);
    telem.fw = (fe > 0) ? s.substring(fi + 3, fe) : s.substring(fi + 3);
  }
}

void parseSensStatus(const String& s) {
  // SENS_ST|DHT:1|LIGHT:1|...|CUR:1|GPS:1|END
  sens.dht     = s.indexOf("DHT:1")    >= 0;
  sens.gas      = s.indexOf("GAS:1")   >= 0;
  sens.flame    = s.indexOf("FLAME:1") >= 0;
  sens.pir      = s.indexOf("PIR:1")   >= 0;
  sens.tilt     = s.indexOf("TILT:1")  >= 0;
  sens.ir       = s.indexOf("IR:1")    >= 0;
  sens.us       = s.indexOf("US:1")    >= 0;
  sens.current  = s.indexOf("CUR:1")   >= 0;
}

void handleMegaLine(String& line) {
  line.trim();
  if (line.length() == 0) return;
  lastMegaRx = millis();
  megaLinked  = true;

  if      (line.startsWith("STAT:"))          parseStat(line);
  else if (line.startsWith("US:"))            parseUS(line);
  else if (line.startsWith("STATUS|"))        parseStatusPipe(line);
  else if (line.startsWith("SENS_ST|"))       parseSensStatus(line);
  else if (line.startsWith("MODE:"))          telem.mode = line.substring(5);
  else if (line == "PING")                    MEGA_SERIAL.println("PONG");   // boot handshake
  else if (line.startsWith("PONG_R4:"))       { /* heartbeat ack — no action needed */ }
  else if (line.startsWith("MEGA_READY|") ||
           line.startsWith("SYSTEM|READY|"))  {
    parseStatusPipe(line);
    MEGA_SERIAL.println("SENSOR_STATUS");    // request sensor flag sync
  }
  else if (line.startsWith("CONN_STATUS|"))   parseStatusPipe(line);
  else if (line.startsWith("BAT:WARN"))       raisAlert("Battery Low",      "Charge soon",        C_AMBER);
  else if (line.startsWith("BAT:LOW"))        raisAlert("Battery Critical", "Plug in NOW",        C_CORAL);
  else if (line.startsWith("BAT:CRIT"))       raisAlert("BATTERY CRITICAL", "Robot stopping!",    C_CORAL);
  else if (line.startsWith("SAFETY:FLAME"))   raisAlert("FLAME DETECTED",   "Check surroundings", C_CORAL);
  else if (line.startsWith("SAFETY:TILT"))    raisAlert("Robot Tilted",     "Check the robot",    C_AMBER);
  else if (line.startsWith("SAFETY:OVERTEMP"))raisAlert("OVERHEATING",      "Battery too hot!",   C_CORAL);
  else if (line.startsWith("SAFETY:GAS"))     raisAlert("Gas Detected",     "Ventilate area",     C_AMBER);
  else if (line.startsWith("EVENT:NAVFAIL"))  raisAlert("Navigation Failed","Manual mode active", C_AMBER);
}

void handleMegaSerial() {
  // Byte-budgeted read — max 64 bytes per call, same discipline as Mega
  int budget = 64;
  while (MEGA_SERIAL.available() && budget-- > 0) {
    char c = MEGA_SERIAL.read();
    if (c == '\n') {
      handleMegaLine(megaBuf);
      megaBuf = "";
    } else if (c != '\r') {
      megaBuf += c;
      if (megaBuf.length() > 128) megaBuf = "";
    }
  }
}


// ════════════════════════════════════════════════════════════════════
//  SETUP
// ════════════════════════════════════════════════════════════════════
void setup() {
  // Allow USB stack to enumerate before doing anything else.
  // Without this, Windows shows "device malfunctioned" if setup() crashes early.
  delay(1500);

  // ── Backlight on immediately so the screen isn't dark during init ──
  pinMode(PIN_BL, OUTPUT);
  digitalWrite(PIN_BL, HIGH);

  // ── Optional buzzer ───────────────────────────────────────────────
#if HAVE_BUZZER
  pinMode(PIN_BUZZ, OUTPUT);
#endif

  // ── Touch controller reset ────────────────────────────────────────
  pinMode(PIN_CTP_RST, OUTPUT);
  digitalWrite(PIN_CTP_RST, LOW);  delay(20);
  digitalWrite(PIN_CTP_RST, HIGH); delay(100);
  pinMode(PIN_CTP_INT, INPUT);

  // ── I2C1 for FT6336U ─────────────────────────────────────────────
  Wire.setSDA(PIN_CTP_SDA);
  Wire.setSCL(PIN_CTP_SCL);
  Wire.begin();
  Wire.setClock(400000);   // 400 kHz fast-mode

  // ── TFT display (SPI0 via TFT_eSPI) ──────────────────────────────
  // Manual hardware reset pulse — ensures display is out of reset
  // before TFT_eSPI sends the init sequence
  pinMode(PIN_RST, OUTPUT);
  digitalWrite(PIN_RST, HIGH); delay(10);
  digitalWrite(PIN_RST, LOW);  delay(50);
  digitalWrite(PIN_RST, HIGH); delay(150);

  tft.init();
  tft.setRotation(1);      // landscape: 480 x 320

  // Diagnostic: flash red then green to confirm init worked
  // If you see these colours the display is alive — replace with C_BG after confirmed
  tft.fillScreen(TFT_RED);   delay(300);
  tft.fillScreen(TFT_GREEN); delay(300);
  tft.fillScreen(C_BG);

  tft.setTextDatum(TL_DATUM);

  // ── Mega UART (UART0: GP0=TX, GP1=RX) ────────────────────────────
  Serial1.setTX(0);
  Serial1.setRX(1);
  MEGA_SERIAL.begin(MEGA_BAUD);

  // ── RNG seed ─────────────────────────────────────────────────────
  randomSeed(analogRead(A0) ^ (analogRead(A1) << 8));

  // ── Initial ping so Mega's boot PING-PONG exchange can complete ───
  delay(400);
  MEGA_SERIAL.println("PONG");    // pre-emptive — Mega may already be waiting
  MEGA_SERIAL.println("PING_R4:0");

  screenDirty = true;
}

// ════════════════════════════════════════════════════════════════════
//  LOOP
// ════════════════════════════════════════════════════════════════════
void loop() {
  // ── 1. Service Mega serial input ──────────────────────────────────
  handleMegaSerial();

  // ── 2. Periodic PING_R4 heartbeat (every 5 s) ────────────────────
  if (millis() - lastPing > 5000) {
    lastPing = millis();
    MEGA_SERIAL.print("PING_R4:");
    MEGA_SERIAL.println(pingSeq++);
    if (pingSeq > 9999) pingSeq = 0;
  }

  // ── 3. Mega link watchdog ─────────────────────────────────────────
  if (megaLinked && millis() - lastMegaRx > 12000) {
    megaLinked = false;
  }

  // ── 4. Auto-advance from boot once Mega responds ──────────────────
  if (currentScreen == SCR_BOOT && megaLinked) {
    currentScreen = SCR_MAIN;
    screenDirty   = true;
    boop(1200, 100); delay(80); boop(1600, 120);
  }

  // ── 5. Read touch (debounced — 80 ms) ────────────────────────────
  Touch t = {0, 0, false};
  if (millis() - lastTouchMs > 80 && !digitalRead(PIN_CTP_INT)) {
    t = readTouch();
    if (t.pressed) lastTouchMs = millis();
  }

  // ── 6. Global E-STOP footer handler (always available) ───────────
  if (hit(t, 4, SCR_H - FTR_H + 4, 88, FTR_H - 8)) {
    if (telem.estop) {
      MEGA_SERIAL.println("ESTOP_CLEAR");
      telem.estop = false;
    } else {
      MEGA_SERIAL.println("EMERGENCY_STOP");
      telem.estop = true;
    }
    screenDirty = true;
    t.pressed = false;
  }

  // ── 7. Alert overlay (draws on top, blocks navigation) ───────────
  if (alertActive) {
    drawHeader(currentScreen == SCR_BOOT ? "BuddyBot" : "Alert");
    drawFooter();
    drawAlert(t);
    return;
  }

  // ── 8. Telemetry refresh timer for main dashboard ─────────────────
  bool telemDue = (millis() - lastTelemDraw > 1000);
  if (currentScreen == SCR_MAIN && telemDue) {
    lastTelemDraw = millis();
    screenDirty = true;   // redraw telem panel every second
  }

  // ── 9. Screen dispatcher ─────────────────────────────────────────
  switch (currentScreen) {
    case SCR_BOOT:       drawBoot();          break;
    case SCR_MAIN:       drawMain(t);         break;
    case SCR_RADAR:      drawRadar(t);        break;
    case SCR_GAMES:      drawGamesMenu(t);    break;
    case SCR_GAME_COLOR: drawGameColor(t);    break;
    case SCR_GAME_SHAPE: drawGameShape(t);    break;
    case SCR_GAME_COUNT: drawGameCount(t);    break;
    case SCR_INFO:       drawInfo(t);         break;
    case SCR_SENSORS:    drawSensors(t);      break;
  }
}
