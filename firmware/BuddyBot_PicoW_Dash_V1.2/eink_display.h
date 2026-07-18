// ═════════════════════════════════════════════════════════════════════════════
//  BUDDYBOT  ·  PICO W  ·  E-INK TELEMETRY DISPLAY
//  File : eink_display.h
//  Hardware : Jaycar XC3747 (Waveshare 1.54" tricolour, 200x200, black/red/white)
// ═════════════════════════════════════════════════════════════════════════════
//
//  Wiring (all pins within GP9-GP19)
//  ─────────────────────────────────
//  XC3747  VCC  →  3V3  (NOT 5V)
//  XC3747  GND  →  GND
//  XC3747  DIN  →  GP11  (SPI1 MOSI)
//  XC3747  CLK  →  GP10  (SPI1 SCK)
//  XC3747  CS   →  GP9
//  XC3747  DC   →  GP12
//  XC3747  RST  →  GP13
//  XC3747  BUSY →  GP14  (input, HIGH = refreshing)
//
//  Required libraries (Arduino IDE → Manage Libraries)
//  ───────────────────────────────────────────────────
//  GxEPD2           by Jean-Marc Zingg
//  Adafruit GFX Library
//
//  If the display shows a blank screen, change GxEPD2_154c to GxEPD2_154_Z90c
//  in the display declaration below.
//
//  Refresh behaviour
//  ─────────────────
//  E-ink full refresh takes ~8 seconds. This is normal for this display.
//  einkLoop() blocks Core 0 for 8 s during each refresh.
//  Since refresh happens every 30 s, serial data may be lost during that window.
//  For a status display updating every 30 s, this is acceptable.
//  To mitigate: increase Serial2 FIFO in bridgeSetup():
//    Serial2.setFIFOSize(512);   // call before Serial2.begin()
//
//  Integration
//  ────────────
//  #include "eink_display.h"
//
//  void setup() {
//    einkSetup();          // call after bridgeSetup()
//    ...
//  }
//  void loop() {
//    bridgeLoop();
//    einkLoop();           // triggers refresh every 30 s
//    ...
//  }
//  void onMegaLine(const String& line) {
//    einkParseMega(line);  // keep display state up to date
//    audioFromMega(line);
//    ...
//  }
//
// ═════════════════════════════════════════════════════════════════════════════

#pragma once
#include <Arduino.h>
#include <SPI.h>
#include <GxEPD2_3C.h>
#include <Fonts/FreeMono9pt7b.h>
#include <Fonts/FreeMonoBold9pt7b.h>

// ── Pin definitions ───────────────────────────────────────────────────────────
#define EINK_CS    9
#define EINK_CLK  10    // SPI1 SCK
#define EINK_DIN  11    // SPI1 MOSI
#define EINK_DC   12
#define EINK_RST  13
#define EINK_BUSY 14    // HIGH while display is refreshing

// Update interval -- e-ink is not suitable for sub-second updates
#define EINK_REFRESH_MS  30000UL   // 30 seconds between redraws

// ── Display instance ──────────────────────────────────────────────────────────
// GxEPD2_154c  = Waveshare 1.54" B tricolour (IL0373, GDEW0154Z04)
// If blank screen: try GxEPD2_154_Z90c instead
GxEPD2_3C<GxEPD2_154c, GxEPD2_154c::HEIGHT> _eink(EINK_CS, EINK_DC, EINK_RST, EINK_BUSY);

// ── Telemetry state (populated by einkParseMega) ──────────────────────────────
static struct {
  float battVolt   = 0.0f;
  int   battPct    = 0;
  float ambTemp    = 0.0f;
  bool  autoMode   = false;
  bool  smokeAlarm = false;
  bool  estop      = false;
  bool  charging   = false;
} _ed;

static unsigned long _einkLastRefresh = 0;
static bool          _einkReady       = false;

// ══════════════════════════════════════════════════════════════════════════════
//  DRAWING PRIMITIVES
// ══════════════════════════════════════════════════════════════════════════════

static void _centred(const char* txt, int16_t y, uint16_t col = GxEPD_BLACK) {
  int16_t x1, y1; uint16_t tw, th;
  _eink.getTextBounds(txt, 0, y, &x1, &y1, &tw, &th);
  _eink.setCursor(max((int16_t)0, (int16_t)((200 - tw) / 2)), y);
  _eink.setTextColor(col);
  _eink.print(txt);
}

static void _batBar(int16_t x, int16_t y, int16_t w, int16_t h, int pct, uint16_t col) {
  _eink.drawRect(x, y, w, h, GxEPD_BLACK);
  int filled = constrain((int)((w - 2) * pct / 100), 0, w - 2);
  if (filled > 0) _eink.fillRect(x + 1, y + 1, filled, h - 2, col);
}

// ══════════════════════════════════════════════════════════════════════════════
//  SCREEN LAYOUT  (200 x 200 px, portrait)
//
//   Y   0 ┌──────────────────────┐
//      15  │    BuddyBot          │  bold title
//      28  │    V36.0             │  fw version
//      29  ├──────────────────────┤  separator
//      50  │ BAT [██████████░░] 84%│  battery bar + pct
//      66  │ 8.24V  CHARGING       │  voltage + charge state
//      72  ├──────────────────────┤
//      88  │ TEMP  26.3C          │  ambient temp
//     104  │ GAS   CLEAR / ALARM  │  gas status (red if alarm)
//     110  ├──────────────────────┤
//     126  │ MODE  AUTONOMOUS      │  drive mode
//     142  │ ESTOP OFF / ACTIVE   │  estop (red if active)
//     148  ├──────────────────────┤
//     165  │ Updated 30s ago      │  last refresh age
//     200  └──────────────────────┘
// ══════════════════════════════════════════════════════════════════════════════

static void _einkDraw() {
  _eink.fillScreen(GxEPD_WHITE);
  _eink.setTextWrap(false);
  char buf[32];

  // ── Title ──────────────────────────────────────────────────────────────────
  _eink.setFont(&FreeMonoBold9pt7b);
  _centred("BuddyBot", 15);
  _eink.setFont(&FreeMono9pt7b);
  _centred("V36.0", 28);
  _eink.drawFastHLine(0, 32, 200, GxEPD_BLACK);

  // ── Battery ────────────────────────────────────────────────────────────────
  bool batLow       = (_ed.battPct < 20);
  uint16_t batColor = batLow ? GxEPD_RED : GxEPD_BLACK;

  _eink.setFont(&FreeMono9pt7b);
  _eink.setTextColor(GxEPD_BLACK);
  _eink.setCursor(2, 50);
  _eink.print("BAT");
  _batBar(32, 38, 120, 14, _ed.battPct, batColor);
  snprintf(buf, sizeof(buf), "%3d%%", _ed.battPct);
  _eink.setTextColor(batLow ? GxEPD_RED : GxEPD_BLACK);
  _eink.setCursor(156, 50);
  _eink.print(buf);

  // Voltage + charging indicator
  _eink.setTextColor(GxEPD_BLACK);
  _eink.setCursor(2, 66);
  if (_ed.charging)
    snprintf(buf, sizeof(buf), "%.2fV  CHARGING", _ed.battVolt);
  else
    snprintf(buf, sizeof(buf), "%.2fV", _ed.battVolt);
  _eink.print(buf);

  _eink.drawFastHLine(0, 71, 200, GxEPD_BLACK);

  // ── Temperature ────────────────────────────────────────────────────────────
  _eink.setTextColor(GxEPD_BLACK);
  _eink.setCursor(2, 88);
  snprintf(buf, sizeof(buf), "TEMP  %.1f C", _ed.ambTemp);
  _eink.print(buf);

  // ── Gas / smoke ────────────────────────────────────────────────────────────
  _eink.setCursor(2, 104);
  if (_ed.smokeAlarm) {
    _eink.setTextColor(GxEPD_RED);
    _eink.print("GAS   ** ALARM **");
  } else {
    _eink.setTextColor(GxEPD_BLACK);
    _eink.print("GAS   CLEAR");
  }

  _eink.drawFastHLine(0, 110, 200, GxEPD_BLACK);

  // ── Drive mode ─────────────────────────────────────────────────────────────
  _eink.setTextColor(GxEPD_BLACK);
  _eink.setCursor(2, 126);
  _eink.print("MODE  ");
  _eink.print(_ed.autoMode ? "AUTONOMOUS" : "MANUAL");

  // ── ESTOP ──────────────────────────────────────────────────────────────────
  _eink.setCursor(2, 142);
  if (_ed.estop) {
    _eink.setTextColor(GxEPD_RED);
    _eink.print("ESTOP ** ACTIVE **");
  } else {
    _eink.setTextColor(GxEPD_BLACK);
    _eink.print("ESTOP OFF");
  }

  _eink.drawFastHLine(0, 148, 200, GxEPD_BLACK);

  // ── Footer ─────────────────────────────────────────────────────────────────
  _eink.setTextColor(GxEPD_BLACK);
  unsigned long age = (millis() - _einkLastRefresh) / 1000;
  snprintf(buf, sizeof(buf), "Updated %lus ago", age);
  _centred(buf, 165);
}

// ══════════════════════════════════════════════════════════════════════════════
//  PUBLIC API
// ══════════════════════════════════════════════════════════════════════════════

// Parse a line arriving from the Mega and update the display state.
// Call this from onMegaLine() in your main sketch.
void einkParseMega(const String& msg) {

  // STAT:gas:ambTemp:hum:haz:pir:tilt:ir:battVolt:battPct:amps
  if (msg.startsWith("STAT:")) {
    int field = 0, start = 5;
    for (int i = 5; i <= (int)msg.length(); i++) {
      if (i == (int)msg.length() || msg[i] == ':') {
        String tok = msg.substring(start, i);
        switch (field) {
          case 1: _ed.ambTemp  = tok.toFloat(); break;
          case 7: _ed.battVolt = tok.toFloat(); break;
          case 8: _ed.battPct  = tok.toInt();   break;
        }
        field++; start = i + 1;
      }
    }
    return;
  }

  // STATUS|ESTOP:YES|AUTO:ON|...|CHG:YES|...
  if (msg.startsWith("STATUS|")) {
    _ed.estop    = (msg.indexOf("ESTOP:YES") >= 0);
    _ed.autoMode = (msg.indexOf("AUTO:ON")   >= 0);
    _ed.charging = (msg.indexOf("CHG:YES")   >= 0);
    return;
  }

  if (msg == "SAFETY:SMOKE_ALARM")              { _ed.smokeAlarm = true;  einkForceRefresh(); return; }
  if (msg == "SAFETY:SMOKE_CLEARED")            { _ed.smokeAlarm = false; einkForceRefresh(); return; }
  if (msg.startsWith("CHARGE:MANUAL:CONN"))     { _ed.charging   = true;  return; }
  if (msg.startsWith("CHARGE:MANUAL:DISC"))     { _ed.charging   = false; return; }
  if (msg == "EMERGENCY_STOP")                  { _ed.estop      = true;  einkForceRefresh(); return; }
  if (msg == "ESTOP_CLEAR" || msg.startsWith("ESTOP|CLEARED")) { _ed.estop = false; return; }
}

// Force an immediate refresh on the next einkLoop() call.
// Called automatically on smoke alarm, ESTOP changes.
void einkForceRefresh() {
  _einkLastRefresh = 0;
}

// Call once from setup() on Core 0 -- after bridgeSetup().
void einkSetup() {
  SPI1.setTX(EINK_DIN);
  SPI1.setSCK(EINK_CLK);
  SPI1.begin();

  _eink.init(115200, true, 10, false, SPI1, SPISettings(4000000, MSBFIRST, SPI_MODE0));
  _eink.setFullWindow();

  // Startup screen
  _eink.firstPage();
  do {
    _eink.fillScreen(GxEPD_WHITE);
    _eink.setFont(&FreeMonoBold9pt7b);
    _centred("BuddyBot", 88);
    _eink.setFont(&FreeMono9pt7b);
    _centred("Starting up...", 108);
  } while (_eink.nextPage());

  _einkReady       = true;
  _einkLastRefresh = millis();
}

// Call every iteration of loop() on Core 0.
// Triggers a full redraw every EINK_REFRESH_MS (default 30 s).
// WARNING: blocks for ~8 s during the e-ink refresh -- this is normal.
void einkLoop() {
  if (!_einkReady) return;
  if (millis() - _einkLastRefresh < EINK_REFRESH_MS) return;

  _einkLastRefresh = millis();
  _eink.setFullWindow();
  _eink.firstPage();
  do { _einkDraw(); } while (_eink.nextPage());
}