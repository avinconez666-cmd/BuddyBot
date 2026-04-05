/*
 * ════════════════════════════════════════════════════════════════════
 *  BUDDYBOT V23.0  ·  UNO R4 WiFi  ·  ULTIMATE COMMS CENTRE
 * ════════════════════════════════════════════════════════════════════
 *  HX8347D 240×320  ·  UNOPAR8 bus  ·  TouchScreen  ·  Serial1→Mega
 *
 *  FEATURES:
 *    ★ 10-second animated matrix-rain splash → "BuddyBot Online"
 *    ★ Scrolling rainbow neon "AJ2BUDDYCOMMS" header on all screens
 *    ★ TECH STATS → Sensor Info (live ultrasonic HUD) + Serial Comms
 *    ★ AJ'S GAMES → Count Stars · Shape Match · Colour Quest · Letter Hunt
 *    ★ MODE → Normal · Bodyguard · Guard Dog  (sends command to Mega)
 *    ★ Real-time 4-sensor top-down visual HUD with danger colouring
 *    ★ Session score, streak tracking, particle celebration
 *
 *  MEGA SERIAL STRINGS PARSED:
 *    STAT:gas:temp:hum:haz:lat:lon:sats:volt:pct:amp:pwr:tz1:tz2
 *    PWR:volt:amps:watts:mah:pct:z1:z2:peak:avg:wh
 *    US:front,rear,left,right      ← add to Mega sendTelemetryToR4()
 *    STATUS|...|END                ← parsed for estop/auto flags
 * ════════════════════════════════════════════════════════════════════
 */

#include <Arduino_GFX_Library.h>
#include <TouchScreen.h>
#include <math.h>

// ─── Hardware ─────────────────────────────────────────────────────────
Arduino_DataBus *bus = new Arduino_UNOPAR8();
Arduino_GFX     *gfx = new Arduino_HX8347D(bus, A4, 0);
TouchScreen       ts  = TouchScreen(8, A3, A2, 9, 300);

// ─── Touchscreen calibration ──────────────────────────────────────────
#define TS_MINX 130
#define TS_MAXX 900
#define TS_MINY 100
#define TS_MAXY 920

// ─── Colour Palette ───────────────────────────────────────────────────
#define C_BLACK   0x0000
#define C_WHITE   0xFFFF
#define C_DKBG    0x0841
#define C_DGLASS  0x1082
#define C_GRAY    0x4208
#define C_LGRAY   0x8410
#define C_NCYAN   0x07FF
#define C_NMAG    0xF81F
#define C_NLIME   0x07E0
#define C_NYEL    0xFFE0
#define C_NORG    0xFC60
#define C_NPUR    0x801F
#define C_DNCYAN  0x0398
#define G_RED     0xF800
#define G_BLUE    0x021F
#define G_GREEN   0x07E0
#define G_YELLOW  0xFFE0
#define G_ORANGE  0xFC60
#define G_PURPLE  0x801F
#define G_TEAL    0x07FF
#define G_CORAL   0xFB0C
#define G_LIME    0x87E0

// ─── Screen Layout ────────────────────────────────────────────────────
#define SCR_W   240
#define SCR_H   320
#define HDR_H    48    // scrolling header height
#define PBAR_Y  298    // power bar Y
#define PBAR_H   22    // power bar height
// Content area: HDR_H → PBAR_Y  = 250px
#define CNT_Y   HDR_H
#define CNT_H   (PBAR_Y - HDR_H)

// Game layout
#define SCORE_Y  HDR_H
#define SCORE_H  24
#define QBOX_Y  (HDR_H + SCORE_H)   // = 72
#define QBOX_H  114                  // question panel height (was 130 — caused overflow)
#define ANS_Y   (QBOX_Y + QBOX_H + 2)  // = 188
// 2×2 answer buttons: 104×40, row spacing 44px
//   row 0: 188–228   row 1: 232–272   back: 275–295   pbar: 298+
#define ANS2_H   40   // 2×2 button height
#define ANS2_SP  44   // 2×2 row stride
#define BACK_X    4
#define BACK_Y  275   // was 268 — now safely below row-1 bottom (272)
#define BACK_W   58
#define BACK_H   20   // was 26 — fits between row-1 (272) and power bar (298)

// ─── App State Machine ────────────────────────────────────────────────
enum AppState : uint8_t {
  S_SPLASH = 0,
  S_MAIN,
  S_TECH,      // Tech Stats folder
  S_SENSORS,   // Live sensor info + US HUD
  S_SERIAL,    // Serial comms log
  S_GAMES,     // Games menu
  S_MODE,      // Mode selector
  S_CNT,       // Count Stars
  S_SHP,       // Shape Match
  S_CLR,       // Colour Quest
  S_LTR,       // Letter Hunt
  S_CELEB      // Particle celebration
};
AppState appState   = S_SPLASH;
AppState returnGame = S_CNT;

// ─── Telemetry ────────────────────────────────────────────────────────
float   botVolt = 8.4f, botAmps = 0.0f, botTemp = 25.0f;
int     botPct  = 100;
int     botGas  = 0;
float   botHum  = 50.0f;
bool    botHaz  = false, botEstop = false, botAuto = false;
int16_t sF = -1, sR = -1, sL = -1, sRi = -1;   // ultrasonic cm

// ─── Serial Log ───────────────────────────────────────────────────────
#define LOG_N   9
#define LOG_W  28
char    logLines[LOG_N][LOG_W];
uint8_t logHead  = 0;
uint8_t logCount = 0;
bool    logDirty = false;

void addLog(const char* tag, const char* msg) {
  snprintf(logLines[logHead], LOG_W, "[%s]%s", tag, msg);
  logHead = (logHead + 1) % LOG_N;
  if (logCount < LOG_N) logCount++;
  logDirty = true;
}

// ─── Mode ─────────────────────────────────────────────────────────────
uint8_t botMode = 0;
const char*    MODE_NAME[3] = { "NORMAL", "BODYGUARD", "GUARD DOG" };
const char*    MODE_ICON[3] = { ":)", "[]", ">_<" };
const uint16_t MODE_COL[3]  = { C_NLIME, C_NCYAN, G_CORAL };
const uint16_t MODE_BG[3]   = { 0x0420, 0x0218, 0x3000 };

// ─── Scrolling Header State ───────────────────────────────────────────
int16_t  hdrOfs  = SCR_W;       // start off right edge
uint8_t  hdrPalI = 0;
uint8_t  hdrTick = 0;
unsigned long hdrMs = 0;
const uint16_t HDR_PAL[] = {
  0x07FF, 0xF81F, 0x07E0, 0xFFE0, 0xFC60, 0x801F, 0xFB56, 0x027F
};
#define HDR_PAL_N 8

// ─── Matrix Rain (splash) ─────────────────────────────────────────────
#define RAIN_N 12
struct Drop { int16_t y; uint8_t spd, tail; };
Drop rain[RAIN_N];

// ─── Games: Session ───────────────────────────────────────────────────
uint8_t  sesStars = 0, sesTotal = 0, streak = 0, hiStreak = 0;
bool     awaitIn  = false;
bool     inFB     = false;
bool     lastOK   = false;
unsigned long fbMs = 0;
#define FB_DUR 1600

// ─── Games: Data ──────────────────────────────────────────────────────
uint8_t cntTgt, cntOpt[3];
uint8_t shpQ, shpOpt[4];
uint8_t clrQ, clrOpt[4];
uint8_t ltrQ, ltrOpt[4];

const char*    SHP_NAME[4] = { "CIRCLE", "TRIANGLE", "SQUARE", "STAR" };
const uint16_t SHP_COL[4]  = { C_NCYAN, C_NMAG, G_YELLOW, G_CORAL };
const char*    CLR_NAME[6] = { "RED", "BLUE", "GREEN", "YELLOW", "ORANGE", "PURPLE" };
const uint16_t CLR_VAL[6]  = { G_RED, G_BLUE, G_GREEN, G_YELLOW, G_ORANGE, G_PURPLE };
const char     LTR_SET[8]  = { 'A','B','C','D','E','F','G','H' };

// ─── Celebration Particles ────────────────────────────────────────────
struct Particle { int16_t x, y, vx, vy; uint16_t col; };
Particle pts[18];
uint8_t  celFr = 0;
unsigned long celMs = 0;

// ─── Sensor refresh ───────────────────────────────────────────────────
unsigned long sensRefMs = 0;
bool          sensNeedsRedraw = false;

// ─── Forward declarations ─────────────────────────────────────────────
void drawScrollHeader();
void drawPowerBar();
void drawBackBtn();
void drawMainMenu();
void nextQuestion();

// ══════════════════════════════════════════════════════════════════════
//  DRAWING PRIMITIVES
// ══════════════════════════════════════════════════════════════════════

void restorePins() {
  pinMode(8, OUTPUT); pinMode(9, OUTPUT);
  pinMode(A2, OUTPUT); pinMode(A3, OUTPUT);
}

// Glow panel: filled dark glass + double neon border
void drawPanel(int x, int y, int w, int h, uint16_t col) {
  gfx->fillRoundRect(x,   y,   w,   h,   8, C_DGLASS);
  gfx->drawRoundRect(x,   y,   w,   h,   8, col);
  gfx->drawRoundRect(x+1, y+1, w-2, h-2, 7, col);
}

// Solid fill button (for colour game)
void drawSolidBtn(int x, int y, int w, int h, uint16_t fill, uint16_t brd) {
  gfx->fillRoundRect(x,   y,   w,   h,   8, fill);
  gfx->drawRoundRect(x,   y,   w,   h,   8, brd);
  gfx->drawRoundRect(x+1, y+1, w-2, h-2, 7, C_WHITE);
}

// 5-point filled star
void drawStar(int cx, int cy, int ro, int ri, uint16_t col) {
  float a = -1.5708f; // -π/2
  int px[10], py[10];
  for (int i = 0; i < 10; i++) {
    float r = (i & 1) ? (float)ri : (float)ro;
    px[i] = cx + (int)(r * cosf(a));
    py[i] = cy + (int)(r * sinf(a));
    a += 0.6283f; // π/5
  }
  for (int i = 0; i < 10; i++) {
    int j = (i + 1) % 10;
    gfx->fillTriangle(cx, cy, px[i], py[i], px[j], py[j], col);
  }
}

// Shape dispatch (0=circle 1=triangle 2=square 3=star)
void drawShape(uint8_t id, int cx, int cy, int sz, uint16_t col) {
  switch (id) {
    case 0: gfx->fillCircle(cx, cy, sz, col); break;
    case 1: gfx->fillTriangle(cx, cy-sz, cx-sz, cy+sz, cx+sz, cy+sz, col); break;
    case 2: gfx->fillRect(cx-sz, cy-sz, sz*2, sz*2, col); break;
    case 3: drawStar(cx, cy, sz, sz/2, col); break;
  }
}

// Back button (< BACK)
void drawBackBtn() {
  gfx->drawRoundRect(BACK_X, BACK_Y, BACK_W, BACK_H, 5, C_GRAY);
  gfx->setTextSize(1); gfx->setTextColor(C_LGRAY);
  gfx->setCursor(BACK_X+7, BACK_Y+9);
  gfx->print("< BACK");
}

// Power bar (bottom strip)
void drawPowerBar() {
  gfx->fillRect(0, PBAR_Y, SCR_W, PBAR_H, C_DKBG);
  gfx->drawRect(0, PBAR_Y, SCR_W, PBAR_H, C_NCYAN);
  uint16_t vc = (botVolt > 7.0f) ? C_NLIME : G_CORAL;
  gfx->setTextSize(1); gfx->setTextColor(vc);
  gfx->setCursor(4, PBAR_Y + 7);
  char pb[40];
  snprintf(pb, sizeof(pb), "PWR:%.1fV %dA=%.0fW  T:%.0fC  BAT:%d%%",
           botVolt, (int)botAmps, botVolt * botAmps, botTemp, botPct);
  gfx->print(pb);
}

// Score bar (in-game)
void drawScoreBar() {
  gfx->fillRect(0, SCORE_Y, SCR_W, SCORE_H, C_DKBG);
  gfx->drawFastHLine(0, SCORE_Y + SCORE_H - 1, SCR_W, C_GRAY);
  for (int i = 0; i < 5; i++) {
    uint16_t c = (i < (sesStars % 6)) ? G_YELLOW : C_GRAY;
    drawStar(12 + i * 22, SCORE_Y + 12, 7, 3, c);
  }
  gfx->setTextSize(1); gfx->setTextColor(C_LGRAY);
  char buf[16];
  snprintf(buf, sizeof(buf), "%d/%d STK:%d", sesStars, sesTotal, streak);
  gfx->setCursor(SCR_W - 82, SCORE_Y + 8);
  gfx->print(buf);
}

// Feedback bar (below answer buttons)
void drawFeedback(bool ok) {
  uint16_t bg  = ok ? 0x0440 : 0x6000;
  uint16_t brd = ok ? G_LIME  : G_CORAL;
  gfx->fillRoundRect(4, 260, 232, 32, 7, bg);
  gfx->drawRoundRect(4, 260, 232, 32, 7, brd);
  gfx->setTextSize(2); gfx->setTextColor(C_WHITE);
  if (ok) { gfx->setCursor(44, 268); gfx->print("BRILLIANT!"); }
  else    { gfx->setCursor(22, 268); gfx->print("TRY AGAIN!"); }
}

void clearFeedback() {
  gfx->fillRect(4, 260, 232, 32, C_BLACK);
}

// ══════════════════════════════════════════════════════════════════════
//  SCROLLING NEON HEADER
// ══════════════════════════════════════════════════════════════════════

void drawScrollHeader() {
  gfx->fillRect(0, 0, SCR_W, HDR_H, C_DKBG);

  // Decorative circuit lines
  uint16_t lc = HDR_PAL[hdrPalI];
  uint16_t lc2 = HDR_PAL[(hdrPalI + 3) % HDR_PAL_N];
  gfx->drawFastHLine(0, HDR_H - 4, SCR_W, lc);
  gfx->drawFastHLine(0, HDR_H - 2, SCR_W, lc2);
  gfx->drawFastHLine(0, HDR_H - 1, SCR_W, 0x0821);

  // Tiny corner nodes on the lines
  gfx->fillRect(0, HDR_H - 7, 5, 5, lc);
  gfx->fillRect(SCR_W - 5, HDR_H - 7, 5, 5, lc);

  // Scrolling text — drawn at hdrOfs, and again at hdrOfs + textWidth for seamless wrap
  const char* txt = "   AJ2BUDDYCOMMS   ";
  int charW = 12; // textSize 2 → 6px × 2 scale = 12px per char
  int txtW  = strlen(txt) * charW;

  gfx->setTextSize(2);

  // Draw two copies so the loop is seamless
  for (int pass = 0; pass < 3; pass++) {
    int x = hdrOfs + pass * txtW;
    if (x > SCR_W) break;
    if (x + txtW < 0) continue;

    // Each character gets its own hue for a rainbow effect
    for (int ci = 0; ci < (int)strlen(txt); ci++) {
      int cx = x + ci * charW;
      if (cx < -charW || cx > SCR_W) continue;
      uint16_t cc = HDR_PAL[(hdrPalI + ci) % HDR_PAL_N];
      gfx->setTextColor(cc);
      gfx->setCursor(cx, 13);
      char ch[2] = { txt[ci], 0 };
      gfx->print(ch);
    }
  }
}

void tickHeader() {
  if (millis() - hdrMs < 38) return; // ~26fps
  hdrMs = millis();
  hdrOfs -= 2;
  const char* txt = "   AJ2BUDDYCOMMS   ";
  int txtW = strlen(txt) * 12;
  if (hdrOfs < -txtW) hdrOfs = 0;
  hdrTick++;
  if (hdrTick >= 12) { hdrTick = 0; hdrPalI = (hdrPalI + 1) % HDR_PAL_N; }
  drawScrollHeader();
}

// ══════════════════════════════════════════════════════════════════════
//  SPLASH SCREEN  (blocking 10 seconds)
// ══════════════════════════════════════════════════════════════════════

void initRain() {
  for (int i = 0; i < RAIN_N; i++) {
    rain[i].y   = -(int16_t)random(20, 250);
    rain[i].spd  = random(3, 7);
    rain[i].tail = random(4, 10);
  }
}

void tickRain() {
  static char chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!@#$%^&*";
  for (int c = 0; c < RAIN_N; c++) {
    int x = c * 20 + 4;

    // Erase tail
    int te = rain[c].y - (int)rain[c].tail * 9;
    if (te >= 0 && te < SCR_H)
      gfx->fillRect(x, te, 14, 9, C_BLACK);

    // Draw head
    if (rain[c].y >= 0 && rain[c].y < SCR_H) {
      gfx->setTextSize(1); gfx->setTextColor(0x07E0);
      gfx->setCursor(x, rain[c].y);
      gfx->print(chars[random(sizeof(chars) - 1)]);
    }
    // Draw mid-trail
    int tm = rain[c].y - (int)rain[c].tail * 4;
    if (tm >= 0 && tm < SCR_H) {
      gfx->setTextColor(0x0240);
      gfx->setCursor(x, tm);
      gfx->print(chars[random(sizeof(chars) - 1)]);
    }

    rain[c].y += rain[c].spd;
    if (rain[c].y > SCR_H + (int)rain[c].tail * 9) {
      rain[c].y   = -(int16_t)random(10, 120);
      rain[c].spd  = random(3, 7);
      rain[c].tail = random(4, 10);
    }
  }
}

void doSplash() {
  gfx->fillScreen(C_BLACK);
  initRain();

  // ── Phase 1: Matrix rain 7 seconds ──────────────────────────────────
  unsigned long t0 = millis();
  while (millis() - t0 < 7000) {
    tickRain();

    // Dim "BOOT..." in corner, pulsing
    if ((millis() / 600) % 2 == 0) {
      gfx->setTextSize(1); gfx->setTextColor(0x0180);
      gfx->setCursor(4, 3); gfx->print("BOOT SEQUENCE...");
    } else {
      gfx->fillRect(4, 3, 130, 9, C_BLACK);
    }
    // Progress tick marks along bottom
    int prog = map(millis() - t0, 0, 7000, 0, SCR_W);
    gfx->drawFastHLine(0, SCR_H - 2, prog, 0x0300);

    delay(42);
  }

  // ── Phase 2: Clear to black, show "BUDDYBOT" typewriter ─────────────
  gfx->fillScreen(C_BLACK);

  // Scan-line wipe for drama
  for (int y = SCR_H; y >= 0; y -= 6) {
    gfx->drawFastHLine(0, y, SCR_W, C_NCYAN);
    if (y % 30 == 0) delay(2);
    gfx->drawFastHLine(0, y, SCR_W, C_BLACK);
  }

  // "BUDDYBOT" — textSize 4 = 24px/char, 8 chars = 192px, centred at x=24
  const char* bb = "BUDDYBOT";
  gfx->setTextSize(4); gfx->setTextColor(C_NCYAN);
  gfx->setCursor(24, 90);
  for (int i = 0; i < 8; i++) {
    char ch[2] = { bb[i], 0 };
    gfx->print(ch);
    // Brief cursor blink
    int cx = 24 + (i + 1) * 24;
    gfx->fillRect(cx, 90, 3, 32, C_NCYAN);
    delay(100);
    gfx->fillRect(cx, 90, 3, 32, C_BLACK);
  }
  delay(250);

  // Glow underline
  for (int i = 0; i < 3; i++) {
    gfx->drawFastHLine(24, 128, 192, (i % 2 == 0) ? C_NCYAN : C_DNCYAN);
    delay(60);
  }
  gfx->drawFastHLine(24, 128, 192, C_NCYAN);
  gfx->drawFastHLine(24, 130, 192, C_NMAG);

  // ── Phase 3: "Online" typewriter ────────────────────────────────────
  delay(200);
  const char* ol = "Online";
  gfx->setTextSize(3); gfx->setTextColor(C_NLIME);
  gfx->setCursor(54, 145);
  for (int i = 0; i < 6; i++) {
    char ch[2] = { ol[i], 0 };
    gfx->print(ch);
    delay(140);
  }

  // Connection dots
  gfx->setTextSize(1); gfx->setTextColor(C_LGRAY);
  gfx->setCursor(72, 185); gfx->print("ESTABLISHING LINK...");

  // ── Phase 4: Loading bar ────────────────────────────────────────────
  gfx->drawRect(20, 200, 200, 18, C_NCYAN);
  for (int i = 0; i <= 198; i += 2) {
    uint16_t c = (i < 66)  ? C_NCYAN :
                 (i < 132) ? C_NMAG  : C_NLIME;
    gfx->fillRect(21, 201, i, 16, c);

    // Percentage label
    if (i % 20 == 0) {
      gfx->fillRect(92, 222, 56, 9, C_BLACK);
      gfx->setTextSize(1); gfx->setTextColor(C_LGRAY);
      char pct[8]; snprintf(pct, sizeof(pct), "%d%%", (int)((i * 100) / 198));
      gfx->setCursor(106, 222); gfx->print(pct);
    }
    delay(9); // ~900ms total
  }

  // ── Phase 5: "READY" flash transition ───────────────────────────────
  gfx->fillRect(20, 222, 200, 12, C_BLACK);
  gfx->setTextSize(2); gfx->setTextColor(G_YELLOW);
  gfx->setCursor(82, 225); gfx->print("READY!");

  // 3 quick flashes
  for (int f = 0; f < 3; f++) {
    gfx->fillScreen(0x07FF);
    delay(70);
    gfx->fillScreen(C_BLACK);
    gfx->setTextSize(4); gfx->setTextColor(C_NCYAN);
    gfx->setCursor(24, 90); gfx->print("BUDDYBOT");
    gfx->setTextSize(3); gfx->setTextColor(C_NLIME);
    gfx->setCursor(54, 145); gfx->print("Online");
    gfx->setTextSize(2); gfx->setTextColor(G_YELLOW);
    gfx->setCursor(82, 225); gfx->print("READY!");
    delay(70);
  }
  delay(300);
}

// ══════════════════════════════════════════════════════════════════════
//  MAIN MENU
// ══════════════════════════════════════════════════════════════════════

// Draws the icon area inside each main menu button
void drawMenuIcon(int cx, int cy, uint16_t col, uint8_t id) {
  switch (id) {
    case 0: { // CPU / Tech Stats
      gfx->fillRect(cx-11, cy-11, 22, 22, 0x0421);
      gfx->drawRect(cx-11, cy-11, 22, 22, col);
      for (int p = 0; p < 3; p++) {
        gfx->drawFastHLine(cx-19, cy-7+p*7, 8, col);
        gfx->drawFastHLine(cx+11, cy-7+p*7, 8, col);
      }
      gfx->drawRect(cx-5, cy-5, 10, 10, col);
      gfx->fillCircle(cx, cy, 3, col);
      break;
    }
    case 1: { // Games — 4 mini shapes
      drawStar(cx-9, cy-9, 8, 3, G_YELLOW);
      gfx->fillCircle(cx+9, cy-9, 7, C_NCYAN);
      gfx->fillRect(cx-16, cy+1, 13, 13, G_CORAL);
      gfx->fillTriangle(cx+9,cy+1, cx+2,cy+14, cx+16,cy+14, C_NLIME);
      break;
    }
    case 2: { // Robot face (mode-aware)
      uint16_t eyeC = MODE_COL[botMode];
      gfx->fillRoundRect(cx-12, cy-14, 24, 20, 4, 0x0421);
      gfx->drawRoundRect(cx-12, cy-14, 24, 20, 4, col);
      gfx->fillCircle(cx-5, cy-8, 3, eyeC);
      gfx->fillCircle(cx+5, cy-8, 3, eyeC);
      if (botMode == 0) {  // smile
        gfx->drawPixel(cx-5, cy-1, eyeC);
        gfx->drawFastHLine(cx-4, cy, 8, eyeC);
        gfx->drawPixel(cx+4, cy-1, eyeC);
      } else {             // stern
        gfx->drawFastHLine(cx-5, cy-1, 10, eyeC);
      }
      gfx->drawFastVLine(cx, cy-14, -5, col);
      gfx->fillCircle(cx, cy-19, 3, col);
      gfx->fillRoundRect(cx-8, cy+6, 16, 8, 2, 0x0421);
      gfx->drawRoundRect(cx-8, cy+6, 16, 8, 2, col);
      break;
    }
  }
}

// One tall main-menu button
void drawMainBtn(int idx) {
  static const char* TITLES[3] = { "TECH STATS", "AJ'S GAMES", "MODE" };
  static const char* SUBS[3]   = { "Sensors & Comms", "4 Fun Activities", "" };
  static const uint16_t COLS[3] = { C_NCYAN, C_NYEL, C_NORG };

  const int BH = 72, BX = 5, BW = 230;
  int by = CNT_Y + 8 + idx * (BH + 8);
  uint16_t col = COLS[idx];

  // Outer glow
  gfx->fillRoundRect(BX,   by,   BW,   BH,   10, C_DKBG);
  gfx->fillRoundRect(BX,   by,   BW,   8,    5,  col);    // coloured top bar
  gfx->drawRoundRect(BX,   by,   BW,   BH,   10, col);
  gfx->drawRoundRect(BX+1, by+1, BW-2, BH-2, 9,  col);

  // Icon box
  gfx->fillRoundRect(BX+5, by+10, 52, BH-14, 7, 0x0821);
  gfx->drawRoundRect(BX+5, by+10, 52, BH-14, 7, col);
  drawMenuIcon(BX + 5 + 26, by + 10 + (BH-14)/2, col, idx);

  // Title
  gfx->setTextSize(2); gfx->setTextColor(C_WHITE);
  gfx->setCursor(BX + 66, by + 14);
  gfx->print(TITLES[idx]);

  // Subtitle / mode name
  gfx->setTextSize(1); gfx->setTextColor(col);
  gfx->setCursor(BX + 66, by + 38);
  if (idx == 2) gfx->print(MODE_NAME[botMode]);
  else          gfx->print(SUBS[idx]);

  // Chevron
  gfx->setTextSize(2); gfx->setTextColor(col);
  gfx->setCursor(BX + BW - 22, by + BH/2 - 8);
  gfx->print(">");
}

void drawMainMenu() {
  appState = S_MAIN;
  gfx->fillScreen(C_DKBG);
  drawScrollHeader();
  for (int i = 0; i < 3; i++) drawMainBtn(i);
  drawPowerBar();
}

// ══════════════════════════════════════════════════════════════════════
//  TECH STATS MENU
// ══════════════════════════════════════════════════════════════════════

void drawSubHeader(const char* title, uint16_t col) {
  gfx->fillRect(0, 0, SCR_W, HDR_H, C_DKBG);
  gfx->drawFastHLine(0, HDR_H-3, SCR_W, col);
  gfx->drawFastHLine(0, HDR_H-1, SCR_W, C_NMAG);
  gfx->setTextSize(2); gfx->setTextColor(col);
  int tw = strlen(title) * 12;
  gfx->setCursor((SCR_W - tw) / 2, 14);
  gfx->print(title);
}

void drawTechMenu() {
  appState = S_TECH;
  gfx->fillScreen(C_DKBG);
  drawScrollHeader();

  // Banner
  gfx->fillRoundRect(8, CNT_Y + 6, 224, 28, 7, 0x0214);
  gfx->setTextSize(2); gfx->setTextColor(C_NCYAN);
  gfx->setCursor(20, CNT_Y + 12); gfx->print("TECH STATS");

  // Subfolder 1: Sensor Info
  drawPanel(8, CNT_Y + 44, 224, 80, C_NCYAN);
  // Mini sensor icon
  gfx->fillRect(20, CNT_Y + 66, 28, 28, 0x0421);
  gfx->drawRect(20, CNT_Y + 66, 28, 28, C_NCYAN);
  for (int p = 0; p < 3; p++)
    gfx->drawFastHLine(12, CNT_Y + 70 + p*9, 8, C_NCYAN);
  gfx->fillCircle(34, CNT_Y + 80, 5, C_NCYAN);
  gfx->setTextSize(2); gfx->setTextColor(C_WHITE);
  gfx->setCursor(58, CNT_Y + 56); gfx->print("SENSOR INFO");
  gfx->setTextSize(1); gfx->setTextColor(C_NCYAN);
  gfx->setCursor(58, CNT_Y + 78); gfx->print("Live sensor readings + US HUD");
  gfx->setTextSize(2); gfx->setTextColor(C_NCYAN);
  gfx->setCursor(SCR_W - 28, CNT_Y + 72); gfx->print(">");

  // Subfolder 2: Serial Comms
  drawPanel(8, CNT_Y + 134, 224, 80, C_NMAG);
  // Mini terminal icon
  gfx->fillRect(20, CNT_Y + 156, 28, 28, 0x2804);
  gfx->drawRect(20, CNT_Y + 156, 28, 28, C_NMAG);
  gfx->setTextSize(1); gfx->setTextColor(C_NMAG);
  gfx->setCursor(22, CNT_Y + 160); gfx->print(">");
  gfx->setCursor(22, CNT_Y + 170); gfx->print("---");
  gfx->setCursor(22, CNT_Y + 180); gfx->print(">>_");
  gfx->setTextSize(2); gfx->setTextColor(C_WHITE);
  gfx->setCursor(58, CNT_Y + 146); gfx->print("SERIAL COMMS");
  gfx->setTextSize(1); gfx->setTextColor(C_NMAG);
  gfx->setCursor(58, CNT_Y + 168); gfx->print("Real-time serial log");
  gfx->setTextSize(2); gfx->setTextColor(C_NMAG);
  gfx->setCursor(SCR_W - 28, CNT_Y + 162); gfx->print(">");

  // Back
  drawPanel(65, CNT_Y + 228, 110, 24, C_GRAY);
  gfx->setTextSize(1); gfx->setTextColor(C_LGRAY);
  gfx->setCursor(80, CNT_Y + 234); gfx->print("< MAIN MENU");

  drawPowerBar();
}

// ══════════════════════════════════════════════════════════════════════
//  SENSOR INFO — Live Ultrasonic HUD + Environmental Data
// ══════════════════════════════════════════════════════════════════════

// Convert distance to beam pixel length (max 55px = 200cm)
int beamLen(int16_t d) {
  if (d < 0) return 12;
  return constrain(map(d, 0, 200, 2, 55), 2, 55);
}

// Colour-code distance
uint16_t distCol(int16_t d) {
  if (d < 0)   return C_GRAY;
  if (d < 20)  return G_RED;
  if (d < 70)  return G_YELLOW;
  return G_GREEN;
}

void drawUSHUD() {
  // Bounding box for the HUD canvas: x=8..232, y=66..196 (130px tall, 224px wide)
  const int CX = 120, CY = 131;   // robot centre
  const int RW = 18,  RH = 26;    // robot half-width/height

  // Clear HUD area
  gfx->fillRect(8, 66, 224, 130, C_DKBG);
  gfx->drawRect(8, 66, 224, 130, 0x2104);

  // Compass labels
  gfx->setTextSize(1); gfx->setTextColor(C_LGRAY);
  gfx->setCursor(CX-6, 68); gfx->print("F");
  gfx->setCursor(CX-6, 183); gfx->print("R");
  gfx->setCursor(10, CY-4);  gfx->print("L");
  gfx->setCursor(222, CY-4); gfx->print("R");

  // Draw sensor beams
  int bF  = beamLen(sF);
  int bRr = beamLen(sR);
  int bL  = beamLen(sL);
  int bRi = beamLen(sRi);

  // Front beam (up from robot top)
  gfx->fillRect(CX-4, CY-RH-bF, 8, bF, distCol(sF));
  // Rear beam (down from robot bottom)
  gfx->fillRect(CX-4, CY+RH, 8, bRr, distCol(sR));
  // Left beam (left from robot left)
  gfx->fillRect(CX-RW-bL, CY-4, bL, 8, distCol(sL));
  // Right beam (right from robot right)
  gfx->fillRect(CX+RW, CY-4, bRi, 8, distCol(sRi));

  // Danger circles (if very close)
  if (sF  >= 0 && sF  < 20) gfx->drawCircle(CX, CY, 40, G_RED);
  if (sR  >= 0 && sR  < 20) gfx->drawCircle(CX, CY, 40, G_RED);

  // Robot body
  gfx->fillRoundRect(CX-RW, CY-RH, RW*2, RH*2, 5, 0x0C42);
  gfx->drawRoundRect(CX-RW, CY-RH, RW*2, RH*2, 5, C_NCYAN);
  // Eyes
  gfx->fillCircle(CX-7, CY-8, 4, MODE_COL[botMode]);
  gfx->fillCircle(CX+7, CY-8, 4, MODE_COL[botMode]);
  // Body detail
  gfx->drawFastHLine(CX-8, CY+2, 16, C_NCYAN);
  gfx->drawRect(CX-5, CY+6, 10, 6, C_NCYAN);

  // Distance labels at beam tips
  gfx->setTextSize(1);
  // Front label
  gfx->setTextColor(distCol(sF));
  char buf[6];
  snprintf(buf, sizeof(buf), "%d", (int)(sF >= 0 ? sF : 0));
  gfx->setCursor(CX + 6, CY - RH - bF);
  gfx->print(sF < 0 ? "?" : buf);
  // Rear label
  gfx->setTextColor(distCol(sR));
  snprintf(buf, sizeof(buf), "%d", (int)(sR >= 0 ? sR : 0));
  gfx->setCursor(CX + 6, CY + RH + bRr - 8);
  gfx->print(sR < 0 ? "?" : buf);
  // Left label
  gfx->setTextColor(distCol(sL));
  snprintf(buf, sizeof(buf), "%d", (int)(sL >= 0 ? sL : 0));
  gfx->setCursor(CX - RW - bL, CY - 14);
  gfx->print(sL < 0 ? "?" : buf);
  // Right label
  gfx->setTextColor(distCol(sRi));
  snprintf(buf, sizeof(buf), "%d", (int)(sRi >= 0 ? sRi : 0));
  gfx->setCursor(CX + RW + bRi, CY - 14);
  gfx->print(sRi < 0 ? "?" : buf);
}

void drawEnvRow(int y, const char* label, const char* val, uint16_t col) {
  gfx->setTextSize(1); gfx->setTextColor(C_LGRAY);
  gfx->setCursor(10, y); gfx->print(label);
  gfx->setTextSize(1); gfx->setTextColor(col);
  gfx->setCursor(90, y); gfx->print(val);
  gfx->drawFastHLine(8, y + 10, 224, 0x0821);
}

void drawSensorInfo() {
  appState = S_SENSORS;
  gfx->fillScreen(C_DKBG);
  drawSubHeader("SENSOR INFO", C_NCYAN);

  // US HUD label
  gfx->setTextSize(1); gfx->setTextColor(C_LGRAY);
  gfx->setCursor(10, HDR_H + 4); gfx->print("ULTRASONIC HUD  (cm, colour = danger)");
  drawUSHUD();

  // Environmental data strip
  gfx->drawFastHLine(0, 200, SCR_W, C_NCYAN);
  char v[16];
  snprintf(v, sizeof(v), "%.1f C", botTemp);
  drawEnvRow(205, "TEMPERATURE:", v, C_NYEL);
  snprintf(v, sizeof(v), "%.0f %%", botHum);
  drawEnvRow(218, "HUMIDITY:   ", v, C_NCYAN);
  snprintf(v, sizeof(v), "%d", botGas);
  drawEnvRow(231, "GAS LEVEL:  ", v, botGas > 400 ? G_CORAL : C_NLIME);
  snprintf(v, sizeof(v), "%.1fV  %d%%", botVolt, botPct);
  drawEnvRow(244, "BATTERY:    ", v, botPct < 20 ? G_CORAL : C_NLIME);
  snprintf(v, sizeof(v), "%s", botEstop ? "E-STOP!" : (botAuto ? "AUTO" : "MANUAL"));
  drawEnvRow(257, "BOT STATE:  ", v, botEstop ? G_RED : C_NLIME);

  drawBackBtn();
  drawPowerBar();
}

// ══════════════════════════════════════════════════════════════════════
//  SERIAL COMMS SCREEN
// ══════════════════════════════════════════════════════════════════════

uint16_t logLineColor(const char* line) {
  if (strstr(line, "STAT"))    return C_NCYAN;
  if (strstr(line, "PWR"))     return C_NYEL;
  if (strstr(line, "US"))      return C_NLIME;
  if (strstr(line, "MODE"))    return C_NORG;
  if (strstr(line, "GESTURE")) return C_NMAG;
  if (strstr(line, "STATUS"))  return 0xFB56;
  if (strstr(line, "ERR") || strstr(line, "CRITICAL")) return G_CORAL;
  return C_LGRAY;
}

void drawSerialScreen() {
  appState = S_SERIAL;
  gfx->fillScreen(C_DKBG);
  drawSubHeader("SERIAL COMMS", C_NMAG);

  // Terminal scanline background
  for (int y = HDR_H; y < PBAR_Y - 26; y += 14)
    gfx->drawFastHLine(0, y, SCR_W, 0x0821);

  // Log lines
  for (int i = 0; i < min((int)logCount, LOG_N); i++) {
    int lineIdx = ((int)logHead - (int)logCount + i + LOG_N * 2) % LOG_N;
    int ly = HDR_H + 6 + i * 22;
    gfx->fillRect(4, ly, 232, 20, 0x1082);
    gfx->drawRect(4, ly, 232, 20, 0x2084);
    gfx->setTextSize(1);
    gfx->setTextColor(logLineColor(logLines[lineIdx]));
    gfx->setCursor(8, ly + 6);
    gfx->print(logLines[lineIdx]);
  }

  if (logCount == 0) {
    gfx->setTextSize(1); gfx->setTextColor(C_GRAY);
    gfx->setCursor(60, 140); gfx->print("Waiting for data...");
  }

  drawBackBtn();
  drawPowerBar();
  logDirty = false;
}

void refreshSerialLog() {
  if (!logDirty || appState != S_SERIAL) return;
  drawSerialScreen();
}

// ══════════════════════════════════════════════════════════════════════
//  GAMES MENU
// ══════════════════════════════════════════════════════════════════════

void drawGameTile(int x, int y, const char* l1, const char* l2,
                  uint16_t col, uint8_t iconId) {
  const int W = 108, H = 88;
  gfx->fillRoundRect(x, y, W, H, 9, C_DGLASS);
  gfx->drawRoundRect(x,   y,   W,   H,   9, col);
  gfx->drawRoundRect(x+1, y+1, W-2, H-2, 8, col);
  gfx->fillRoundRect(x, y, W, 8, 4, col); // top accent bar

  // Mini icon
  int icx = x + 24, icy = y + H/2;
  switch (iconId) {
    case 0: drawStar(icx, icy, 14, 6, col); break;
    case 1: drawShape(2, icx, icy, 12, col); break;
    case 2: gfx->fillCircle(icx, icy, 13, col); break;
    case 3:
      gfx->setTextSize(3); gfx->setTextColor(col);
      gfx->setCursor(icx-9, icy-12); gfx->print("A");
      break;
  }

  gfx->setTextSize(2); gfx->setTextColor(col);
  gfx->setCursor(x+44, y+20); gfx->print(l1);
  gfx->setTextColor(C_WHITE);
  gfx->setCursor(x+44, y+44); gfx->print(l2);
}

void drawGamesMenu() {
  appState = S_GAMES;
  gfx->fillScreen(C_DKBG);
  drawScrollHeader();

  gfx->fillRoundRect(8, CNT_Y+4, 224, 30, 7, 0x0422);
  gfx->setTextSize(2); gfx->setTextColor(C_NYEL);
  gfx->setCursor(20, CNT_Y+11); gfx->print("CHOOSE YOUR GAME!");
  drawStar(204, CNT_Y+19, 9, 4, G_YELLOW);

  // 2×2 grid
  drawGameTile(6,   CNT_Y+42, "COUNT", "STARS",  G_TEAL,  0);
  drawGameTile(126, CNT_Y+42, "SHAPE", "MATCH",  C_NMAG,  1);
  drawGameTile(6,   CNT_Y+140,"COLOUR","QUEST",  G_ORANGE,2);
  drawGameTile(126, CNT_Y+140,"LETTER","HUNT",   C_NLIME, 3);

  drawPanel(65, CNT_Y+240, 110, 24, C_GRAY);
  gfx->setTextSize(1); gfx->setTextColor(C_LGRAY);
  gfx->setCursor(80, CNT_Y+246); gfx->print("< MAIN MENU");

  drawPowerBar();
}

// ══════════════════════════════════════════════════════════════════════
//  MODE SELECTOR
// ══════════════════════════════════════════════════════════════════════

const char* MODE_DESC[3] = {
  "Friendly companion mode",
  "Alert & protection mode",
  "Playful guard dog mode"
};

void drawModeScreen() {
  appState = S_MODE;
  gfx->fillScreen(C_DKBG);
  drawScrollHeader();

  gfx->setTextSize(2); gfx->setTextColor(C_NYEL);
  gfx->setCursor(40, CNT_Y + 6); gfx->print("SELECT MODE");

  for (int i = 0; i < 3; i++) {
    int my = CNT_Y + 36 + i * 70;
    bool active = (botMode == i);
    uint16_t col = MODE_COL[i];
    uint16_t bg  = active ? MODE_BG[i] : C_DKBG;

    gfx->fillRoundRect(6, my, 228, 62, 9, bg);
    gfx->drawRoundRect(6,   my,   228, 62, 9, col);
    gfx->drawRoundRect(7,   my+1, 226, 60, 8, active ? col : C_GRAY);
    if (active) {
      gfx->drawRoundRect(5, my-1, 230, 64, 10, col);
    }

    // Icon
    drawMenuIcon(38, my+31, col, 2);

    // Mode name
    gfx->setTextSize(2); gfx->setTextColor(active ? C_WHITE : col);
    gfx->setCursor(72, my+12); gfx->print(MODE_NAME[i]);

    // Description
    gfx->setTextSize(1); gfx->setTextColor(active ? col : C_LGRAY);
    gfx->setCursor(72, my+36); gfx->print(MODE_DESC[i]);

    // Active badge
    if (active) {
      gfx->fillRoundRect(168, my+10, 56, 16, 4, col);
      gfx->setTextSize(1); gfx->setTextColor(C_BLACK);
      gfx->setCursor(172, my+14); gfx->print("ACTIVE");
    }
  }

  drawPanel(65, CNT_Y+250, 110, 24, C_GRAY);
  gfx->setTextSize(1); gfx->setTextColor(C_LGRAY);
  gfx->setCursor(80, CNT_Y+256); gfx->print("< MAIN MENU");

  drawPowerBar();
}

void selectMode(uint8_t m) {
  botMode = m;
  // Flash confirmation
  gfx->fillScreen(MODE_COL[m]);
  delay(80);
  gfx->fillScreen(C_DKBG);
  delay(40);
  // Send to Mega
  Serial1.print("MODE:");
  Serial1.println(MODE_NAME[m]);
  addLog("TX", MODE_NAME[m]);
  drawModeScreen();
}

// ══════════════════════════════════════════════════════════════════════
//  GAME — COUNT THE STARS
// ══════════════════════════════════════════════════════════════════════

void genCnt() {
  cntTgt   = random(1, 6);
  cntOpt[0] = cntTgt;
  do { cntOpt[1] = random(1,6); } while (cntOpt[1]==cntOpt[0]);
  do { cntOpt[2] = random(1,6); } while (cntOpt[2]==cntOpt[0]||cntOpt[2]==cntOpt[1]);
  for (int i=2;i>0;i--){int j=random(i+1);uint8_t t=cntOpt[i];cntOpt[i]=cntOpt[j];cntOpt[j]=t;}
  awaitIn = true;
}

void drawCountGame() {
  const uint16_t SC[5] = { G_YELLOW, G_CORAL, C_NCYAN, C_NLIME, C_NMAG };
  gfx->fillScreen(C_BLACK);
  drawSubHeader("COUNT STARS", G_TEAL);
  drawScoreBar();

  drawPanel(8, QBOX_Y, 224, QBOX_H, G_TEAL);
  gfx->setTextSize(1); gfx->setTextColor(G_TEAL);
  gfx->setCursor(48, QBOX_Y+6); gfx->print("HOW MANY STARS?");

  int sz = (cntTgt <= 3) ? 22 : 16;
  int gap = sz*2 + 10;
  int row0 = min((int)cntTgt, 3);
  int row1 = max(0, (int)cntTgt - 3);
  int y0   = (row1 > 0) ? QBOX_Y+48 : QBOX_Y+64;
  int x0   = (SCR_W - row0*gap + 10)/2 + sz;
  for (int i=0;i<row0;i++) drawStar(x0+i*gap, y0, sz, sz/2, SC[i%5]);
  if (row1>0){
    int x1=(SCR_W-row1*gap+10)/2+sz;
    for(int i=0;i<row1;i++) drawStar(x1+i*gap, y0+sz*2+12, sz, sz/2, SC[(row0+i)%5]);
  }

  for (int i=0;i<3;i++){
    int bx=10+i*75;
    drawPanel(bx, ANS_Y, 68, 52, G_TEAL);
    gfx->setTextSize(4); gfx->setTextColor(C_WHITE);
    gfx->setCursor(cntOpt[i]>=10 ? bx+10 : bx+20, ANS_Y+10);
    gfx->print(cntOpt[i]);
  }
  drawBackBtn(); drawPowerBar();
}

void startCountGame() { returnGame=S_CNT; appState=S_CNT; genCnt(); drawCountGame(); }

// ══════════════════════════════════════════════════════════════════════
//  GAME — SHAPE MATCH
// ══════════════════════════════════════════════════════════════════════

void genShp() {
  shpQ=random(4); shpOpt[0]=shpQ;
  for(int i=1;i<4;){uint8_t r=random(4);bool d=false;for(int k=0;k<i;k++)if(shpOpt[k]==r)d=true;if(!d){shpOpt[i]=r;i++;}}
  for(int i=3;i>0;i--){int j=random(i+1);uint8_t t=shpOpt[i];shpOpt[i]=shpOpt[j];shpOpt[j]=t;}
  awaitIn=true;
}

void drawShapeGame() {
  const uint16_t OC[4]={C_NCYAN,G_ORANGE,C_NLIME,C_NMAG};
  gfx->fillScreen(C_BLACK);
  drawSubHeader("SHAPE MATCH", C_NMAG);
  drawScoreBar();

  drawPanel(8,QBOX_Y,224,QBOX_H,C_NMAG);
  gfx->setTextSize(1); gfx->setTextColor(C_NMAG);
  gfx->setCursor(30,QBOX_Y+6); gfx->print("FIND THE MATCHING SHAPE!");
  // Shape at centre of Q-box — radius 32 (was 36) keeps it inside panel
  drawShape(shpQ,120,QBOX_Y+62,32,SHP_COL[shpQ]);
  gfx->drawCircle(120,QBOX_Y+62,42,C_WHITE);
  gfx->setTextSize(2); gfx->setTextColor(C_WHITE);
  int tw=strlen(SHP_NAME[shpQ])*12;
  // Name at QBOX_Y+96 — bottom ~112, panel bottom=114 ✓
  gfx->setCursor((SCR_W-tw)/2,QBOX_Y+96); gfx->print(SHP_NAME[shpQ]);

  // 2×2 answer grid using ANS2_H / ANS2_SP constants
  for(int i=0;i<4;i++){
    int bx=10+(i%2)*116, by=ANS_Y+(i/2)*ANS2_SP;
    drawPanel(bx,by,104,ANS2_H,OC[i]);
    // Icon on the left, label on the right — no overlap
    drawShape(shpOpt[i],bx+20,by+ANS2_H/2,12,OC[i]);
    gfx->setTextSize(1); gfx->setTextColor(C_WHITE);
    gfx->setCursor(bx+40,by+ANS2_H/2-4); gfx->print(SHP_NAME[shpOpt[i]]);
  }
  drawBackBtn(); drawPowerBar();
}

void startShapeGame() { returnGame=S_SHP; appState=S_SHP; genShp(); drawShapeGame(); }

// ══════════════════════════════════════════════════════════════════════
//  GAME — COLOUR QUEST
// ══════════════════════════════════════════════════════════════════════

void genClr() {
  clrQ=random(6); clrOpt[0]=clrQ;
  for(int i=1;i<4;){uint8_t r=random(6);bool d=false;for(int k=0;k<i;k++)if(clrOpt[k]==r)d=true;if(!d){clrOpt[i]=r;i++;}}
  for(int i=3;i>0;i--){int j=random(i+1);uint8_t t=clrOpt[i];clrOpt[i]=clrOpt[j];clrOpt[j]=t;}
  awaitIn=true;
}

void drawColourGame() {
  gfx->fillScreen(C_BLACK);
  drawSubHeader("COLOUR QUEST", G_ORANGE);
  drawScoreBar();

  drawPanel(8,QBOX_Y,224,QBOX_H,G_ORANGE);
  gfx->setTextSize(1); gfx->setTextColor(G_ORANGE);
  gfx->setCursor(52,QBOX_Y+6); gfx->print("TAP THIS COLOUR!");
  gfx->fillCircle(120,QBOX_Y+70,44,CLR_VAL[clrQ]);
  gfx->drawCircle(120,QBOX_Y+70,46,C_WHITE);
  gfx->drawCircle(120,QBOX_Y+70,47,C_LGRAY);
  gfx->setTextSize(2); gfx->setTextColor(C_WHITE);
  int tw=strlen(CLR_NAME[clrQ])*12;
  gfx->setCursor((SCR_W-tw)/2,QBOX_Y+60); gfx->print(CLR_NAME[clrQ]);

  for(int i=0;i<4;i++){
    int bx=10+(i%2)*116, by=ANS_Y+(i/2)*ANS2_SP;
    drawSolidBtn(bx,by,104,ANS2_H,CLR_VAL[clrOpt[i]],C_WHITE);
    gfx->setTextSize(1); gfx->setTextColor(C_WHITE);
    int nl=strlen(CLR_NAME[clrOpt[i]])*6;
    gfx->setCursor(bx+(104-nl)/2,by+ANS2_H/2-4); gfx->print(CLR_NAME[clrOpt[i]]);
  }
  drawBackBtn(); drawPowerBar();
}

void startColourGame() { returnGame=S_CLR; appState=S_CLR; genClr(); drawColourGame(); }

// ══════════════════════════════════════════════════════════════════════
//  GAME — LETTER HUNT
// ══════════════════════════════════════════════════════════════════════

void genLtr() {
  ltrQ=random(8); ltrOpt[0]=ltrQ;
  for(int i=1;i<4;){uint8_t r=random(8);bool d=false;for(int k=0;k<i;k++)if(ltrOpt[k]==r)d=true;if(!d){ltrOpt[i]=r;i++;}}
  for(int i=3;i>0;i--){int j=random(i+1);uint8_t t=ltrOpt[i];ltrOpt[i]=ltrOpt[j];ltrOpt[j]=t;}
  awaitIn=true;
}

void drawLetterGame() {
  const uint16_t LC[4]={C_NCYAN,G_ORANGE,C_NMAG,G_YELLOW};
  gfx->fillScreen(C_BLACK);
  drawSubHeader("LETTER HUNT", C_NLIME);
  drawScoreBar();

  drawPanel(8,QBOX_Y,224,QBOX_H,C_NLIME);
  gfx->setTextSize(1); gfx->setTextColor(C_NLIME);
  gfx->setCursor(28,QBOX_Y+6); gfx->print("FIND THIS LETTER! TAP IT!");
  gfx->fillCircle(120,QBOX_Y+70,46,0x0420);
  gfx->drawCircle(120,QBOX_Y+70,46,C_NLIME);
  gfx->drawCircle(120,QBOX_Y+70,48,0x0220);
  gfx->setTextSize(7); gfx->setTextColor(C_NLIME);
  char ql[2]={LTR_SET[ltrQ],0};
  gfx->setCursor(95,QBOX_Y+42); gfx->print(ql);

  for(int i=0;i<4;i++){
    int bx=10+(i%2)*116, by=ANS_Y+(i/2)*ANS2_SP;
    drawPanel(bx,by,104,ANS2_H,LC[i]);
    gfx->setTextSize(3); gfx->setTextColor(C_WHITE);
    char ll[2]={LTR_SET[ltrOpt[i]],0};
    // textSize 3 = 18px tall, centre vertically in ANS2_H=40
    gfx->setCursor(bx+40,by+(ANS2_H-21)/2); gfx->print(ll);
  }
  drawBackBtn(); drawPowerBar();
}

void startLetterGame() { returnGame=S_LTR; appState=S_LTR; genLtr(); drawLetterGame(); }

// ══════════════════════════════════════════════════════════════════════
//  ANSWER CHECKING
// ══════════════════════════════════════════════════════════════════════

void checkAnswer(uint8_t idx) {
  if (!awaitIn || inFB) return;
  awaitIn = false; inFB = true; fbMs = millis();
  sesTotal++;
  bool ok = false;
  switch(appState){
    case S_CNT: ok=(cntOpt[idx]==cntTgt); break;
    case S_SHP: ok=(shpOpt[idx]==shpQ);   break;
    case S_CLR: ok=(clrOpt[idx]==clrQ);   break;
    case S_LTR: ok=(ltrOpt[idx]==ltrQ);   break;
    default: break;
  }
  lastOK = ok;
  if (ok) {
    sesStars++; streak++;
    if (streak > hiStreak) hiStreak = streak;
    if (streak % 3 == 0 || sesStars % 5 == 0) { initCeleb(); return; }
  } else {
    streak = 0;
  }
  drawScoreBar();
  drawFeedback(ok);
}

void nextQuestion() {
  inFB = false;
  awaitIn = false;
  // BUG FIX: when called from celebration, appState is S_CELEB.
  // Restore the game we came from before switching.
  if (appState == S_CELEB) appState = returnGame;
  clearFeedback();
  switch(appState){
    case S_CNT: genCnt();  drawCountGame();  break;
    case S_SHP: genShp();  drawShapeGame();  break;
    case S_CLR: genClr();  drawColourGame(); break;
    case S_LTR: genLtr();  drawLetterGame(); break;
    default: drawGamesMenu(); break; // safety fallback
  }
}

// ══════════════════════════════════════════════════════════════════════
//  CELEBRATION
// ══════════════════════════════════════════════════════════════════════

void initCeleb() {
  AppState prev = appState;
  appState = S_CELEB; returnGame = prev; inFB = false;
  gfx->fillScreen(C_BLACK);
  gfx->setTextSize(3); gfx->setTextColor(G_YELLOW);
  gfx->setCursor(18,50); gfx->print("AMAZING AJ!");
  gfx->setTextSize(2); gfx->setTextColor(C_WHITE);
  gfx->setCursor(46,92); gfx->print("YOU'RE SO");
  gfx->setCursor(58,116); gfx->print("CLEVER!");
  for(int i=0;i<min((int)streak,5);i++) drawStar(26+i*38,155,16,6,G_YELLOW);

  const uint16_t PC[6]={G_YELLOW,G_CORAL,C_NCYAN,C_NLIME,C_NMAG,G_ORANGE};
  for(int i=0;i<18;i++){
    pts[i]={SCR_W/2,190,(int16_t)random(-9,10),(int16_t)random(-12,-3),PC[i%6]};
  }
  celFr=0; celMs=millis();
}

void animateCeleb() {
  if (millis()-celMs<35) return;
  celMs=millis(); celFr++;
  for(int i=0;i<18;i++){
    gfx->fillRect(pts[i].x-1,pts[i].y-1,10,10,C_BLACK);
    pts[i].x+=pts[i].vx; pts[i].vy+=1; pts[i].y+=pts[i].vy;
    if(pts[i].x<4||pts[i].x>SCR_W-10) pts[i].vx=-pts[i].vx;
    if(pts[i].y>PBAR_Y-8){pts[i].vy=-(abs(pts[i].vy)*3/4);pts[i].y=PBAR_Y-8;}
    gfx->fillRoundRect(pts[i].x,pts[i].y,8,8,2,pts[i].col);
  }
  drawPowerBar();
  if(celFr>=72) nextQuestion();
}

// ══════════════════════════════════════════════════════════════════════
//  TOUCH HANDLER
// ══════════════════════════════════════════════════════════════════════

// Returns index 0-3 if touch hits a 2×2 answer grid, else -1
int8_t hit4(int tx, int ty) {
  for(int i=0;i<4;i++){
    int bx=10+(i%2)*116, by=ANS_Y+(i/2)*ANS2_SP;
    if(tx>bx&&tx<bx+104&&ty>by&&ty<by+ANS2_H) return i;
  }
  return -1;
}

void handleTouch() {
  TSPoint p = ts.getPoint();
  restorePins();
  if (p.z < 180 || p.z > 1100) return;
  int tx = map(p.x, TS_MAXX, TS_MINX, 0, SCR_W);
  int ty = map(p.y, TS_MINY, TS_MAXY, 0, SCR_H);
  delay(55); // debounce

  bool onBack = (tx>=BACK_X && tx<=BACK_X+BACK_W && ty>=BACK_Y && ty<=BACK_Y+BACK_H);

  switch(appState){

    // ── Main Menu ─────────────────────────────────────────────────────
    case S_MAIN: {
      const int BH=72, BY0=CNT_Y+8;
      if (ty>BY0    && ty<BY0+BH)    { drawTechMenu();   break; }
      if (ty>BY0+80 && ty<BY0+80+BH) { drawGamesMenu();  break; }
      if (ty>BY0+160&& ty<BY0+160+BH){ drawModeScreen(); break; }
      break;
    }

    // ── Tech Menu ─────────────────────────────────────────────────────
    case S_TECH:
      if (ty>CNT_Y+249&&tx>65&&tx<175) { drawMainMenu(); break; }
      if (ty>CNT_Y+44 &&ty<CNT_Y+124)  { drawSensorInfo(); break; }
      if (ty>CNT_Y+134&&ty<CNT_Y+214)  { drawSerialScreen(); break; }
      break;

    // ── Sensor Info ───────────────────────────────────────────────────
    case S_SENSORS:
      if (onBack) { drawTechMenu(); }
      break;

    // ── Serial Comms ──────────────────────────────────────────────────
    case S_SERIAL:
      if (onBack) { drawTechMenu(); }
      break;

    // ── Games Menu ────────────────────────────────────────────────────
    case S_GAMES: {
      if (ty>CNT_Y+240 && tx>65 && tx<175) { drawMainMenu(); break; }
      int gy1 = CNT_Y+42, gy2 = CNT_Y+140;
      if (tx<127 && ty>gy1 && ty<gy1+88) { startCountGame();  break; }
      if (tx>=127&& ty>gy1 && ty<gy1+88) { startShapeGame();  break; }
      if (tx<127 && ty>gy2 && ty<gy2+88) { startColourGame(); break; }
      if (tx>=127&& ty>gy2 && ty<gy2+88) { startLetterGame(); break; }
      break;
    }

    // ── Mode Screen ───────────────────────────────────────────────────
    case S_MODE: {
      if (ty>CNT_Y+250 && tx>65 && tx<175) { drawMainMenu(); break; }
      for(int i=0;i<3;i++){
        int my=CNT_Y+36+i*70;
        if(ty>my && ty<my+62) { selectMode(i); break; }
      }
      break;
    }

    // ── Count Stars ───────────────────────────────────────────────────
    case S_CNT:
      if (onBack) {
        inFB = false; awaitIn = false; // BUG FIX: clear feedback state on exit
        drawGamesMenu(); break;
      }
      if (inFB) break;
      for(int i=0;i<3;i++){
        int bx=10+i*75;
        if(tx>bx&&tx<bx+68&&ty>ANS_Y&&ty<ANS_Y+50) { checkAnswer(i); break; }
      }
      break;

    // ── Shape / Colour / Letter ───────────────────────────────────────
    case S_SHP: case S_CLR: case S_LTR:
      if (onBack) {
        inFB = false; awaitIn = false; // BUG FIX: clear feedback state on exit
        drawGamesMenu(); break;
      }
      if (!inFB) { int8_t h=hit4(tx,ty); if(h>=0) checkAnswer(h); }
      break;

    default: break;
  }
}

// ══════════════════════════════════════════════════════════════════════
//  MEGA LINK — Telemetry Parser
// ══════════════════════════════════════════════════════════════════════

void handleMegaLink() {
  if (!Serial1.available()) return;
  String raw = Serial1.readStringUntil('\n');
  raw.trim();
  if (raw.length() == 0) return;

  // Log every incoming message (truncated)
  char tag[6] = "MEGA";
  if      (raw.startsWith("STAT"))   strcpy(tag,"STAT");
  else if (raw.startsWith("PWR"))    strcpy(tag,"PWR");
  else if (raw.startsWith("US"))     strcpy(tag,"US");
  else if (raw.startsWith("STATUS")) strcpy(tag,"STAT");
  else if (raw.startsWith("MODE"))   strcpy(tag,"MODE");
  else if (raw.startsWith("GESTURE"))strcpy(tag,"GEST");
  addLog(tag, raw.c_str());

  char buf[100];
  raw.toCharArray(buf, sizeof(buf));
  char *tok;

  // ── US:front,rear,left,right  (add this line to Mega's sendTelemetryToR4) ──
  if (raw.startsWith("US:")) {
    sscanf(buf + 3, "%d,%d,%d,%d", &sF, &sR, &sL, &sRi);
    sensNeedsRedraw = true;
    return;
  }

  // ── STAT:gas:temp:hum:haz:lat:lon:sats:volt:pct:amp:pwr:tz1:tz2 ───────────
  if (raw.startsWith("STAT:")) {
    tok = strtok(buf + 5, ":"); int i = 0;
    while (tok) {
      if (i==0)  botGas  = atoi(tok);
      if (i==1)  botTemp = atof(tok);
      if (i==2)  botHum  = atof(tok);
      if (i==3)  botHaz  = atoi(tok);
      if (i==8)  botVolt = atof(tok);
      if (i==9)  botPct  = constrain(atoi(tok),0,100);
      if (i==10) botAmps = atof(tok);
      tok = strtok(NULL,":"); i++;
    }
    return;
  }

  // ── PWR:volt:amps:watts:mah:pct:z1:z2:... ─────────────────────────────────
  if (raw.startsWith("PWR:")) {
    tok = strtok(buf + 4, ":"); int i = 0;
    while (tok) {
      if (i==0) botVolt = atof(tok);
      if (i==1) botAmps = atof(tok);
      if (i==4) botPct  = constrain(atoi(tok),0,100);
      tok = strtok(NULL,":"); i++;
    }
    return;
  }

  // ── STATUS|BAT:x|PCT:x|AMP:x|...|ESTOP:YES|AUTO:ON|... ───────────────────
  if (raw.startsWith("STATUS|")) {
    botEstop = (raw.indexOf("ESTOP:YES") >= 0);
    botAuto  = (raw.indexOf("AUTO:ON")  >= 0);
    int vi = raw.indexOf("BAT:");
    if (vi >= 0) botVolt = raw.substring(vi+4).toFloat();
    int pi = raw.indexOf("PCT:");
    if (pi >= 0) botPct  = constrain(raw.substring(pi+4).toInt(),0,100);
    return;
  }
}

// ══════════════════════════════════════════════════════════════════════
//  SETUP
// ══════════════════════════════════════════════════════════════════════

void setup() {
  Serial1.begin(115200);
  randomSeed(analogRead(A5) ^ (unsigned long)millis());

  gfx->begin();
  gfx->setRotation(0);      // portrait
  gfx->invertDisplay(true); // required for HX8347D
  gfx->fillScreen(C_BLACK);

  // Clear log
  memset(logLines, 0, sizeof(logLines));

  doSplash();       // blocking 10-second splash
  drawMainMenu();   // enter main app
}

// ══════════════════════════════════════════════════════════════════════
//  LOOP
// ══════════════════════════════════════════════════════════════════════

void loop() {
  handleMegaLink();

  // Celebration owns the loop until done
  if (appState == S_CELEB) { animateCeleb(); return; }

  // Feedback auto-advance
  if (inFB && millis() - fbMs > FB_DUR) {
    if (lastOK) nextQuestion();
    else { inFB = false; clearFeedback(); awaitIn = true; }
  }

  // Scroll header on menu screens
  if (appState==S_MAIN || appState==S_TECH ||
      appState==S_GAMES || appState==S_MODE) {
    tickHeader();
  }

  // Refresh live sensor screen every 600ms
  if (appState == S_SENSORS && millis() - sensRefMs > 600) {
    sensRefMs = millis();
    drawUSHUD();
    // Refresh env data strip
    char v[16];
    snprintf(v,sizeof(v),"%.1f C",botTemp); drawEnvRow(205,"TEMPERATURE:",v,C_NYEL);
    snprintf(v,sizeof(v),"%.0f %%",botHum); drawEnvRow(218,"HUMIDITY:   ",v,C_NCYAN);
    snprintf(v,sizeof(v),"%d",botGas);       drawEnvRow(231,"GAS LEVEL:  ",v,botGas>400?G_CORAL:C_NLIME);
    snprintf(v,sizeof(v),"%.1fV %d%%",botVolt,botPct); drawEnvRow(244,"BATTERY:    ",v,botPct<20?G_CORAL:C_NLIME);
    snprintf(v,sizeof(v),"%s",botEstop?"E-STOP!":(botAuto?"AUTO":"MANUAL")); drawEnvRow(257,"BOT STATE:  ",v,botEstop?G_RED:C_NLIME);
    drawPowerBar();
  }

  // Auto-refresh serial comms log when new data
  refreshSerialLog();

  handleTouch();
}
