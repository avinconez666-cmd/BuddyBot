//*
 * ════════════════════════════════════════════════════════════════════
 *  BUDDYBOT V25.0  ·  UNO R4 WiFi  ·  ORBITAL HMI INTERFACE
 * ════════════════════════════════════════════════════════════════════
 *  HX8347D 240×320  ·  UNOPAR8 bus  ·  TouchScreen  ·  Serial1→Mega
 *
 *  REDESIGNED FEATURES:
 *    ★ Premium dark-space HMI aesthetic with electric-blue accents
 *    ★ Persistent header back-button overlaid on every submenu
 *    ★ Paginated scrollable menu system (supports 6+ items)
 *    ★ Rebuilt mini-games: Count Stars · Shape Match · Colour Quest · Letter Hunt
 *    ★ Live ultrasonic HUD + environmental telemetry
 *    ★ MODE → Normal · Bodyguard · Guard Dog
 *    ★ Animated celebration with physics particles
 *    ★ Full startup diagnostic with board link quality
 *
 *  MEGA SERIAL STRINGS PARSED (unchanged):
 *    STAT:gas:temp:hum:haz:pir:tilt:flame:ir:volt:pct:amps
 *    PWR:volt:amps:watts:mah:pct:z1:z2:peak:avg:wh
 *    US:front,rear,left,right
 *    STATUS|...|END
 * ════════════════════════════════════════════════════════════════════
 */

// ─── Board link status — defined before #includes ─────────────────
struct BoardStat {
  bool     linked     = false;
  bool     ready      = false;
  uint32_t bytesRx    = 0;
  uint32_t msgsOK     = 0;
  uint32_t msgsFail   = 0;
  uint32_t lastRxMs   = 0;
  uint8_t  qualPct    = 0;
  char     faultMsg[44] = "";
  char     fwVer[12]   = "";
};

#include <Arduino_GFX_Library.h>
#include <TouchScreen.h>
#include <math.h>

enum AlertSeverity : uint8_t { SEV_INFO = 0, SEV_WARN = 1, SEV_CRITICAL = 2 };
struct AlertOverlay {
  bool          active    = false;
  bool          ignorable = true;
  AlertSeverity sev       = SEV_INFO;
  char          title[28] = "";
  char          msg[88]   = "";
};

// ─── Hardware ─────────────────────────────────────────────────────────
Arduino_DataBus *bus = new Arduino_UNOPAR8();
Arduino_GFX     *gfx = new Arduino_HX8347D(bus, A4, 0);
TouchScreen       ts  = TouchScreen(8, A3, A2, 9, 300);

// ─── Touchscreen calibration ──────────────────────────────────────────
#define TS_MINX 130
#define TS_MAXX 900
#define TS_MINY 100
#define TS_MAXY 920

// ═══════════════════════════════════════════════════════════════════════
//  PREMIUM COLOUR PALETTE  — Orbital HMI
// ═══════════════════════════════════════════════════════════════════════
#define C_BG      0x0209   // Deep space background
#define C_SURF    0x0862   // Raised surface
#define C_SURF2   0x10A3   // Card highlight
#define C_LINE    0x2124   // Subtle hairline
#define C_CYAN    0x07FF   // Electric cyan (primary)
#define C_DCYAN   0x0398   // Dim cyan
#define C_MINT    0x07E4   // Mint green (success)
#define C_AMBER   0xFD20   // Amber (warning)
#define C_CORAL   0xFB0C   // Coral (danger/critical)
#define C_PURP    0x801F   // Purple accent
#define C_MAG     0xF81F   // Magenta
#define C_YLLOW   0xFFE0   // Yellow
#define C_WHITE   0xFFFF   // Pure white
#define C_LGRAY   0x8C71   // Light gray text
#define C_MGRAY   0x528A   // Medium gray
#define C_DGRAY   0x2124   // Dark border gray
#define C_RED     0xF800   // Red
#define C_GREEN   0x07E0   // Green
#define C_ORANGE  0xFC60   // Orange
#define C_LIME    0x87E0   // Lime
#define C_TEAL    0x07E4   // Teal (same as mint)
#define C_BLACK   0x0000   // Black

// ─── Screen & Layout ──────────────────────────────────────────────────
#define SCR_W   240
#define SCR_H   320
#define HDR_H    48    // Header height
#define PBAR_Y  296    // Power bar Y
#define PBAR_H   24    // Power bar height
#define CNT_Y   HDR_H
#define CNT_H   (PBAR_Y - HDR_H)   // 248px content area

// ─── Header back-button tap zone (overlaid) ──────────────────────────
#define BACK_TX  52    // tap zone: x < 52
#define BACK_TY  HDR_H // tap zone: y < HDR_H

// ─── Game Layout ──────────────────────────────────────────────────────
#define G_SCORE_Y  HDR_H                         // 48
#define G_SCORE_H  20                            // score bar height
#define G_QBOX_Y   (G_SCORE_Y + G_SCORE_H + 4)  // 72
#define G_QBOX_H   112                           // question panel
#define G_ANS_Y    (G_QBOX_Y + G_QBOX_H + 4)    // 188
#define G_ANS_H    44                            // answer button height
#define G_ANS_SP   48                            // row stride (answer gap)

// Count Stars uses 3 big buttons (different layout)
#define G_CNT_BW   74   // count button width
#define G_CNT_BH   76   // count button height

// ─── Main Menu Pagination ─────────────────────────────────────────────
#define MENU_N          3   // total main menu items
#define MENU_PER_PAGE   3   // items visible per page
uint8_t menuPage = 0;

// ─── App State Machine ────────────────────────────────────────────────
enum AppState : uint8_t {
  S_STARTUP = 0, S_SPLASH, S_MAIN, S_TECH, S_SENSORS, S_SERIAL,
  S_GAMES, S_MODE, S_CNT, S_SHP, S_CLR, S_LTR, S_CELEB
};
AppState appState   = S_STARTUP;
AppState returnGame = S_CNT;

// ─── Telemetry ────────────────────────────────────────────────────────
float   botVolt = 8.4f, botAmps = 0.0f, botTemp = 25.0f;
int     botPct  = 100;
int     botGas  = 0;
float   botHum  = 50.0f;
float   botBoostVolt = 0.0f;
bool    botHaz  = false, botEstop = false, botAuto = false;
int16_t sF = -1, sR = -1, sL = -1, sRi = -1;

// ─── Alert state ──────────────────────────────────────────────────────
bool    alertBatWarn = false, alertBatLow = false, alertBatCritical = false;
bool    alertTilt = false, alertFlame = false, alertGas = false;
char    alertText[32] = "";

// ─── Serial Log ───────────────────────────────────────────────────────
#define LOG_N   7
#define LOG_W  30
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
const uint16_t MODE_COL[3]  = { C_MINT, C_CYAN, C_CORAL };
const uint16_t MODE_BG[3]   = { 0x0422, 0x0218, 0x3000 };

// ─── Scrolling Header State ───────────────────────────────────────────
int16_t  hdrOfs  = SCR_W;
uint8_t  hdrPalI = 0;
uint8_t  hdrTick = 0;
unsigned long hdrMs = 0;
const uint16_t HDR_PAL[] = {
  0x07FF, 0xF81F, 0x07E0, 0xFFE0, 0xFC60, 0x801F, 0xFB56, 0x027F
};
#define HDR_PAL_N 8

// ─── Matrix Rain ──────────────────────────────────────────────────────
#define RAIN_N 12
struct Drop { int16_t y; uint8_t spd, tail; };
Drop rain[RAIN_N];

// ─── Games: Session ───────────────────────────────────────────────────
uint8_t  sesStars = 0, sesTotal = 0, streak = 0, hiStreak = 0;
bool     awaitIn = false;
bool     inFB    = false;
bool     lastOK  = false;
unsigned long fbMs = 0;
#define FB_DUR 1600

// ─── Games: Data ──────────────────────────────────────────────────────
uint8_t cntTgt, cntOpt[3];
uint8_t shpQ, shpOpt[4];
uint8_t clrQ, clrOpt[4];
uint8_t ltrQ, ltrOpt[4];

const char*    SHP_NAME[4] = { "CIRCLE", "TRIANGLE", "SQUARE", "STAR" };
const uint16_t SHP_COL[4]  = { C_CYAN, C_MAG, C_YLLOW, C_CORAL };
const char*    CLR_NAME[6] = { "RED", "BLUE", "GREEN", "YELLOW", "ORANGE", "PURPLE" };
const uint16_t CLR_VAL[6]  = { C_RED, 0x021F, C_GREEN, C_YLLOW, C_ORANGE, C_PURP };
const char     LTR_SET[8]  = { 'A','B','C','D','E','F','G','H' };

// ─── Celebration Particles ────────────────────────────────────────────
struct Particle { int16_t x, y, vx, vy; uint16_t col; };
Particle pts[20];
uint8_t  celFr = 0;
unsigned long celMs = 0;

// ─── Sensor refresh ───────────────────────────────────────────────────
unsigned long sensRefMs = 0;
bool          sensNeedsRedraw = false;
String        lastSensStatus  = "";

// ─── Board link status ────────────────────────────────────────────────
BoardStat bsMega, bsR3, bsS9, bsESP32;

// ─── Alert overlay ────────────────────────────────────────────────────
AlertOverlay activeAlert;
static const uint16_t OVL_SEV_COL[3] = { C_CYAN, C_AMBER, C_RED };
static const uint16_t OVL_SEV_BG[3]  = { 0x0218, 0x3200, 0x6000 };
static const char* const OVL_SEV_LBL[3] = { "INFO", "WARNING", "CRITICAL" };

// ─── Ping / pong ──────────────────────────────────────────────────────
uint16_t      pingSeq       = 0;
uint8_t       pingOkCount   = 0;
unsigned long lastPingSentMs = 0;
unsigned long lastPongRxMs   = 0;
#define PING_IV_MS       2000
#define PING_LINK_THRESH    3

// ─── Serial sniffer ───────────────────────────────────────────────────
#define SNIFF_N  12
uint8_t  sniffBuf[SNIFF_N];
uint8_t  sniffWr    = 0;
uint32_t sniffTotal = 0;
uint32_t sniffWindowStart = 0;
uint8_t  bytesPerSec = 0;
uint32_t windowBytes = 0;

// ─── CRC tracking ─────────────────────────────────────────────────────
uint32_t crcOK = 0, crcFail = 0;

uint8_t r4CalcCRC(const char* s, uint8_t len) {
  uint8_t c = 0;
  for (uint8_t i = 0; i < len; i++) c ^= (uint8_t)s[i];
  return c;
}

// ─── Startup screen state ─────────────────────────────────────────────
unsigned long qualWindowStart = 0;
unsigned long startMs     = 0;
bool          startReady  = false;
bool          skipConfirm = false;
unsigned long skipTapMs   = 0;
#define START_SKIP_DELAY  20000

// ─── Forward declarations ─────────────────────────────────────────────
void drawScrollHeader();
void drawSubHeader(const char* title, uint16_t col, bool showBack = false);
void drawPowerBar();
void showAlert(AlertSeverity sev, const char* title, const char* body, bool ignorable = true);
void drawAlertOverlay();
void dismissAlert(bool ignore);
void drawMainMenu();
void nextQuestion();
void drawStartupScreen();
void proceedFromStartup();


// ══════════════════════════════════════════════════════════════════════
//  DRAWING PRIMITIVES
// ══════════════════════════════════════════════════════════════════════

void restorePins() {
  pinMode(8, OUTPUT); pinMode(9, OUTPUT);
  pinMode(A2, OUTPUT); pinMode(A3, OUTPUT);
}

// Clean card panel with single thin border
void drawPanel(int x, int y, int w, int h, uint16_t col) {
  gfx->fillRoundRect(x, y, w, h, 7, C_SURF);
  gfx->drawRoundRect(x, y, w, h, 7, col);
}

// Filled card (for colour game buttons)
void drawSolidBtn(int x, int y, int w, int h, uint16_t fill, uint16_t brd) {
  gfx->fillRoundRect(x, y, w, h, 7, fill);
  gfx->drawRoundRect(x, y, w, h, 7, brd);
}

// Accent bar + body card
void drawAccentCard(int x, int y, int w, int h, uint16_t col) {
  gfx->fillRoundRect(x, y, w, h, 7, C_SURF);
  gfx->drawRoundRect(x, y, w, h, 7, col);
  gfx->fillRoundRect(x, y, w, 4, 3, col);   // top accent strip
}

// 5-point filled star
void drawStar(int cx, int cy, int ro, int ri, uint16_t col) {
  float a = -1.5708f;
  int px[10], py[10];
  for (int i = 0; i < 10; i++) {
    float r = (i & 1) ? (float)ri : (float)ro;
    px[i] = cx + (int)(r * cosf(a));
    py[i] = cy + (int)(r * sinf(a));
    a += 0.6283f;
  }
  for (int i = 0; i < 10; i++) {
    int j = (i + 1) % 10;
    gfx->fillTriangle(cx, cy, px[i], py[i], px[j], py[j], col);
  }
}

// Shape dispatch
void drawShape(uint8_t id, int cx, int cy, int sz, uint16_t col) {
  switch (id) {
    case 0: gfx->fillCircle(cx, cy, sz, col); break;
    case 1: gfx->fillTriangle(cx, cy-sz, cx-sz, cy+sz, cx+sz, cy+sz, col); break;
    case 2: gfx->fillRect(cx-sz, cy-sz, sz*2, sz*2, col); break;
    case 3: drawStar(cx, cy, sz, sz/2, col); break;
  }
}

// ── Persistent header back button (overlaid at top-left) ──────────────
// Drawn last on submenus so it sits above the header content.
// Tap zone: x < BACK_TX && y < BACK_TY
void drawHeaderBack() {
  // Translucent pill
  gfx->fillRoundRect(3, 8, 44, 30, 8, C_SURF);
  gfx->drawRoundRect(3, 8, 44, 30, 8, C_CYAN);
  // Arrow symbol
  gfx->setTextColor(C_CYAN);
  gfx->setTextSize(2);
  gfx->setCursor(10, 14);
  gfx->print("<");
  gfx->setTextSize(1);
  gfx->setTextColor(C_DCYAN);
  gfx->setCursor(26, 20);
  gfx->print("BK");
}

// ─── Power bar ────────────────────────────────────────────────────────
void drawPowerBar() {
  gfx->fillRect(0, PBAR_Y, SCR_W, PBAR_H, C_SURF);
  gfx->drawFastHLine(0, PBAR_Y, SCR_W, C_DGRAY);

  // Battery gauge (left side)
  uint16_t batCol = (botPct > 50) ? C_MINT : (botPct > 20 ? C_AMBER : C_CORAL);
  int gaugeW = constrain((botPct * 36) / 100, 1, 36);
  gfx->drawRect(4, PBAR_Y + 5, 38, 14, batCol);
  gfx->fillRect(42, PBAR_Y + 8, 3, 8, batCol);   // battery tip
  gfx->fillRect(5, PBAR_Y + 6, gaugeW, 12, batCol);

  // Voltage & current (center)
  char pb[48];
  snprintf(pb, sizeof(pb), "%.1fV  %.0fW  %.0fC  %d%%",
           botVolt, botVolt * botAmps, botTemp, botPct);
  gfx->setTextSize(1);
  gfx->setTextColor(C_LGRAY);
  gfx->setCursor(50, PBAR_Y + 8);
  gfx->print(pb);

  // Alert strip
  if (alertText[0]) {
    gfx->setTextColor(C_CORAL);
    gfx->setCursor(50, PBAR_Y + 16);
    gfx->print(alertText);
  }
}

// ── Score bar (in-game) ───────────────────────────────────────────────
void drawScoreBar() {
  gfx->fillRect(0, G_SCORE_Y, SCR_W, G_SCORE_H, C_SURF);
  gfx->drawFastHLine(0, G_SCORE_Y + G_SCORE_H - 1, SCR_W, C_DGRAY);

  // Star row (5 stars)
  for (int i = 0; i < 5; i++) {
    uint16_t c = (i < (sesStars % 6)) ? C_YLLOW : C_DGRAY;
    drawStar(14 + i * 20, G_SCORE_Y + 10, 6, 3, c);
  }

  // Stats right
  char buf[20];
  snprintf(buf, sizeof(buf), "%d/%d  STK:%d", sesStars, sesTotal, streak);
  gfx->setTextSize(1);
  gfx->setTextColor(C_LGRAY);
  gfx->setCursor(SCR_W - 90, G_SCORE_Y + 6);
  gfx->print(buf);
}

// ── In-game feedback (drawn over power bar area) ──────────────────────
void drawFeedback(bool ok) {
  uint16_t bg  = ok ? 0x0442 : 0x5000;
  uint16_t brd = ok ? C_MINT : C_CORAL;
  gfx->fillRoundRect(4, 284, 232, 10, 4, bg);
  gfx->drawRoundRect(4, 284, 232, 10, 4, brd);
  gfx->setTextSize(1);
  gfx->setTextColor(C_WHITE);
  if (ok) { gfx->setCursor(68, 287); gfx->print("BRILLIANT! Well done!"); }
  else    { gfx->setCursor(68, 287); gfx->print("Not quite — try again!"); }
  // Big tick / cross
  gfx->setTextSize(2);
  gfx->setTextColor(brd);
  gfx->setCursor(12, 283);
  gfx->print(ok ? "+" : "x");
}

void clearFeedback() {
  gfx->fillRect(4, 284, 232, 10, C_BG);
}

// ══════════════════════════════════════════════════════════════════════
//  SCROLLING NEON HEADER  (main menu)
// ══════════════════════════════════════════════════════════════════════

void drawScrollHeader() {
  gfx->fillRect(0, 0, SCR_W, HDR_H, C_SURF);

  // Bottom accent lines
  gfx->drawFastHLine(0, HDR_H - 3, SCR_W, HDR_PAL[hdrPalI]);
  gfx->drawFastHLine(0, HDR_H - 1, SCR_W, C_DGRAY);

  // Corner indicator nodes
  uint16_t nc = HDR_PAL[hdrPalI];
  gfx->fillRect(0, 0, 4, 4, nc);
  gfx->fillRect(SCR_W - 4, 0, 4, 4, nc);

  // Scrolling rainbow text
  const char* txt = "   AJ2BUDDYCOMMS   ";
  int charW = 12;
  int txtW  = strlen(txt) * charW;

  gfx->setTextSize(2);
  for (int pass = 0; pass < 3; pass++) {
    int x = hdrOfs + pass * txtW;
    if (x > SCR_W) break;
    if (x + txtW < 0) continue;
    for (int ci = 0; ci < (int)strlen(txt); ci++) {
      int cx = x + ci * charW;
      if (cx < -charW || cx > SCR_W) continue;
      uint16_t cc = HDR_PAL[(hdrPalI + ci) % HDR_PAL_N];
      gfx->setTextColor(cc);
      gfx->setCursor(cx, 14);
      char ch[2] = { txt[ci], 0 };
      gfx->print(ch);
    }
  }
}

void tickHeader() {
  if (millis() - hdrMs < 38) return;
  hdrMs = millis();
  hdrOfs -= 2;
  const char* txt = "   AJ2BUDDYCOMMS   ";
  int txtW = strlen(txt) * 12;
  if (hdrOfs < -txtW) hdrOfs = 0;
  hdrTick++;
  if (hdrTick >= 12) { hdrTick = 0; hdrPalI = (hdrPalI + 1) % HDR_PAL_N; }
  drawScrollHeader();
}

// ── Static sub-header (sensor info, serial, games, mode) ──────────────
// showBack = true → overlays a back button on the left
void drawSubHeader(const char* title, uint16_t col, bool showBack) {
  gfx->fillRect(0, 0, SCR_W, HDR_H, C_SURF);
  gfx->drawFastHLine(0, HDR_H - 3, SCR_W, col);
  gfx->drawFastHLine(0, HDR_H - 1, SCR_W, C_DGRAY);

  // Centered title
  gfx->setTextSize(2);
  gfx->setTextColor(col);
  int tw = strlen(title) * 12;
  gfx->setCursor((SCR_W - tw) / 2, 14);
  gfx->print(title);

  if (showBack) drawHeaderBack();
}

// ══════════════════════════════════════════════════════════════════════
//  SPLASH SCREEN
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
    int te = rain[c].y - (int)rain[c].tail * 9;
    if (te >= 0 && te < SCR_H)
      gfx->fillRect(x, te, 14, 9, C_BLACK);
    if (rain[c].y >= 0 && rain[c].y < SCR_H) {
      gfx->setTextSize(1); gfx->setTextColor(C_MINT);
      gfx->setCursor(x, rain[c].y);
      gfx->print(chars[random(sizeof(chars) - 1)]);
    }
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

  // Subtle grid
  for (int i = 0; i < 320; i += 18) gfx->drawFastVLine(i, 0, 320, 0x0841);
  for (int i = 0; i < 240; i += 18) gfx->drawFastHLine(0, i, 240, 0x0841);

  const char* title = "BUDDYBOT";
  int titleX = 24, titleY = 82;
  gfx->setTextSize(4);

  for (int i = 0; i < 8; i++) {
    char ch[2] = { title[i], 0 };
    gfx->setTextColor(C_MAG, C_BLACK);
    gfx->setCursor(titleX + 4, titleY + 4);
    gfx->print(ch);
    gfx->setTextColor(C_CYAN, C_BLACK);
    gfx->setCursor(titleX, titleY);
    gfx->print(ch);
    gfx->setTextColor(0x7FFF, C_BLACK);
    gfx->setCursor(titleX - 2, titleY - 2);
    gfx->print(ch);
    for (int s = 0; s < 6; s++) {
      int sx = titleX + random(8, 24);
      int sy = titleY + random(4, 28);
      gfx->drawFastHLine(sx, sy, random(3, 9), 0x7FFF);
      delay(4);
    }
    titleX += 28;
    delay(95);
  }

  delay(180);
  gfx->setTextSize(4);
  gfx->setTextColor(C_CYAN, C_BLACK);
  gfx->setCursor(24, 82);
  gfx->print(title);

  delay(320);
  gfx->setTextSize(2);
  for (int offset = -80; offset <= 0; offset += 8) {
    gfx->fillRect(0, 178, 240, 22, C_BLACK);
    gfx->setTextColor(C_MAG, C_BLACK);
    gfx->setCursor(38 + offset, 178);
    gfx->print("by REINSMA INNOVATIONS");
    delay(18);
  }

  int barY = 238;
  gfx->drawRect(19, barY, 202, 20, C_CYAN);
  for (int prog = 0; prog <= 200; prog += 5) {
    uint16_t barCol = (prog < 70) ? C_CYAN : (prog < 130 ? C_MAG : 0x7FFF);
    gfx->fillRect(20, barY + 2, prog, 16, barCol);
    gfx->drawFastHLine(20 + (prog - 12), barY + 2, 14, C_WHITE);
    if (random(0, 3) == 0) {
      int sparkX = 20 + random(0, prog);
      gfx->drawFastVLine(sparkX, barY - 2, 4, 0x7FFF);
    }
    if (prog % 25 == 0) {
      gfx->fillRect(92, 262, 58, 12, C_BLACK);
      gfx->setTextSize(1); gfx->setTextColor(0x7FFF);
      gfx->setCursor(104, 264);
      gfx->print(prog / 2); gfx->print("%");
    }
    handleMegaLink();
    delay(12);
  }

  delay(280);
  gfx->fillScreen(0x7FFF);
  gfx->setTextSize(3); gfx->setTextColor(C_BLACK);
  gfx->setCursor(28, 108); gfx->print("SYSTEM ONLINE");
  delay(120);
  gfx->fillScreen(C_BLACK);
  delay(80);
  drawMainMenu();
}

// ══════════════════════════════════════════════════════════════════════
//  MAIN MENU  (paginated)
// ══════════════════════════════════════════════════════════════════════

void drawMainBtn(int idx) {
  static const char* TITLES[3] = { "TECH STATS", "AJ'S GAMES", "MODE" };
  static const char* SUBS[3]   = { "Sensors & comms", "4 fun activities", "" };
  static const uint16_t COLS[3] = { C_CYAN, C_YLLOW, C_CORAL };

  const int BH = 70, BX = 6, BW = 228;
  int by = CNT_Y + 6 + idx * (BH + 6);
  uint16_t col = COLS[idx];

  // Card body
  gfx->fillRoundRect(BX, by, BW, BH, 8, C_SURF);
  // Left accent bar
  gfx->fillRoundRect(BX, by, 5, BH, 4, col);
  // Border
  gfx->drawRoundRect(BX, by, BW, BH, 8, C_DGRAY);

  // Coloured icon box
  gfx->fillRoundRect(BX + 12, by + 12, 44, BH - 24, 6, C_BG);
  gfx->drawRoundRect(BX + 12, by + 12, 44, BH - 24, 6, col);

  // Icons
  int icx = BX + 34, icy = by + BH/2;
  switch(idx) {
    case 0: {
      gfx->fillRect(icx-9, icy-9, 18, 18, 0x0421);
      gfx->drawRect(icx-9, icy-9, 18, 18, col);
      for (int p = 0; p < 3; p++) {
        gfx->drawFastHLine(icx-15, icy-6+p*6, 6, col);
        gfx->drawFastHLine(icx+9,  icy-6+p*6, 6, col);
      }
      gfx->fillCircle(icx, icy, 3, col);
      break;
    }
    case 1: {
      drawStar(icx-7, icy-7, 7, 3, C_YLLOW);
      gfx->fillCircle(icx+7, icy-7, 6, C_CYAN);
      gfx->fillRect(icx-13, icy+1, 10, 10, C_CORAL);
      gfx->fillTriangle(icx+7,icy+1, icx+1,icy+12, icx+13,icy+12, C_MINT);
      break;
    }
    case 2: {
      uint16_t eyeC = MODE_COL[botMode];
      gfx->fillRoundRect(icx-10, icy-12, 20, 16, 3, 0x0421);
      gfx->drawRoundRect(icx-10, icy-12, 20, 16, 3, col);
      gfx->fillCircle(icx-4, icy-7, 3, eyeC);
      gfx->fillCircle(icx+4, icy-7, 3, eyeC);
      if (botMode == 0) {
        gfx->drawFastHLine(icx-4, icy-1, 8, eyeC);
      } else {
        gfx->drawFastHLine(icx-4, icy-2, 8, eyeC);
      }
      gfx->fillRoundRect(icx-7, icy+4, 14, 7, 2, 0x0421);
      gfx->drawRoundRect(icx-7, icy+4, 14, 7, 2, col);
      break;
    }
  }

  // Title
  gfx->setTextSize(2);
  gfx->setTextColor(C_WHITE);
  gfx->setCursor(BX + 66, by + 12);
  gfx->print(TITLES[idx]);

  // Subtitle / mode name
  gfx->setTextSize(1);
  gfx->setTextColor(col);
  gfx->setCursor(BX + 66, by + 36);
  if (idx == 2) gfx->print(MODE_NAME[botMode]);
  else          gfx->print(SUBS[idx]);

  // Chevron
  gfx->setTextSize(2);
  gfx->setTextColor(C_DGRAY);
  gfx->setCursor(BX + BW - 22, by + BH/2 - 8);
  gfx->print(">");
}

void drawMainMenu() {
  appState = S_MAIN;
  gfx->fillScreen(C_BG);
  drawScrollHeader();

  int totalPages = (MENU_N + MENU_PER_PAGE - 1) / MENU_PER_PAGE;
  int start = menuPage * MENU_PER_PAGE;
  int end   = min(start + MENU_PER_PAGE, MENU_N);

  for (int i = start; i < end; i++) {
    drawMainBtn(i - start);
  }

  // Pagination indicator (only if multiple pages)
  if (totalPages > 1) {
    int piY = PBAR_Y - 22;

    // Prev arrow
    uint16_t prevCol = (menuPage > 0) ? C_CYAN : C_DGRAY;
    gfx->fillRoundRect(6, piY, 34, 18, 5, C_SURF);
    gfx->drawRoundRect(6, piY, 34, 18, 5, prevCol);
    gfx->setTextSize(2); gfx->setTextColor(prevCol);
    gfx->setCursor(14, piY + 2); gfx->print("<");

    // Page dots
    for (int p = 0; p < totalPages; p++) {
      uint16_t dotC = (p == menuPage) ? C_CYAN : C_DGRAY;
      gfx->fillCircle(SCR_W/2 - (totalPages-1)*8 + p*16, piY + 9, 3, dotC);
    }

    // Next arrow
    uint16_t nextCol = (menuPage < totalPages - 1) ? C_CYAN : C_DGRAY;
    gfx->fillRoundRect(SCR_W - 40, piY, 34, 18, 5, C_SURF);
    gfx->drawRoundRect(SCR_W - 40, piY, 34, 18, 5, nextCol);
    gfx->setTextSize(2); gfx->setTextColor(nextCol);
    gfx->setCursor(SCR_W - 28, piY + 2); gfx->print(">");
  }

  drawPowerBar();
}

// ══════════════════════════════════════════════════════════════════════
//  TECH STATS MENU
// ══════════════════════════════════════════════════════════════════════

void drawTechMenu() {
  appState = S_TECH;
  gfx->fillScreen(C_BG);
  drawSubHeader("TECH STATS", C_CYAN, true);

  // Sensor Info card
  int cy1 = CNT_Y + 12;
  drawAccentCard(8, cy1, 224, 82, C_CYAN);
  gfx->fillRect(16, cy1+14, 36, 36, C_BG);
  gfx->drawRect(16, cy1+14, 36, 36, C_CYAN);
  for (int p = 0; p < 3; p++) gfx->drawFastHLine(8, cy1+20+p*9, 8, C_CYAN);
  gfx->fillCircle(34, cy1+32, 6, C_CYAN);
  gfx->setTextSize(2); gfx->setTextColor(C_WHITE);
  gfx->setCursor(60, cy1 + 16); gfx->print("SENSOR INFO");
  gfx->setTextSize(1); gfx->setTextColor(C_LGRAY);
  gfx->setCursor(60, cy1 + 42); gfx->print("Live readings + US HUD");
  gfx->setTextSize(2); gfx->setTextColor(C_DGRAY);
  gfx->setCursor(SCR_W - 26, cy1 + 28); gfx->print(">");

  // Serial Comms card
  int cy2 = CNT_Y + 106;
  drawAccentCard(8, cy2, 224, 82, C_MAG);
  gfx->fillRect(16, cy2+14, 36, 36, C_BG);
  gfx->drawRect(16, cy2+14, 36, 36, C_MAG);
  gfx->setTextSize(1); gfx->setTextColor(C_MAG);
  gfx->setCursor(18, cy2+18); gfx->print(">");
  gfx->setCursor(18, cy2+28); gfx->print("---");
  gfx->setCursor(18, cy2+38); gfx->print(">>_");
  gfx->setTextSize(2); gfx->setTextColor(C_WHITE);
  gfx->setCursor(60, cy2 + 16); gfx->print("SERIAL COMMS");
  gfx->setTextSize(1); gfx->setTextColor(C_LGRAY);
  gfx->setCursor(60, cy2 + 42); gfx->print("Real-time serial log");
  gfx->setTextSize(2); gfx->setTextColor(C_DGRAY);
  gfx->setCursor(SCR_W - 26, cy2 + 28); gfx->print(">");

  drawPowerBar();
}

// ══════════════════════════════════════════════════════════════════════
//  SENSOR INFO — Live Ultrasonic HUD + Environmental Data
// ══════════════════════════════════════════════════════════════════════

int beamLen(int16_t d) {
  if (d < 0) return 10;
  return constrain(map(d, 0, 200, 2, 52), 2, 52);
}

uint16_t distCol(int16_t d) {
  if (d < 0)   return C_MGRAY;
  if (d < 20)  return C_RED;
  if (d < 70)  return C_AMBER;
  return C_MINT;
}

void drawUSHUD() {
  const int CX = 120, CY = 136;
  const int RW = 17,  RH = 24;
  const int HX = 8,   HY = 68, HW = 224, HH = 128;

  gfx->fillRect(HX, HY, HW, HH, C_SURF);
  gfx->drawRect(HX, HY, HW, HH, C_DGRAY);

  // Faint grid
  for (int gx = HX + 22; gx < HX + HW; gx += 22)
    gfx->drawFastVLine(gx, HY, HH, C_LINE);
  for (int gy = HY + 16; gy < HY + HH; gy += 16)
    gfx->drawFastHLine(HX, gy, HW, C_LINE);

  // Labels
  gfx->setTextSize(1); gfx->setTextColor(C_LGRAY);
  gfx->setCursor(CX - 4, HY + 4); gfx->print("F");
  gfx->setCursor(CX - 4, HY + HH - 12); gfx->print("R");
  gfx->setCursor(HX + 4, CY - 4); gfx->print("L");
  gfx->setCursor(HX + HW - 10, CY - 4); gfx->print("R");

  // Beams
  int bF  = beamLen(sF);
  int bRr = beamLen(sR);
  int bL  = beamLen(sL);
  int bRi = beamLen(sRi);

  gfx->fillRect(CX - 3, CY - RH - bF, 6, bF, distCol(sF));
  gfx->fillRect(CX - 3, CY + RH, 6, bRr, distCol(sR));
  gfx->fillRect(CX - RW - bL, CY - 3, bL, 6, distCol(sL));
  gfx->fillRect(CX + RW, CY - 3, bRi, 6, distCol(sRi));

  // Danger ring
  if (sF >= 0 && sF < 20) gfx->drawCircle(CX, CY, 38, C_RED);
  if (sR >= 0 && sR < 20) gfx->drawCircle(CX, CY, 38, C_RED);

  // Robot body
  gfx->fillRoundRect(CX - RW, CY - RH, RW*2, RH*2, 5, 0x0C42);
  gfx->drawRoundRect(CX - RW, CY - RH, RW*2, RH*2, 5, C_CYAN);
  gfx->fillCircle(CX - 6, CY - 9, 4, MODE_COL[botMode]);
  gfx->fillCircle(CX + 6, CY - 9, 4, MODE_COL[botMode]);
  gfx->drawFastHLine(CX - 8, CY + 2, 16, C_CYAN);
  gfx->drawRect(CX - 5, CY + 6, 10, 5, C_CYAN);

  // Distance labels
  gfx->setTextSize(1);
  char buf[6];
  gfx->setTextColor(distCol(sF));
  snprintf(buf, sizeof(buf), "%d", (int)(sF >= 0 ? sF : 0));
  gfx->setCursor(CX + 5, CY - RH - bF); gfx->print(sF < 0 ? "?" : buf);
  gfx->setTextColor(distCol(sR));
  snprintf(buf, sizeof(buf), "%d", (int)(sR >= 0 ? sR : 0));
  gfx->setCursor(CX + 5, CY + RH + bRr - 8); gfx->print(sR < 0 ? "?" : buf);
  gfx->setTextColor(distCol(sL));
  snprintf(buf, sizeof(buf), "%d", (int)(sL >= 0 ? sL : 0));
  gfx->setCursor(CX - RW - bL, CY - 14); gfx->print(sL < 0 ? "?" : buf);
  gfx->setTextColor(distCol(sRi));
  snprintf(buf, sizeof(buf), "%d", (int)(sRi >= 0 ? sRi : 0));
  gfx->setCursor(CX + RW + bRi, CY - 14); gfx->print(sRi < 0 ? "?" : buf);
}

void drawEnvRow(int y, const char* label, const char* val, uint16_t col) {
  gfx->setTextSize(1);
  gfx->setTextColor(C_LGRAY, C_BG);
  gfx->setCursor(10, y); gfx->print(label);
  gfx->setTextColor(col, C_BG);
  gfx->setCursor(100, y); gfx->print(val);
  gfx->setCursor(100 + strlen(val) * 6, y); gfx->print(F("      "));
  gfx->drawFastHLine(8, y + 10, 224, C_LINE);
}

void drawSensorInfo() {
  appState = S_SENSORS;
  gfx->fillScreen(C_BG);
  drawSubHeader("SENSOR INFO", C_CYAN, true);

  gfx->setTextSize(1); gfx->setTextColor(C_LGRAY);
  gfx->setCursor(10, CNT_Y + 4); gfx->print("ULTRASONIC HUD  (cm · colour = proximity)");
  drawUSHUD();

  gfx->drawFastHLine(0, 200, SCR_W, C_DGRAY);

  char v[20];
  snprintf(v, sizeof(v), "%.1fC", botTemp);
  drawEnvRow(205, "TEMPERATURE:", v, C_AMBER);
  snprintf(v, sizeof(v), "%.0f%%", botHum);
  drawEnvRow(217, "HUMIDITY:   ", v, C_CYAN);
  snprintf(v, sizeof(v), "%d", botGas);
  drawEnvRow(229, "GAS LEVEL:  ", v, botGas > 400 ? C_CORAL : C_MINT);
  snprintf(v, sizeof(v), "%.1fV  %d%%", botVolt, botPct);
  drawEnvRow(241, "BATTERY:    ", v, botPct < 20 ? C_CORAL : C_MINT);
  snprintf(v, sizeof(v), "%s", botEstop ? "E-STOP!" : (botAuto ? "AUTO" : "MANUAL"));
  drawEnvRow(253, "BOT STATE:  ", v, botEstop ? C_RED : C_MINT);
  snprintf(v, sizeof(v), "%.2fV", botBoostVolt);
  drawEnvRow(265, "BOOST OUT:  ", v, (botBoostVolt > 0.5f) ? C_MINT : C_MGRAY);

  drawPowerBar();
}

// ══════════════════════════════════════════════════════════════════════
//  SERIAL COMMS SCREEN
// ══════════════════════════════════════════════════════════════════════

uint16_t logLineColor(const char* line) {
  if (strstr(line, "STAT"))    return C_CYAN;
  if (strstr(line, "PWR"))     return C_AMBER;
  if (strstr(line, "US"))      return C_MINT;
  if (strstr(line, "MODE"))    return C_ORANGE;
  if (strstr(line, "GESTURE")) return C_MAG;
  if (strstr(line, "STATUS"))  return 0xFB56;
  if (strstr(line, "ERR") || strstr(line, "CRITICAL")) return C_CORAL;
  return C_LGRAY;
}

void drawSerialScreen() {
  appState = S_SERIAL;
  gfx->fillScreen(C_BG);
  drawSubHeader("SERIAL COMMS", C_MAG, true);

  // CRT scanlines for atmosphere
  for (int y = HDR_H; y < PBAR_Y; y += 4)
    gfx->drawFastHLine(0, y, SCR_W, C_LINE);

  for (int i = 0; i < min((int)logCount, LOG_N); i++) {
    int lineIdx = ((int)logHead - (int)logCount + i + LOG_N * 2) % LOG_N;
    int ly      = HDR_H + 4 + i * 30;
    uint16_t lc = logLineColor(logLines[lineIdx]);

    gfx->fillRect(4, ly, 232, 27, C_SURF);
    gfx->fillRect(4, ly, 3, 27, lc);
    gfx->drawRect(7, ly, 229, 27, C_DGRAY);

    const char* raw = logLines[lineIdx];
    int tagEnd = 0;
    for (int k = 0; k < LOG_W && raw[k]; k++) {
      if (raw[k] == ']') { tagEnd = k + 1; break; }
    }

    gfx->setTextSize(1); gfx->setTextColor(lc);
    gfx->setCursor(11, ly + 4);
    char tagBuf[10] = {};
    strncpy(tagBuf, raw, min(9, tagEnd)); tagBuf[9] = '\0';
    gfx->print(tagBuf);

    gfx->setTextColor(C_WHITE);
    gfx->setCursor(11, ly + 15);
    const char* msg = (tagEnd > 0 && tagEnd < LOG_W) ? (raw + tagEnd) : raw;
    char msgBuf[36] = {};
    strncpy(msgBuf, msg, 35); msgBuf[35] = '\0';
    gfx->print(msgBuf);
  }

  if (logCount == 0) {
    gfx->setTextSize(1); gfx->setTextColor(C_MGRAY);
    gfx->setCursor(44, 152); gfx->print("Awaiting Mega data...");
    gfx->setCursor(28, 166); gfx->print("(US packets filtered out)");
  }

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
  const int W = 110, H = 90;
  gfx->fillRoundRect(x, y, W, H, 8, C_SURF);
  gfx->drawRoundRect(x, y, W, H, 8, col);
  gfx->fillRoundRect(x, y, W, 4, 4, col);

  int icx = x + 26, icy = y + H/2;
  switch (iconId) {
    case 0: drawStar(icx, icy, 15, 6, col); break;
    case 1: drawShape(2, icx, icy, 12, col); break;
    case 2: gfx->fillCircle(icx, icy, 13, col); break;
    case 3:
      gfx->setTextSize(3); gfx->setTextColor(col);
      gfx->setCursor(icx - 9, icy - 12); gfx->print("A");
      break;
  }

  gfx->setTextSize(2); gfx->setTextColor(col);
  gfx->setCursor(x + 48, y + 18); gfx->print(l1);
  gfx->setTextSize(2); gfx->setTextColor(C_LGRAY);
  gfx->setCursor(x + 48, y + 42); gfx->print(l2);
}

void drawGamesMenu() {
  appState = S_GAMES;
  gfx->fillScreen(C_BG);
  drawSubHeader("AJ'S GAMES", C_YLLOW, true);

  gfx->setTextSize(1); gfx->setTextColor(C_LGRAY);
  gfx->setCursor(16, CNT_Y + 8); gfx->print("Choose your game, AJ!");

  int gy = CNT_Y + 22;
  drawGameTile(5,   gy,      "COUNT", "STARS",  C_TEAL,  0);
  drawGameTile(125, gy,      "SHAPE", "MATCH",  C_MAG,   1);
  drawGameTile(5,   gy + 98, "COLOUR","QUEST",  C_ORANGE,2);
  drawGameTile(125, gy + 98, "LETTER","HUNT",   C_LIME,  3);

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
  gfx->fillScreen(C_BG);
  drawSubHeader("ROBOT MODE", C_CORAL, true);

  for (int i = 0; i < 3; i++) {
    int my = CNT_Y + 12 + i * 76;
    bool active = (botMode == i);
    uint16_t col = MODE_COL[i];
    uint16_t bg  = active ? MODE_BG[i] : C_SURF;

    gfx->fillRoundRect(6, my, 228, 68, 8, bg);
    gfx->drawRoundRect(6, my, 228, 68, 8, active ? col : C_DGRAY);
    if (active) {
      gfx->fillRoundRect(6, my, 5, 68, 3, col);
    }

    // Icon
    int icx = 36, icy = my + 34;
    uint16_t eyeC = col;
    gfx->fillRoundRect(icx-12, icy-13, 24, 18, 4, 0x0421);
    gfx->drawRoundRect(icx-12, icy-13, 24, 18, 4, col);
    gfx->fillCircle(icx-5, icy-7, 3, eyeC);
    gfx->fillCircle(icx+5, icy-7, 3, eyeC);
    if (i == 0) { gfx->drawFastHLine(icx-4, icy-1, 8, eyeC); }
    else        { gfx->drawFastHLine(icx-4, icy-3, 8, eyeC); }
    gfx->fillRoundRect(icx-7, icy+5, 14, 8, 2, 0x0421);
    gfx->drawRoundRect(icx-7, icy+5, 14, 8, 2, col);

    // Name
    gfx->setTextSize(2);
    gfx->setTextColor(active ? C_WHITE : col);
    gfx->setCursor(60, my + 12); gfx->print(MODE_NAME[i]);

    // Description
    gfx->setTextSize(1);
    gfx->setTextColor(active ? col : C_LGRAY);
    gfx->setCursor(60, my + 36); gfx->print(MODE_DESC[i]);

    // Active badge
    if (active) {
      gfx->fillRoundRect(170, my + 10, 56, 16, 5, col);
      gfx->setTextSize(1); gfx->setTextColor(C_BLACK);
      gfx->setCursor(174, my + 14); gfx->print("ACTIVE");
    }
  }

  drawPowerBar();
}

void selectMode(uint8_t m) {
  botMode = m;
  gfx->fillScreen(MODE_COL[m]);
  delay(80);
  gfx->fillScreen(C_BG);
  delay(40);
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
  const uint16_t SC[5] = { C_YLLOW, C_CORAL, C_CYAN, C_MINT, C_MAG };
  gfx->fillScreen(C_BG);
  drawSubHeader("COUNT STARS", C_TEAL, true);
  drawScoreBar();

  // Question panel
  drawAccentCard(6, G_QBOX_Y, 228, G_QBOX_H, C_TEAL);
  gfx->setTextSize(1); gfx->setTextColor(C_LGRAY);
  gfx->setCursor(52, G_QBOX_Y + 8); gfx->print("HOW MANY STARS?");

  // Star display — centred in question box
  int sz = (cntTgt <= 3) ? 20 : 15;
  int gap = sz * 2 + 8;
  int row0 = min((int)cntTgt, 3);
  int row1 = max(0, (int)cntTgt - 3);
  int y0   = (row1 > 0) ? G_QBOX_Y + 44 : G_QBOX_Y + 58;
  int x0   = (SCR_W - row0 * gap + 8) / 2 + sz;
  for (int i = 0; i < row0; i++) drawStar(x0 + i*gap, y0, sz, sz/2, SC[i%5]);
  if (row1 > 0) {
    int x1 = (SCR_W - row1*gap + 8)/2 + sz;
    for (int i = 0; i < row1; i++) drawStar(x1 + i*gap, y0 + sz*2+10, sz, sz/2, SC[(row0+i)%5]);
  }

  // 3 big answer buttons
  for (int i = 0; i < 3; i++) {
    int bx = 4 + i * (G_CNT_BW + 5);
    uint16_t col = SC[i];
    gfx->fillRoundRect(bx, G_ANS_Y, G_CNT_BW, G_CNT_BH, 9, C_SURF);
    gfx->drawRoundRect(bx, G_ANS_Y, G_CNT_BW, G_CNT_BH, 9, col);
    gfx->fillRoundRect(bx, G_ANS_Y, G_CNT_BW, 4, 4, col);
    // Giant number
    gfx->setTextSize(4); gfx->setTextColor(col);
    int numX = (cntOpt[i] >= 10) ? bx + 12 : bx + 22;
    gfx->setCursor(numX, G_ANS_Y + 20);
    gfx->print(cntOpt[i]);
    // Mini stars below number
    for (int s = 0; s < cntOpt[i]; s++) {
      drawStar(bx + 12 + s * 14, G_ANS_Y + G_CNT_BH - 14, 5, 2, col);
    }
  }

  drawPowerBar();
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
  const uint16_t OC[4] = { C_CYAN, C_ORANGE, C_MINT, C_MAG };
  gfx->fillScreen(C_BG);
  drawSubHeader("SHAPE MATCH", C_MAG, true);
  drawScoreBar();

  drawAccentCard(6, G_QBOX_Y, 228, G_QBOX_H, C_MAG);
  gfx->setTextSize(1); gfx->setTextColor(C_LGRAY);
  gfx->setCursor(34, G_QBOX_Y + 8); gfx->print("FIND THE MATCHING SHAPE!");

  // Central shape with glow ring
  gfx->drawCircle(120, G_QBOX_Y + 64, 46, C_DGRAY);
  gfx->drawCircle(120, G_QBOX_Y + 64, 44, SHP_COL[shpQ]);
  drawShape(shpQ, 120, G_QBOX_Y + 64, 30, SHP_COL[shpQ]);

  gfx->setTextSize(1); gfx->setTextColor(SHP_COL[shpQ]);
  int tw = strlen(SHP_NAME[shpQ]) * 6;
  gfx->setCursor((SCR_W - tw)/2, G_QBOX_Y + 98); gfx->print(SHP_NAME[shpQ]);

  // 2×2 answer grid
  for (int i = 0; i < 4; i++) {
    int bx = (i%2 == 0) ? 5 : 121;
    int by = G_ANS_Y + (i/2) * G_ANS_SP;
    gfx->fillRoundRect(bx, by, 110, G_ANS_H, 8, C_SURF);
    gfx->drawRoundRect(bx, by, 110, G_ANS_H, 8, OC[i]);
    drawShape(shpOpt[i], bx + 22, by + G_ANS_H/2, 13, OC[i]);
    gfx->setTextSize(1); gfx->setTextColor(C_WHITE);
    gfx->setCursor(bx + 44, by + G_ANS_H/2 - 4); gfx->print(SHP_NAME[shpOpt[i]]);
  }

  drawPowerBar();
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
  gfx->fillScreen(C_BG);
  drawSubHeader("COLOUR QUEST", C_ORANGE, true);
  drawScoreBar();

  drawAccentCard(6, G_QBOX_Y, 228, G_QBOX_H, C_ORANGE);
  gfx->setTextSize(1); gfx->setTextColor(C_LGRAY);
  gfx->setCursor(52, G_QBOX_Y + 8); gfx->print("TAP THIS COLOUR!");

  // Big filled colour circle
  gfx->drawCircle(120, G_QBOX_Y + 64, 48, C_DGRAY);
  gfx->drawCircle(120, G_QBOX_Y + 64, 46, CLR_VAL[clrQ]);
  gfx->fillCircle(120, G_QBOX_Y + 64, 44, CLR_VAL[clrQ]);

  // Colour name inside circle (contrasting)
  gfx->setTextSize(2); gfx->setTextColor(C_WHITE);
  int tw = strlen(CLR_NAME[clrQ]) * 12;
  gfx->setCursor((SCR_W - tw)/2, G_QBOX_Y + 56); gfx->print(CLR_NAME[clrQ]);

  // 2×2 answer grid — solid colour swatches
  for (int i = 0; i < 4; i++) {
    int bx = (i%2 == 0) ? 5 : 121;
    int by = G_ANS_Y + (i/2) * G_ANS_SP;
    gfx->fillRoundRect(bx, by, 110, G_ANS_H, 8, CLR_VAL[clrOpt[i]]);
    gfx->drawRoundRect(bx, by, 110, G_ANS_H, 8, C_WHITE);
    gfx->setTextSize(1); gfx->setTextColor(C_WHITE);
    int nl = strlen(CLR_NAME[clrOpt[i]]) * 6;
    gfx->setCursor(bx + (110-nl)/2, by + G_ANS_H/2 - 4); gfx->print(CLR_NAME[clrOpt[i]]);
  }

  drawPowerBar();
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
  const uint16_t LC[4] = { C_CYAN, C_ORANGE, C_MAG, C_YLLOW };
  gfx->fillScreen(C_BG);
  drawSubHeader("LETTER HUNT", C_LIME, true);
  drawScoreBar();

  drawAccentCard(6, G_QBOX_Y, 228, G_QBOX_H, C_LIME);
  gfx->setTextSize(1); gfx->setTextColor(C_LGRAY);
  gfx->setCursor(24, G_QBOX_Y + 8); gfx->print("FIND THIS LETTER AND TAP IT!");

  // Giant letter in glowing circle
  gfx->drawCircle(120, G_QBOX_Y + 64, 48, C_DGRAY);
  gfx->drawCircle(120, G_QBOX_Y + 64, 46, C_LIME);
  gfx->fillCircle(120, G_QBOX_Y + 64, 44, C_BG);
  // textSize 6 = 36px wide char
  gfx->setTextSize(6); gfx->setTextColor(C_LIME);
  char ql[2] = { LTR_SET[ltrQ], 0 };
  gfx->setCursor(120 - 18, G_QBOX_Y + 40); gfx->print(ql);

  // 2×2 answer grid
  for (int i = 0; i < 4; i++) {
    int bx = (i%2 == 0) ? 5 : 121;
    int by = G_ANS_Y + (i/2) * G_ANS_SP;
    gfx->fillRoundRect(bx, by, 110, G_ANS_H, 8, C_SURF);
    gfx->drawRoundRect(bx, by, 110, G_ANS_H, 8, LC[i]);
    gfx->fillRoundRect(bx, by, 110, 4, 4, LC[i]);
    char ll[2] = { LTR_SET[ltrOpt[i]], 0 };
    gfx->setTextSize(3); gfx->setTextColor(LC[i]);
    gfx->setCursor(bx + 38, by + (G_ANS_H - 24)/2); gfx->print(ll);
  }

  drawPowerBar();
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
  if (appState == S_CELEB) appState = returnGame;
  clearFeedback();
  switch(appState){
    case S_CNT: genCnt(); drawCountGame();  break;
    case S_SHP: genShp(); drawShapeGame();  break;
    case S_CLR: genClr(); drawColourGame(); break;
    case S_LTR: genLtr(); drawLetterGame(); break;
    default: drawGamesMenu(); break;
  }
}

// ══════════════════════════════════════════════════════════════════════
//  CELEBRATION
// ══════════════════════════════════════════════════════════════════════

void initCeleb() {
  AppState prev = appState;
  appState = S_CELEB; returnGame = prev; inFB = false;
  gfx->fillScreen(C_BG);

  // Sunburst lines
  for (int a = 0; a < 18; a++) {
    float ang = a * 0.3491f; // 20° increments
    int ex = 120 + (int)(160 * cosf(ang));
    int ey = 160 + (int)(160 * sinf(ang));
    gfx->drawLine(120, 160, ex, ey, C_SURF2);
  }

  gfx->setTextSize(3); gfx->setTextColor(C_YLLOW);
  gfx->setCursor(20, 44); gfx->print("AMAZING AJ!");

  gfx->setTextSize(2); gfx->setTextColor(C_WHITE);
  gfx->setCursor(50, 90); gfx->print("YOU'RE SO");
  gfx->setCursor(56, 114); gfx->print("CLEVER!");

  // Streak stars
  for (int i = 0; i < min((int)streak, 5); i++)
    drawStar(26 + i * 38, 152, 16, 7, C_YLLOW);

  // Score text
  gfx->setTextSize(1); gfx->setTextColor(C_CYAN);
  char sc[24];
  snprintf(sc, sizeof(sc), "Score: %d  Streak: %d", sesStars, streak);
  gfx->setCursor((SCR_W - strlen(sc)*6)/2, 184);
  gfx->print(sc);

  // Init particles from centre
  const uint16_t PC[6] = { C_YLLOW, C_CORAL, C_CYAN, C_MINT, C_MAG, C_ORANGE };
  for (int i = 0; i < 20; i++) {
    pts[i] = { SCR_W/2, 195, (int16_t)random(-10,11), (int16_t)random(-13,-4), PC[i%6] };
  }
  celFr = 0; celMs = millis();
}

void animateCeleb() {
  if (millis() - celMs < 35) return;
  celMs = millis(); celFr++;

  for (int i = 0; i < 20; i++) {
    gfx->fillRect(pts[i].x - 1, pts[i].y - 1, 10, 10, C_BG);
    pts[i].x += pts[i].vx;
    pts[i].vy += 1;
    pts[i].y  += pts[i].vy;
    if (pts[i].x < 4 || pts[i].x > SCR_W - 10) pts[i].vx = -pts[i].vx;
    if (pts[i].y > PBAR_Y - 8) {
      pts[i].vy = -(abs(pts[i].vy) * 3 / 4);
      pts[i].y  = PBAR_Y - 8;
    }
    gfx->fillRoundRect(pts[i].x, pts[i].y, 8, 8, 2, pts[i].col);
  }

  drawPowerBar();
  if (celFr >= 80) nextQuestion();
}

// ══════════════════════════════════════════════════════════════════════
//  ALERT OVERLAY
// ══════════════════════════════════════════════════════════════════════

void drawAlertOverlay() {
  if (!activeAlert.active) return;
  const uint16_t sevCol = OVL_SEV_COL[activeAlert.sev];
  const uint16_t sevBg  = OVL_SEV_BG[activeAlert.sev];
  const int OX=10, OY=40, OW=220, OH=236;

  // Dim backdrop (every 3rd row)
  for (int row = 0; row < SCR_H; row += 3)
    gfx->drawFastHLine(0, row, SCR_W, 0x0820);

  // Shadow
  gfx->fillRoundRect(OX+4, OY+4, OW, OH, 12, C_BLACK);

  // Panel body
  gfx->fillRoundRect(OX, OY, OW, OH, 12, 0x0841);
  gfx->drawRoundRect(OX, OY, OW, OH, 12, sevCol);

  // Header bar
  gfx->fillRoundRect(OX, OY, OW, 50, 12, sevBg);
  gfx->drawRoundRect(OX, OY, OW, 50, 12, sevCol);
  gfx->fillRect(OX, OY+38, OW, 12, sevBg);

  // Severity icon
  if (activeAlert.sev == SEV_CRITICAL) {
    gfx->fillTriangle(OX+22, OY+44, OX+38, OY+10, OX+54, OY+44, 0x6000);
    gfx->drawTriangle(OX+22, OY+44, OX+38, OY+10, OX+54, OY+44, sevCol);
    gfx->setTextColor(sevCol); gfx->setTextSize(2);
    gfx->setCursor(OX+33, OY+22); gfx->print('!');
  } else if (activeAlert.sev == SEV_WARN) {
    gfx->fillCircle(OX+38, OY+25, 17, sevBg);
    gfx->drawCircle(OX+38, OY+25, 17, sevCol);
    gfx->setTextColor(sevCol); gfx->setTextSize(2);
    gfx->setCursor(OX+31, OY+16); gfx->print('!');
  } else {
    gfx->fillCircle(OX+38, OY+25, 17, sevBg);
    gfx->drawCircle(OX+38, OY+25, 17, sevCol);
    gfx->setTextColor(sevCol); gfx->setTextSize(2);
    gfx->setCursor(OX+32, OY+16); gfx->print('i');
  }

  // Labels + title
  gfx->setTextColor(sevCol); gfx->setTextSize(1);
  gfx->setCursor(OX+64, OY+8); gfx->print(OVL_SEV_LBL[activeAlert.sev]);
  gfx->setTextColor(C_WHITE); gfx->setTextSize(2);
  gfx->setCursor(OX+64, OY+22); gfx->print(activeAlert.title);

  // Message body
  gfx->setTextColor(C_LGRAY); gfx->setTextSize(1);
  int bx = OX+14, by = OY+60;
  const char* p = activeAlert.msg;
  char line[36]; int li = 0;
  while (*p) {
    if ((*p == ' ' && li > 24) || li >= 35) {
      line[li] = '\0';
      gfx->setCursor(bx, by); gfx->print(line);
      by += 14; li = 0;
      if (*p == ' ') { p++; continue; }
    } else { line[li++] = *p; }
    p++;
  }
  if (li > 0) { line[li] = '\0'; gfx->setCursor(bx, by); gfx->print(line); }

  // Buttons
  int btnY = OY + OH - 54;

  gfx->fillRoundRect(OX+12, btnY, 88, 42, 9, 0x0440);
  gfx->drawRoundRect(OX+12, btnY, 88, 42, 9, C_MINT);
  gfx->setTextColor(C_MINT); gfx->setTextSize(2);
  gfx->setCursor(OX+28, btnY+13); gfx->print("OKAY");

  uint16_t ignCol  = activeAlert.ignorable ? C_AMBER : C_MGRAY;
  uint16_t ignFill = activeAlert.ignorable ? 0x2200 : C_SURF;
  gfx->fillRoundRect(OX+120, btnY, 88, 42, 9, ignFill);
  gfx->drawRoundRect(OX+120, btnY, 88, 42, 9, ignCol);
  gfx->setTextColor(ignCol); gfx->setTextSize(2);
  gfx->setCursor(OX+128, btnY+13); gfx->print("IGNORE");
}

void showAlert(AlertSeverity sev, const char* title, const char* body, bool ignorable) {
  if (activeAlert.active && activeAlert.sev > sev) return;
  activeAlert.active    = true;
  activeAlert.sev       = sev;
  activeAlert.ignorable = ignorable && (sev != SEV_CRITICAL);
  strncpy(activeAlert.title, title, 27); activeAlert.title[27] = '\0';
  strncpy(activeAlert.msg,   body,  87); activeAlert.msg[87]   = '\0';
  drawAlertOverlay();
}

void dismissAlert(bool ignore) {
  if (ignore && !activeAlert.ignorable) return;
  activeAlert.active = false;
  switch (appState) {
    case S_MAIN:    drawMainMenu();     break;
    case S_TECH:    drawTechMenu();     break;
    case S_SENSORS: drawSensorInfo();   break;
    case S_SERIAL:  drawSerialScreen(); break;
    case S_GAMES:   drawGamesMenu();    break;
    case S_MODE:    drawModeScreen();   break;
    default:        drawMainMenu();     break;
  }
}

// ══════════════════════════════════════════════════════════════════════
//  TOUCH HANDLER
// ══════════════════════════════════════════════════════════════════════

// Returns index 0-3 for 2×2 answer grid, else -1
int8_t hit4(int tx, int ty) {
  for (int i = 0; i < 4; i++) {
    int bx = (i%2 == 0) ? 5 : 121;
    int by = G_ANS_Y + (i/2) * G_ANS_SP;
    if (tx > bx && tx < bx+110 && ty > by && ty < by+G_ANS_H) return i;
  }
  return -1;
}

void handleTouch() {
  TSPoint p = ts.getPoint();
  restorePins();
  if (p.z < 180 || p.z > 1100) return;
  int tx = map(p.x, TS_MAXX, TS_MINX, 0, SCR_W);
  int ty = map(p.y, TS_MINY, TS_MAXY, 0, SCR_H);
  delay(55);

  // Alert overlay intercepts all touch
  if (activeAlert.active) {
    const int OX=10, OY=40, OW=220, OH=236;
    int btnY = OY + OH - 54;
    if (tx >= OX+12 && tx <= OX+100 && ty >= btnY && ty <= btnY+42) {
      dismissAlert(false);
    } else if (tx >= OX+120 && tx <= OX+208 && ty >= btnY && ty <= btnY+42
               && activeAlert.ignorable) {
      dismissAlert(true);
    }
    return;
  }

  // Back button: overlaid in header, available in all submenus
  bool onBack = (tx < BACK_TX && ty < BACK_TY);

  switch(appState) {

    // ── Startup ───────────────────────────────────────────────────────
    case S_STARTUP: {
      if (ty >= 260 && ty <= 294) {
        if (startReady) {
          proceedFromStartup();
        } else {
          unsigned long elapsed = millis() - startMs;
          if (elapsed > START_SKIP_DELAY) {
            if (!skipConfirm) {
              skipConfirm = true; skipTapMs = millis(); drawStartupScreen();
            } else if (millis() - skipTapMs < 3000) {
              skipConfirm = false; proceedFromStartup();
            } else {
              skipConfirm = false; drawStartupScreen();
            }
          }
        }
      } else if (ty >= 40 && ty <= 78) {
        pingSeq++;
        Serial1.print(F("PING_R4:")); Serial1.println(pingSeq);
        snprintf(bsMega.faultMsg, sizeof(bsMega.faultMsg), "Retrying ping seq=%d...", pingSeq);
        drawStartupScreen();
      }
      break;
    }

    // ── Main Menu ─────────────────────────────────────────────────────
    case S_MAIN: {
      const int BH=70, BY0=CNT_Y+6;
      int totalPages = (MENU_N + MENU_PER_PAGE - 1) / MENU_PER_PAGE;

      // Page arrows (bottom area)
      if (totalPages > 1 && ty >= PBAR_Y - 22 && ty <= PBAR_Y - 4) {
        if (tx >= 6 && tx <= 40 && menuPage > 0) {
          menuPage--; drawMainMenu(); break;
        }
        if (tx >= SCR_W-40 && tx <= SCR_W-6 && menuPage < totalPages-1) {
          menuPage++; drawMainMenu(); break;
        }
      }

      // Button taps
      int start = menuPage * MENU_PER_PAGE;
      for (int i = 0; i < MENU_PER_PAGE && (start+i) < MENU_N; i++) {
        int by = BY0 + i * (BH + 6);
        if (ty > by && ty < by + BH) {
          int item = start + i;
          if (item == 0) { drawTechMenu(); break; }
          if (item == 1) { drawGamesMenu(); break; }
          if (item == 2) { drawModeScreen(); break; }
          break;
        }
      }
      break;
    }

    // ── Tech Menu ─────────────────────────────────────────────────────
    case S_TECH:
      if (onBack) { drawMainMenu(); break; }
      if (ty > CNT_Y+12 && ty < CNT_Y+94)  { drawSensorInfo();  break; }
      if (ty > CNT_Y+106 && ty < CNT_Y+188){ drawSerialScreen(); break; }
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
      if (onBack) { drawMainMenu(); break; }
      int gy1 = CNT_Y + 22, gy2 = CNT_Y + 120;
      if (tx < 120 && ty > gy1 && ty < gy1 + 90) { startCountGame();  break; }
      if (tx >= 120 && ty > gy1 && ty < gy1 + 90) { startShapeGame(); break; }
      if (tx < 120 && ty > gy2 && ty < gy2 + 90) { startColourGame(); break; }
      if (tx >= 120 && ty > gy2 && ty < gy2 + 90) { startLetterGame(); break; }
      break;
    }

    // ── Mode Screen ───────────────────────────────────────────────────
    case S_MODE: {
      if (onBack) { drawMainMenu(); break; }
      for (int i = 0; i < 3; i++) {
        int my = CNT_Y + 12 + i * 76;
        if (ty > my && ty < my + 68) { selectMode(i); break; }
      }
      break;
    }

    // ── Count Stars ───────────────────────────────────────────────────
    case S_CNT:
      if (onBack) { inFB = false; awaitIn = false; drawGamesMenu(); break; }
      if (inFB) break;
      for (int i = 0; i < 3; i++) {
        int bx = 4 + i * (G_CNT_BW + 5);
        if (tx > bx && tx < bx+G_CNT_BW && ty > G_ANS_Y && ty < G_ANS_Y+G_CNT_BH) {
          checkAnswer(i); break;
        }
      }
      break;

    // ── Shape / Colour / Letter ───────────────────────────────────────
    case S_SHP: case S_CLR: case S_LTR:
      if (onBack) { inFB = false; awaitIn = false; drawGamesMenu(); break; }
      if (!inFB) { int8_t h = hit4(tx, ty); if (h >= 0) checkAnswer(h); }
      break;

    default: break;
  }
}

// ══════════════════════════════════════════════════════════════════════
//  STARTUP DIAGNOSTIC SCREEN
// ══════════════════════════════════════════════════════════════════════

uint16_t qualCol(uint8_t q) {
  if (q >= 70) return C_MINT;
  if (q >= 40) return C_AMBER;
  return C_CORAL;
}

void updateMegaFault() {
  if (bsMega.ready) {
    snprintf(bsMega.faultMsg, sizeof(bsMega.faultMsg), "Link OK  CRC_OK:%lu FAIL:%lu", crcOK, crcFail);
  } else if (sniffTotal == 0 && millis() - startMs > 5000) {
    snprintf(bsMega.faultMsg, sizeof(bsMega.faultMsg), "NO DATA on RX — check TX18>D0");
  } else if (sniffTotal > 0 && bsMega.msgsOK == 0) {
    snprintf(bsMega.faultMsg, sizeof(bsMega.faultMsg), "Bytes rcvd, no packets — baud?");
  } else if (bsMega.msgsOK > 0 && pingOkCount < PING_LINK_THRESH) {
    snprintf(bsMega.faultMsg, sizeof(bsMega.faultMsg), "Pkts OK, ping %d/%d — check RX19<D1", pingOkCount, PING_LINK_THRESH);
  } else {
    snprintf(bsMega.faultMsg, sizeof(bsMega.faultMsg), "%u bytes  %lu msgs  %lu fail", sniffTotal, bsMega.msgsOK, bsMega.msgsFail);
  }
}

void drawBoardRow(int y, const char* label, BoardStat& bs, bool isMega) {
  uint16_t statusCol = bs.ready ? C_MINT : (bs.linked ? C_AMBER : C_MGRAY);
  const char* statusStr = bs.ready ? "READY" : (bs.linked ? "LINK " : "WAIT ");

  gfx->fillRoundRect(4, y, SCR_W-8, 38, 5,
    bs.ready ? 0x0422 : (bs.linked ? 0x2200 : C_SURF));
  gfx->drawRoundRect(4, y, SCR_W-8, 38, 5, statusCol);

  // Status dot
  gfx->fillCircle(18, y+19, 7, statusCol);
  if (bs.ready || bs.linked) {
    gfx->setTextColor(C_BLACK); gfx->setTextSize(1);
    gfx->setCursor(bs.ready ? 13 : 14, y+15);
    gfx->print(bs.ready ? F("OK") : F("~"));
  }

  gfx->setTextColor(bs.ready ? C_WHITE : (bs.linked ? C_AMBER : C_LGRAY));
  gfx->setTextSize(1);
  gfx->setCursor(30, y+6); gfx->print(label);

  gfx->setTextColor(statusCol); gfx->setTextSize(1);
  gfx->setCursor(SCR_W-52, y+6); gfx->print(statusStr);

  // Quality bar
  int barX = 30, barY = y+22, barW = 100, barH = 6;
  gfx->drawRect(barX, barY, barW, barH, C_DGRAY);
  int fillW = (bs.qualPct * (barW-2)) / 100;
  if (fillW > 0) gfx->fillRect(barX+1, barY+1, fillW, barH-2, qualCol(bs.qualPct));

  char info[20];
  if (isMega) snprintf(info, sizeof(info), "%dB/s  Q:%d%%", bytesPerSec, bs.qualPct);
  else        snprintf(info, sizeof(info), "%lu msg  Q:%d%%", bs.msgsOK, bs.qualPct);
  gfx->setTextColor(C_LGRAY); gfx->setTextSize(1);
  gfx->setCursor(barX + barW + 4, y+19); gfx->print(info);

  if (bs.faultMsg[0] && !bs.ready) {
    gfx->setTextColor(bs.linked ? C_AMBER : C_CORAL);
    gfx->setTextSize(1); gfx->setCursor(30, y+28);
    char trunc[26]; strncpy(trunc, bs.faultMsg, 25); trunc[25]='\0';
    gfx->print(trunc);
  } else if (bs.fwVer[0]) {
    gfx->setTextColor(C_DCYAN); gfx->setTextSize(1);
    gfx->setCursor(30, y+28); gfx->print(bs.fwVer);
  }
}

void drawHexSniffer() {
  int sy = 222;
  gfx->fillRect(4, sy, SCR_W-8, 26, C_SURF);
  gfx->drawRect(4, sy, SCR_W-8, 26, C_DGRAY);
  gfx->setTextColor(C_DCYAN); gfx->setTextSize(1);
  gfx->setCursor(8, sy+3); gfx->print(F("RX SNIFFER:"));
  gfx->setTextColor(sniffTotal > 0 ? C_CYAN : C_MGRAY);
  gfx->setCursor(8, sy+14);
  if (sniffTotal == 0) {
    gfx->print(F("-- -- -- -- -- -- -- -- -- -- -- --"));
  } else {
    char hex[4];
    for (int i = 0; i < SNIFF_N; i++) {
      int idx = (sniffWr - SNIFF_N + i + SNIFF_N) % SNIFF_N;
      sprintf(hex, "%02X ", sniffBuf[idx]);
      gfx->print(hex);
    }
  }
}

void drawStartupScreen() {
  gfx->fillScreen(C_BG);

  // Animated header
  static uint8_t hdrAnim = 0; hdrAnim = (hdrAnim + 1) % HDR_PAL_N;
  uint16_t hdrC = HDR_PAL[hdrAnim];
  gfx->fillRect(0, 0, SCR_W, 38, C_SURF);
  // Corner brackets
  gfx->drawFastHLine(0, 0, 16, hdrC); gfx->drawFastVLine(0, 0, 10, hdrC);
  gfx->drawFastHLine(SCR_W-16, 0, 16, hdrC); gfx->drawFastVLine(SCR_W-1, 0, 10, hdrC);
  gfx->drawFastHLine(0, 36, SCR_W, hdrC);
  gfx->drawFastHLine(0, 37, SCR_W, C_DGRAY);

  gfx->setTextColor(hdrC); gfx->setTextSize(2);
  gfx->setCursor(12, 4); gfx->print(F("BUDDYBOT DIAG"));

  // Animated dots
  static uint8_t dotAnim = 0; dotAnim = (dotAnim + 1) % 4;
  gfx->setTextColor(C_MGRAY); gfx->setTextSize(1);
  gfx->setCursor(12, 26); gfx->print(F("System startup check"));
  gfx->setTextColor(hdrC); gfx->setCursor(136, 26);
  for (int d = 0; d < 3; d++) gfx->print(d < (int)dotAnim ? '.' : ' ');

  // Board rows
  updateMegaFault();
  drawBoardRow(40,  "MEGA V29.0", bsMega,  true);
  drawBoardRow(82,  "R3 MOTORS",  bsR3,    false);
  drawBoardRow(124, "ANDROID S9", bsS9,    false);
  drawBoardRow(166, "ESP32 WIFI", bsESP32, false);

  drawHexSniffer();

  // WiFi IP
  gfx->setTextColor(bsESP32.ready ? C_MINT : C_MGRAY);
  gfx->setTextSize(1); gfx->setCursor(8, 252);
  gfx->print(F("WiFi IP: "));
  gfx->print(bsESP32.fwVer[0] ? bsESP32.fwVer : (bsESP32.linked ? "BT only" : "Waiting..."));

  // Start / skip button
  unsigned long elapsed = millis() - startMs;
  if (startReady) {
    gfx->fillRoundRect(20, 260, SCR_W-40, 34, 8, 0x0440);
    gfx->drawRoundRect(20, 260, SCR_W-40, 34, 8, C_MINT);
    gfx->setTextColor(C_MINT); gfx->setTextSize(2);
    gfx->setCursor(38, 271); gfx->print(F("TAP TO START"));
  } else if (elapsed > START_SKIP_DELAY) {
    uint16_t skipCol = skipConfirm ? C_RED : C_AMBER;
    gfx->fillRoundRect(20, 260, SCR_W-40, 34, 8, 0x2000);
    gfx->drawRoundRect(20, 260, SCR_W-40, 34, 8, skipCol);
    gfx->setTextColor(skipCol); gfx->setTextSize(1);
    gfx->setCursor(26, 266);
    gfx->print(skipConfirm ? F("TAP AGAIN TO FORCE START") : F("SKIP — some boards not ready"));
    gfx->setCursor(26, 280);
    char tbuf[24]; snprintf(tbuf, sizeof(tbuf), "Elapsed: %lus", elapsed/1000);
    gfx->print(tbuf);
  } else {
    unsigned long remaining = (START_SKIP_DELAY - elapsed) / 1000;
    gfx->fillRoundRect(20, 260, SCR_W-40, 34, 8, C_SURF);
    gfx->drawRoundRect(20, 260, SCR_W-40, 34, 8, C_DGRAY);
    gfx->setTextColor(C_MGRAY); gfx->setTextSize(1);
    gfx->setCursor(36, 266); gfx->print(F("Waiting for boards..."));
    gfx->setCursor(36, 278);
    char tbuf[28]; snprintf(tbuf, sizeof(tbuf), "SKIP available in %lus", remaining);
    gfx->print(tbuf);
  }

  drawPowerBar();
}

// ─── Transition from startup to main ──────────────────────────────────
void proceedFromStartup() {
  for (int x = 0; x < SCR_W / 2 + 2; x += 6) {
    gfx->fillRect(0,         0, x,      SCR_H, 0x0440);
    gfx->fillRect(SCR_W - x, 0, x + 2, SCR_H, 0x0440);
    delay(4);
  }
  gfx->fillScreen(0x0440);
  gfx->setTextColor(0x0220); gfx->setTextSize(3);
  gfx->setCursor(22, 122); gfx->print(F("SYSTEM ONLINE"));
  gfx->setTextColor(C_LIME); gfx->setTextSize(3);
  gfx->setCursor(20, 120); gfx->print(F("SYSTEM ONLINE"));
  delay(180);

  int top = 0, bot = SCR_H - 1;
  while (top < bot) {
    gfx->drawFastHLine(0, top, SCR_W, C_LIME);
    gfx->drawFastHLine(0, bot, SCR_W, C_LIME);
    top += 4; bot -= 4;
    delay(3);
  }
  gfx->drawFastHLine(0, SCR_H / 2, SCR_W, C_WHITE);
  gfx->drawFastHLine(0, SCR_H/2-1, SCR_W, C_WHITE);
  delay(40);
  gfx->fillScreen(C_BLACK);
  delay(60);

  doSplash();
  drawMainMenu();
}

// ══════════════════════════════════════════════════════════════════════
//  MEGA LINK — Telemetry Parser  (unchanged from original)
// ══════════════════════════════════════════════════════════════════════

void handleMegaLink() {
  static char megaBuf[120];
  static uint8_t megaLen = 0;

  unsigned long nowMs = millis();
  if (nowMs - sniffWindowStart >= 1000) {
    bytesPerSec       = (uint8_t)min(windowBytes, (uint32_t)255);
    windowBytes       = 0;
    sniffWindowStart  = nowMs;
    if (bytesPerSec > 0) {
      if (bsMega.qualPct < 95) bsMega.qualPct += 5;
    } else {
      if (bsMega.qualPct > 5) bsMega.qualPct -= 5;
      else bsMega.qualPct = 0;
    }
  }

  while (Serial1.available()) {
    char c = Serial1.read();
    sniffTotal++;
    windowBytes++;
    bsMega.bytesRx++;
    bsMega.lastRxMs = millis();
    bsMega.linked   = true;
    sniffBuf[sniffWr] = (uint8_t)c;
    sniffWr = (sniffWr + 1) % SNIFF_N;

    if (c == '\r') continue;
    if (c == '\n') {
      if (megaLen == 0) continue;
      megaBuf[megaLen] = '\0';
      String raw = String(megaBuf);
      raw.trim();
      megaLen = 0;
      if (raw.length() == 0) continue;

      bool crcValid = true;
      int crcIdx = raw.lastIndexOf("|CRC:");
      if (crcIdx > 0) {
        String crcHex = raw.substring(crcIdx + 5);
        String payload = raw.substring(0, crcIdx);
        uint8_t expected = (uint8_t)strtoul(crcHex.c_str(), nullptr, 16);
        uint8_t actual   = r4CalcCRC(payload.c_str(), payload.length());
        if (expected == actual) { crcOK++; raw = payload; }
        else { crcFail++; bsMega.msgsFail++; crcValid = false; raw = payload; }
      }

      bsMega.msgsOK++;

      // [Phase 3B] Auto-pairing: Mega sends "PING" during setup() to verify link.
      // R4 must reply "PONG" so Mega's r4Ready flag is set.
      if (raw == "PING") {
        Serial1.println(F("PONG"));
        bsMega.linked = true;
        continue;
      }

      if (raw.startsWith("PONG_R4:")) {
        uint16_t seq = (uint16_t)raw.substring(8).toInt();
        if (seq == pingSeq) {
          lastPongRxMs = millis();
          if (pingOkCount < PING_LINK_THRESH) pingOkCount++;
          if (pingOkCount >= PING_LINK_THRESH && !bsMega.ready) {
            bsMega.ready = true;
            if (appState == S_STARTUP) drawStartupScreen();
          }
        }
        continue;
      }

      if (!raw.startsWith("US:")) {
        char tag[6] = "MEGA";
        if      (raw.startsWith("STAT"))     strcpy(tag,"STAT");
        else if (raw.startsWith("PWR"))      strcpy(tag,"PWR");
        else if (raw.startsWith("STATUS"))   strcpy(tag,"STAT");
        else if (raw.startsWith("MODE"))     strcpy(tag,"MODE");
        else if (raw.startsWith("GESTURE"))  strcpy(tag,"GEST");
        else if (raw.startsWith("BAT:"))     strcpy(tag,"BAT");
        else if (raw.startsWith("SAFETY:"))  strcpy(tag,"SAFE");
        else if (raw.startsWith("SENS_ST|")) strcpy(tag,"SENS");
        else if (raw.startsWith("MEGA_READY")) strcpy(tag,"BOOT");
        else if (raw.startsWith("CONN_ST"))  strcpy(tag,"CONN");
        addLog(tag, raw.c_str());
      }

      char buf[120];
      raw.toCharArray(buf, sizeof(buf));
      char *tok;

      if (raw.startsWith("MEGA_READY|")) {
        bsMega.linked = true;
        int fwIdx = raw.indexOf("FW:");
        if (fwIdx > 0) {
          String fwStr = raw.substring(fwIdx+3);
          int endIdx = fwStr.indexOf('|');
          if (endIdx > 0) fwStr = fwStr.substring(0, endIdx);
          fwStr.toCharArray(bsMega.fwVer, sizeof(bsMega.fwVer));
        }
        if (appState == S_STARTUP) drawStartupScreen();
        continue;
      }

      if (raw.startsWith("CONN_STATUS|")) {
        bsMega.linked = true;
        bsR3.linked   = (raw.indexOf("R3:OK") >= 0);
        bsR3.ready    = bsR3.linked;
        bsR3.qualPct  = bsR3.ready ? 100 : 0;
        if (bsR3.ready) snprintf(bsR3.faultMsg, sizeof(bsR3.faultMsg), "Motor slave ACK confirmed");
        else            snprintf(bsR3.faultMsg, sizeof(bsR3.faultMsg), "No ACK — check SS pins 10/11");
        int fwIdx = raw.indexOf("FW:");
        if (fwIdx > 0) {
          String fwStr = raw.substring(fwIdx+3);
          int endIdx = fwStr.indexOf('|');
          if (endIdx > 0) fwStr = fwStr.substring(0, endIdx);
          fwStr.toCharArray(bsMega.fwVer, sizeof(bsMega.fwVer));
        }
        if (appState == S_STARTUP) drawStartupScreen();
        continue;
      }

      if (raw.startsWith("WIFI_IP:")) {
        String ip = raw.substring(8); ip.trim();
        if (ip == "BT_ONLY") {
          snprintf(bsESP32.fwVer, sizeof(bsESP32.fwVer), "BT-only mode");
          snprintf(bsESP32.faultMsg, sizeof(bsESP32.faultMsg), "WiFi failed — BT available");
          bsESP32.linked = true; bsESP32.ready = true; bsESP32.qualPct = 60;
        } else {
          ip.toCharArray(bsESP32.fwVer, sizeof(bsESP32.fwVer));
          bsESP32.linked = true; bsESP32.ready = true; bsESP32.qualPct = 100;
          snprintf(bsESP32.faultMsg, sizeof(bsESP32.faultMsg), "WiFi connected");
        }
        if (appState == S_STARTUP) drawStartupScreen();
        continue;
      }

      if (raw.startsWith("STATUS|")) {
        botEstop = (raw.indexOf("ESTOP:YES") >= 0);
        botAuto  = (raw.indexOf("AUTO:ON")   >= 0);
        if (raw.indexOf("R3:OK") >= 0)   { bsR3.linked=true; bsR3.ready=true; bsR3.qualPct=100; }
        if (raw.indexOf("R3:FAIL") >= 0) { bsR3.ready=false; }
        if (raw.indexOf("ESP:OK") >= 0)  { bsESP32.linked=true; if(!bsESP32.ready) bsESP32.qualPct=80; }
        if (raw.indexOf("S9:OK") >= 0)   { bsS9.linked=true; bsS9.ready=true; bsS9.qualPct=100;
          snprintf(bsS9.faultMsg, sizeof(bsS9.faultMsg), "Android app connected"); }
        else { if(!bsS9.ready) snprintf(bsS9.faultMsg, sizeof(bsS9.faultMsg), "No S9 — use wireless ADB"); }
        int vi = raw.indexOf("BAT:");
        if (vi >= 0) botVolt = raw.substring(vi+4).toFloat();
        int pi = raw.indexOf("PCT:");
        if (pi >= 0) botPct  = constrain(raw.substring(pi+4).toInt(),0,100);
        if (appState == S_STARTUP) drawStartupScreen();
        continue;
      }

      if (raw.startsWith("SYSTEM|READY")) {
        bsMega.linked = true;
        if (appState == S_STARTUP) drawStartupScreen();
        continue;
      }

      if (raw.startsWith("MODE:")) {
        String mode = raw.substring(5);
        if      (mode == "NORMAL")    botMode = 0;
        else if (mode == "BODYGUARD") botMode = 1;
        else if (mode == "GUARD DOG" || mode == "DOG") botMode = 2;
        continue;
      }

      if (raw.startsWith("BAT:")) {
        alertBatWarn = alertBatLow = alertBatCritical = false;
        if (raw == "BAT:WARN") {
          alertBatWarn = true;
          snprintf(alertText, sizeof(alertText), "Battery warning");
          showAlert(SEV_INFO, "BAT WARNING", "Battery below optimal. Consider charging soon.", true);
        } else if (raw == "BAT:LOW") {
          alertBatLow = true;
          snprintf(alertText, sizeof(alertText), "Battery low");
          showAlert(SEV_WARN, "BAT LOW", "Battery is running low. Speed reduced. Charge when possible.", true);
        } else if (raw == "BAT:CRITICAL") {
          alertBatCritical = true;
          snprintf(alertText, sizeof(alertText), "Battery critical");
          showAlert(SEV_CRITICAL, "BAT CRITICAL", "Battery critically low. BuddyBot has stopped. Charge immediately.", false);
        }
        continue;
      }

      if (raw.startsWith("SAFETY:")) {
        alertTilt = alertFlame = alertGas = false;
        if (raw == "SAFETY:TILT") {
          alertTilt = true;
          snprintf(alertText, sizeof(alertText), "Tilt detected");
          showAlert(SEV_WARN, "TILT ALERT", "BuddyBot has been tilted or knocked over. Motors stopped.", true);
        } else if (raw == "SAFETY:FLAME_ALERT") {
          alertFlame = true;
          snprintf(alertText, sizeof(alertText), "Flame alert!");
          showAlert(SEV_CRITICAL, "FLAME ALERT", "Flame detected nearby! Check surroundings immediately.", false);
        } else if (raw == "SAFETY:GAS_ALERT") {
          alertGas = true;
          snprintf(alertText, sizeof(alertText), "Gas alert!");
          showAlert(SEV_WARN, "GAS ALERT", "Gas sensor triggered. Ensure adequate ventilation.", true);
        }
        continue;
      }

      if (raw.startsWith("GESTURE:")) { continue; }

      if (raw.startsWith("SENS_ST|")) {
        int end2 = raw.indexOf("|END");
        lastSensStatus = (end2 > 0) ? raw.substring(8, end2) : raw.substring(8);
        sensNeedsRedraw = true;
        continue;
      }

      if (raw.startsWith("US:")) {
        sscanf(buf + 3, "%d,%d,%d,%d", &sF, &sR, &sL, &sRi);
        sensNeedsRedraw = true;
        continue;
      }

      if (raw.startsWith("STAT:")) {
        tok = strtok(buf + 5, ":"); int i = 0;
        while (tok) {
          if (i==0)  botGas  = atoi(tok);
          if (i==1)  botTemp = atof(tok);
          if (i==2)  botHum  = atof(tok);
          if (i==3)  botHaz  = atoi(tok);
          if (i==8)  botVolt = atof(tok);
          if (i==9)  botPct  = constrain(atoi(tok),0,100);
          if (i==10) botAmps     = atof(tok);
          if (i==11) botBoostVolt = atof(tok);
          tok = strtok(NULL,":"); i++;
        }
        continue;
      }

      if (raw.startsWith("PWR:")) {
        tok = strtok(buf + 4, ":"); int i = 0;
        while (tok) {
          if (i==0) botVolt = atof(tok);
          if (i==1) botAmps = atof(tok);
          if (i==4) botPct  = constrain(atoi(tok),0,100);
          tok = strtok(NULL,":"); i++;
        }
        continue;
      }

    } else {
      if (megaLen + 1 < sizeof(megaBuf)) {
        megaBuf[megaLen++] = c;
      } else {
        megaLen = 0;
      }
    }
  }
}

// ══════════════════════════════════════════════════════════════════════
//  SETUP & LOOP
// ══════════════════════════════════════════════════════════════════════

void setup() {
  Serial1.begin(115200);
  randomSeed(analogRead(A5) ^ (unsigned long)millis());

  gfx->begin();
  gfx->setRotation(0);
  gfx->invertDisplay(true);
  gfx->fillScreen(C_BLACK);

  memset(logLines, 0, sizeof(logLines));

  startMs          = millis();
  sniffWindowStart = millis();
  qualWindowStart  = millis();
  pingSeq          = 0;
  pingOkCount      = 0;
  sniffTotal       = 0;
  sniffWr          = 0;
  windowBytes      = 0;

  snprintf(bsMega.faultMsg,  sizeof(bsMega.faultMsg),  "Waiting — check TX18>D0 RX19<D1");
  snprintf(bsR3.faultMsg,    sizeof(bsR3.faultMsg),    "Waiting for Mega R3 report");
  snprintf(bsS9.faultMsg,    sizeof(bsS9.faultMsg),    "Connect S9 USB or wireless ADB");
  snprintf(bsESP32.faultMsg, sizeof(bsESP32.faultMsg), "Waiting for WiFi IP from Mega");

  appState = S_STARTUP;
  drawStartupScreen();
}

void loop() {
  handleMegaLink();

  unsigned long nowMs = millis();

  // Periodic PING
  if (nowMs - lastPingSentMs >= PING_IV_MS) {
    lastPingSentMs = nowMs;
    pingSeq++;
    Serial1.print(F("PING_R4:"));
    Serial1.println(pingSeq);
  }

  // Line idle detection
  if (!bsMega.linked && sniffTotal == 0 && (nowMs - startMs) > 6000) {
    snprintf(bsMega.faultMsg, sizeof(bsMega.faultMsg), "NO RX BYTES — check wiring TX18>D0");
    if (appState == S_STARTUP) drawStartupScreen();
  }

  // Startup screen
  if (appState == S_STARTUP) {
    bool critOK = bsMega.ready && bsR3.ready;
    if (critOK && !startReady) {
      startReady = true;
      drawStartupScreen();
    }
    static unsigned long lastStartRedraw = 0;
    if (nowMs - lastStartRedraw > 2000) {
      lastStartRedraw = nowMs;
      if (!startReady) drawStartupScreen();
    }
    handleTouch();
    return;
  }

  // Celebration
  if (appState == S_CELEB) { animateCeleb(); return; }

  // Mega watchdog
  if (bsMega.ready && bsMega.lastRxMs > 0 && (nowMs - bsMega.lastRxMs > 10000)) {
    snprintf(bsMega.faultMsg, sizeof(bsMega.faultMsg), "LINK LOST — no data 10s");
    bsMega.qualPct = (bsMega.qualPct > 10) ? bsMega.qualPct - 10 : 0;
  }

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

  // Refresh sensor screen
  if (appState == S_SENSORS && (millis() - sensRefMs > 600 || sensNeedsRedraw)) {
    sensRefMs = millis();
    sensNeedsRedraw = false;
    drawUSHUD();
    char v[20];
    snprintf(v,sizeof(v),"%.1fC",botTemp);  drawEnvRow(205,"TEMPERATURE:",v,C_AMBER);
    snprintf(v,sizeof(v),"%.0f%%",botHum);  drawEnvRow(217,"HUMIDITY:   ",v,C_CYAN);
    snprintf(v,sizeof(v),"%d",botGas);       drawEnvRow(229,"GAS LEVEL:  ",v,botGas>400?C_CORAL:C_MINT);
    snprintf(v,sizeof(v),"%.1fV %d%%",botVolt,botPct); drawEnvRow(241,"BATTERY:    ",v,botPct<20?C_CORAL:C_MINT);
    snprintf(v,sizeof(v),"%s",botEstop?"E-STOP!":(botAuto?"AUTO":"MANUAL")); drawEnvRow(253,"BOT STATE:  ",v,botEstop?C_RED:C_MINT);
    snprintf(v,sizeof(v),"%.2fV",botBoostVolt); drawEnvRow(265,"BOOST OUT:  ",v,(botBoostVolt>0.5f)?C_MINT:C_MGRAY);
    drawPowerBar();
  }

  // Serial log auto-refresh
  refreshSerialLog();

  handleTouch();
}
      break;
    }

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
//  STARTUP DIAGNOSTIC SCREEN
//  Features: A1-A4, B3, B6, D1, D3, E4
// ══════════════════════════════════════════════════════════════════════

// E4: Quality colour — green > 70%, yellow > 40%, red otherwise
uint16_t qualCol(uint8_t q) {
  if (q >= 70) return C_NLIME;
  if (q >= 40) return C_NYEL;
  return G_CORAL;
}

// D1: Update fault message for Mega link based on byte/msg counts
void updateMegaFault() {
  if (bsMega.ready) {
    snprintf(bsMega.faultMsg, sizeof(bsMega.faultMsg), "Link OK  CRC_OK:%lu FAIL:%lu", crcOK, crcFail);
  } else if (sniffTotal == 0 && millis() - startMs > 5000) {
    // B6: Line idle — no bytes at all
    snprintf(bsMega.faultMsg, sizeof(bsMega.faultMsg), "NO DATA on RX — check TX18>D0");
  } else if (sniffTotal > 0 && bsMega.msgsOK == 0) {
    snprintf(bsMega.faultMsg, sizeof(bsMega.faultMsg), "Bytes rcvd, no packets — baud?");
  } else if (bsMega.msgsOK > 0 && pingOkCount < PING_LINK_THRESH) {
    snprintf(bsMega.faultMsg, sizeof(bsMega.faultMsg), "Pkts OK, ping %d/%d — check RX19<D1", pingOkCount, PING_LINK_THRESH);
  } else {
    snprintf(bsMega.faultMsg, sizeof(bsMega.faultMsg), "%u bytes  %lu msgs  %lu fail", sniffTotal, bsMega.msgsOK, bsMega.msgsFail);
  }
}

// Draw one board row (40px tall) — A3: tap zone for retry built in
// y = top of row. Returns retry button hit zone y-range as the row itself.
void drawBoardRow(int y, const char* label, BoardStat& bs, bool isMega) {
  uint16_t statusCol = bs.ready ? C_NLIME : (bs.linked ? C_NYEL : C_GRAY);
  const char* statusStr = bs.ready ? "READY" : (bs.linked ? "LINK " : "WAIT ");

  // Row background
  gfx->fillRoundRect(4, y, SCR_W-8, 38, 5,
                     bs.ready ? 0x0422 : (bs.linked ? 0x2200 : 0x0841));
  gfx->drawRoundRect(4, y, SCR_W-8, 38, 5, statusCol);

  // Status dot
  gfx->fillCircle(18, y+19, 7, statusCol);
  if (bs.ready || bs.linked) {
    gfx->setTextColor(C_BLACK); gfx->setTextSize(1);
    gfx->setCursor(bs.ready ? 13 : 14, y+15);
    gfx->print(bs.ready ? F("OK") : F("~"));
  }

  // Board name
  gfx->setTextColor(bs.ready ? C_WHITE : (bs.linked ? C_NYEL : C_LGRAY));
  gfx->setTextSize(1);
  gfx->setCursor(30, y+6);
  gfx->print(label);

  // Status badge
  gfx->setTextColor(statusCol); gfx->setTextSize(1);
  gfx->setCursor(SCR_W-52, y+6);
  gfx->print(statusStr);

  // E4: Quality bar (80px wide, 6px tall)
  int barX = 30, barY = y+22, barW = 100, barH = 6;
  gfx->drawRect(barX, barY, barW, barH, C_GRAY);
  int fillW = (bs.qualPct * (barW-2)) / 100;
  if (fillW > 0) gfx->fillRect(barX+1, barY+1, fillW, barH-2, qualCol(bs.qualPct));

  // Bytes/sec or quality %
  char info[20];
  if (isMega) {
    snprintf(info, sizeof(info), "%dB/s  Q:%d%%", bytesPerSec, bs.qualPct);
  } else {
    snprintf(info, sizeof(info), "%lu msg  Q:%d%%", bs.msgsOK, bs.qualPct);
  }
  gfx->setTextColor(C_LGRAY); gfx->setTextSize(1);
  gfx->setCursor(barX + barW + 4, y+19);
  gfx->print(info);

  // D1: Fault message (small, bottom of row)
  if (bs.faultMsg[0] && !bs.ready) {
    gfx->setTextColor(bs.linked ? C_NYEL : G_CORAL);
    gfx->setTextSize(1);
    gfx->setCursor(30, y+28);
    // Truncate to fit
    char trunc[26]; strncpy(trunc, bs.faultMsg, 25); trunc[25]='\0';
    gfx->print(trunc);
  } else if (bs.fwVer[0]) {
    gfx->setTextColor(C_DNCYAN); gfx->setTextSize(1);
    gfx->setCursor(30, y+28);
    gfx->print(bs.fwVer);
  }
}

// D3: Draw raw hex sniffer strip
void drawHexSniffer() {
  int sy = 222;
  gfx->fillRect(4, sy, SCR_W-8, 26, 0x0821);
  gfx->drawRect(4, sy, SCR_W-8, 26, C_DNCYAN);
  gfx->setTextColor(C_DNCYAN); gfx->setTextSize(1);
  gfx->setCursor(8, sy+3);
  gfx->print(F("RX SNIFFER:"));

  gfx->setTextColor(sniffTotal > 0 ? C_NCYAN : C_GRAY);
  gfx->setCursor(8, sy+14);
  if (sniffTotal == 0) {
    gfx->print(F("-- -- -- -- -- -- -- -- -- -- -- --"));
  } else {
    char hex[4];
    for (int i = 0; i < SNIFF_N; i++) {
      int idx = (sniffWr - SNIFF_N + i + SNIFF_N) % SNIFF_N;
      sprintf(hex, "%02X ", sniffBuf[idx]);
      gfx->print(hex);
    }
  }
}

// Main startup screen draw — called on every state change
void drawStartupScreen() {
  gfx->fillScreen(C_BLACK);

  // ── Animated header bar ────────────────────────────────────────────
  // Colour cycles through the palette each redraw for a "live" feel
  static uint8_t hdrAnim = 0; hdrAnim = (hdrAnim + 1) % HDR_PAL_N;
  uint16_t hdrC = HDR_PAL[hdrAnim];
  gfx->fillRect(0, 0, SCR_W, 38, C_DKBG);
  // Neon corner brackets
  gfx->drawFastHLine(0,  0,  16, hdrC);
  gfx->drawFastVLine(0,  0,  10, hdrC);
  gfx->drawFastHLine(SCR_W-16, 0, 16, hdrC);
  gfx->drawFastVLine(SCR_W-1,  0, 10, hdrC);
  // Title
  gfx->setTextColor(hdrC); gfx->setTextSize(2);
  gfx->setCursor(12, 4);
  gfx->print(F("BUDDYBOT DIAG"));
  // Animated "scanning" dots (3 dots, cycling)
  static uint8_t dotAnim = 0; dotAnim = (dotAnim + 1) % 4;
  gfx->setTextColor(C_GRAY); gfx->setTextSize(1);
  gfx->setCursor(12, 26);
  gfx->print(F("System startup check"));
  gfx->setTextColor(hdrC);
  gfx->setCursor(136, 26);
  for (int d = 0; d < 3; d++) gfx->print(d < (int)dotAnim ? '.' : ' ');
  gfx->drawFastHLine(0, 36, SCR_W, hdrC);
  gfx->drawFastHLine(0, 37, SCR_W, C_DNCYAN);

  // A4: Board rows (y positions: 40, 82, 124, 166)
  updateMegaFault();
  drawBoardRow(40,  "MEGA V29.0",    bsMega,  true);
  drawBoardRow(82,  "R3 MOTORS",     bsR3,    false);
  drawBoardRow(124, "ANDROID S9",    bsS9,    false);
  drawBoardRow(166, "ESP32 WIFI",    bsESP32, false);

  // D3: Hex sniffer
  drawHexSniffer();

  // WiFi IP line
  gfx->setTextColor(bsESP32.ready ? C_NLIME : C_GRAY);
  gfx->setTextSize(1);
  gfx->setCursor(8, 252);
  gfx->print(F("WiFi IP: "));
  gfx->print(bsESP32.fwVer[0] ? bsESP32.fwVer : (bsESP32.linked ? "BT only" : "Waiting..."));

  // A1: Bottom button — TAP START only appears when ready
  // A2: SKIP appears after START_SKIP_DELAY
  unsigned long elapsed = millis() - startMs;

  if (startReady) {
    // All systems go — green TAP START
    gfx->fillRoundRect(20, 260, SCR_W-40, 34, 8, 0x0440);
    gfx->drawRoundRect(20, 260, SCR_W-40, 34, 8, C_NLIME);
    gfx->drawRoundRect(21, 261, SCR_W-42, 32, 7, C_NLIME);
    gfx->setTextColor(C_NLIME); gfx->setTextSize(2);
    gfx->setCursor(38, 271);
    gfx->print(F("TAP TO START"));
  } else if (elapsed > START_SKIP_DELAY) {
    // A2: SKIP button — red, requires double-tap
    uint16_t skipCol = skipConfirm ? G_RED : C_NORG;
    gfx->fillRoundRect(20, 260, SCR_W-40, 34, 8, 0x2000);
    gfx->drawRoundRect(20, 260, SCR_W-40, 34, 8, skipCol);
    gfx->setTextColor(skipCol); gfx->setTextSize(1);
    gfx->setCursor(26, 266);
    gfx->print(skipConfirm ? F("TAP AGAIN TO FORCE START") : F("SKIP — some boards not ready"));
    gfx->setCursor(26, 280);
    char tbuf[24];
    snprintf(tbuf, sizeof(tbuf), "Elapsed: %lus", elapsed/1000);
    gfx->print(tbuf);
  } else {
    // Waiting — grey panel with countdown
    unsigned long remaining = (START_SKIP_DELAY - elapsed) / 1000;
    gfx->fillRoundRect(20, 260, SCR_W-40, 34, 8, 0x0821);
    gfx->drawRoundRect(20, 260, SCR_W-40, 34, 8, C_GRAY);
    gfx->setTextColor(C_GRAY); gfx->setTextSize(1);
    gfx->setCursor(36, 266);
    gfx->print(F("Waiting for boards..."));
    gfx->setCursor(36, 278);
    char tbuf[28];
    snprintf(tbuf, sizeof(tbuf), "SKIP available in %lus", remaining);
    gfx->print(tbuf);
  }

  drawPowerBar();
}

// Transition out of startup — smooth cinematic sequence → splash → main menu
void proceedFromStartup() {
  // ── Phase 1: Horizontal wipe — green bars sweep in from both edges ──
  for (int x = 0; x < SCR_W / 2 + 2; x += 6) {
    gfx->fillRect(0,         0, x,       SCR_H, 0x0440);
    gfx->fillRect(SCR_W - x, 0, x + 2,  SCR_H, 0x0440);
    delay(4);
  }

  // ── Phase 2: SYSTEM ONLINE text slam ────────────────────────────────
  gfx->fillScreen(0x0440);   // solid dark green
  // Shadow
  gfx->setTextColor(0x0220); gfx->setTextSize(3);
  gfx->setCursor(22, 122); gfx->print(F("SYSTEM ONLINE"));
  // Main
  gfx->setTextColor(C_NLIME); gfx->setTextSize(3);
  gfx->setCursor(20, 120); gfx->print(F("SYSTEM ONLINE"));
  delay(180);

  // ── Phase 3: Scanline collapse — lines race toward centre ───────────
  int top = 0, bot = SCR_H - 1;
  while (top < bot) {
    gfx->drawFastHLine(0, top, SCR_W, C_NLIME);
    gfx->drawFastHLine(0, bot, SCR_W, C_NLIME);
    top += 4; bot -= 4;
    delay(3);
  }
  // Bright centre flash
  gfx->drawFastHLine(0, SCR_H / 2, SCR_W, C_WHITE);
  gfx->drawFastHLine(0, SCR_H/2-1, SCR_W, C_WHITE);
  delay(40);
  gfx->fillScreen(C_BLACK);
  delay(60);

  doSplash();       // matrix rain + BUDDYBOT title sequence
  drawMainMenu();
}

// ══════════════════════════════════════════════════════════════════════
//  MEGA LINK — Telemetry Parser
// ══════════════════════════════════════════════════════════════════════

void handleMegaLink() {
  static char megaBuf[120];
  static uint8_t megaLen = 0;

  // ── B3 + D3: Track bytes-per-second window ──────────────────────────
  unsigned long nowMs = millis();
  if (nowMs - sniffWindowStart >= 1000) {
    bytesPerSec       = (uint8_t)min(windowBytes, (uint32_t)255);
    windowBytes       = 0;
    sniffWindowStart  = nowMs;
    // E4: Update Mega quality (expect ~10 msgs/10s = ~1/s; window msgs/expected)
    // Simple decay: if bytes arrived, quality climbs; if not, it drops
    if (bytesPerSec > 0) {
      if (bsMega.qualPct < 95) bsMega.qualPct += 5;
    } else {
      if (bsMega.qualPct > 5) bsMega.qualPct -= 5;
      else bsMega.qualPct = 0;
    }
  }

  while (Serial1.available()) {
    char c = Serial1.read();

    // B3 + D3: Byte accounting and sniffer
    sniffTotal++;
    windowBytes++;
    bsMega.bytesRx++;
    bsMega.lastRxMs = millis();
    bsMega.linked   = true;
    sniffBuf[sniffWr] = (uint8_t)c;
    sniffWr = (sniffWr + 1) % SNIFF_N;

    if (c == '\r') continue;
    if (c == '\n') {
      if (megaLen == 0) continue;
      megaBuf[megaLen] = '\0';
      String raw = String(megaBuf);
      raw.trim();
      megaLen = 0;
      if (raw.length() == 0) continue;

      // ── E2: CRC check — strip |CRC:XX suffix if present ────────────
      bool crcValid = true;
      int crcIdx = raw.lastIndexOf("|CRC:");
      if (crcIdx > 0) {
        String crcHex = raw.substring(crcIdx + 5);
        String payload = raw.substring(0, crcIdx);
        uint8_t expected = (uint8_t)strtoul(crcHex.c_str(), nullptr, 16);
        uint8_t actual   = r4CalcCRC(payload.c_str(), payload.length());
        if (expected == actual) {
          crcOK++; raw = payload;   // strip CRC suffix, use payload
        } else {
          crcFail++;
          bsMega.msgsFail++;
          crcValid = false;
          // Still try to parse — CRC mismatch may just be old firmware
          raw = payload;
        }
      }

      bsMega.msgsOK++;

      // ── B1: Pong handler ─────────────────────────────────────────
      if (raw.startsWith("PONG_R4:")) {
        uint16_t seq = (uint16_t)raw.substring(8).toInt();  // FIX: match pingSeq type
        if (seq == pingSeq) {
          lastPongRxMs = millis();
          if (pingOkCount < PING_LINK_THRESH) pingOkCount++;
          if (pingOkCount >= PING_LINK_THRESH && !bsMega.ready) {
            bsMega.ready = true;
            if (appState == S_STARTUP) drawStartupScreen();
          }
        }
        continue;
      }

      // ── Log all messages EXCEPT US: (too noisy) ─────────────────
      if (!raw.startsWith("US:")) {
        char tag[6] = "MEGA";
        if      (raw.startsWith("STAT"))     strcpy(tag,"STAT");
        else if (raw.startsWith("PWR"))      strcpy(tag,"PWR");
        else if (raw.startsWith("STATUS"))   strcpy(tag,"STAT");
        else if (raw.startsWith("MODE"))     strcpy(tag,"MODE");
        else if (raw.startsWith("GESTURE"))  strcpy(tag,"GEST");
        else if (raw.startsWith("BAT:"))     strcpy(tag,"BAT");
        else if (raw.startsWith("SAFETY:"))  strcpy(tag,"SAFE");
        else if (raw.startsWith("SENS_ST|")) strcpy(tag,"SENS");
        else if (raw.startsWith("MEGA_READY")) strcpy(tag,"BOOT");
        else if (raw.startsWith("CONN_ST"))  strcpy(tag,"CONN");
        addLog(tag, raw.c_str());
      }

      char buf[120];
      raw.toCharArray(buf, sizeof(buf));
      char *tok;

      // ── E3/B1: MEGA_READY — firmware version confirmed ────────────
      if (raw.startsWith("MEGA_READY|")) {
        bsMega.linked = true;
        int fwIdx = raw.indexOf("FW:");
        if (fwIdx > 0) {
          String fwStr = raw.substring(fwIdx+3);
          int endIdx = fwStr.indexOf('|');
          if (endIdx > 0) fwStr = fwStr.substring(0, endIdx);
          fwStr.toCharArray(bsMega.fwVer, sizeof(bsMega.fwVer));
        }
        if (appState == S_STARTUP) drawStartupScreen();
        continue;
      }

      // ── CONN_STATUS — R3 and overall status from Mega ─────────────
      if (raw.startsWith("CONN_STATUS|")) {
        bsMega.linked = true;
        bsR3.linked   = (raw.indexOf("R3:OK") >= 0);
        bsR3.ready    = bsR3.linked;
        bsR3.qualPct  = bsR3.ready ? 100 : 0;
        if (bsR3.ready) {
          snprintf(bsR3.faultMsg, sizeof(bsR3.faultMsg), "Motor slave ACK confirmed");
        } else {
          snprintf(bsR3.faultMsg, sizeof(bsR3.faultMsg), "No ACK — check SS pins 10/11");
        }
        // Extract Mega FW version from CONN_STATUS
        int fwIdx = raw.indexOf("FW:");
        if (fwIdx > 0) {
          String fwStr = raw.substring(fwIdx+3);
          int endIdx = fwStr.indexOf('|');
          if (endIdx > 0) fwStr = fwStr.substring(0, endIdx);
          fwStr.toCharArray(bsMega.fwVer, sizeof(bsMega.fwVer));
        }
        if (appState == S_STARTUP) drawStartupScreen();
        continue;
      }

      // ── WIFI_IP — ESP32 IP forwarded by Mega ─────────────────────
      if (raw.startsWith("WIFI_IP:")) {
        String ip = raw.substring(8);
        ip.trim();
        if (ip == "BT_ONLY") {
          snprintf(bsESP32.fwVer, sizeof(bsESP32.fwVer), "BT-only mode");
          snprintf(bsESP32.faultMsg, sizeof(bsESP32.faultMsg), "WiFi failed — BT available");
          bsESP32.linked = true;
          bsESP32.ready  = true;  // BT-only counts as ready
          bsESP32.qualPct = 60;
        } else {
          ip.toCharArray(bsESP32.fwVer, sizeof(bsESP32.fwVer));
          bsESP32.linked  = true;
          bsESP32.ready   = true;
          bsESP32.qualPct = 100;
          snprintf(bsESP32.faultMsg, sizeof(bsESP32.faultMsg), "WiFi connected");
        }
        if (appState == S_STARTUP) drawStartupScreen();
        continue;
      }

      // ── STATUS| — enriched with R3/ESP/S9 status ─────────────────
      if (raw.startsWith("STATUS|")) {
        botEstop = (raw.indexOf("ESTOP:YES") >= 0);
        botAuto  = (raw.indexOf("AUTO:ON")   >= 0);
        // R3 status from STATUS| packet
        if (raw.indexOf("R3:OK") >= 0)   { bsR3.linked=true; bsR3.ready=true; bsR3.qualPct=100; }
        if (raw.indexOf("R3:FAIL") >= 0) { bsR3.ready=false; }
        // ESP32 status
        if (raw.indexOf("ESP:OK") >= 0)  { bsESP32.linked=true; if(!bsESP32.ready) bsESP32.qualPct=80; }
        // S9 status
        if (raw.indexOf("S9:OK") >= 0)   { bsS9.linked=true; bsS9.ready=true; bsS9.qualPct=100;
          snprintf(bsS9.faultMsg, sizeof(bsS9.faultMsg), "Android app connected"); }
        else { if(!bsS9.ready) snprintf(bsS9.faultMsg, sizeof(bsS9.faultMsg), "No S9 — use wireless ADB"); }
        int vi = raw.indexOf("BAT:");
        if (vi >= 0) botVolt = raw.substring(vi+4).toFloat();
        int pi = raw.indexOf("PCT:");
        if (pi >= 0) botPct  = constrain(raw.substring(pi+4).toInt(),0,100);
        if (appState == S_STARTUP) drawStartupScreen();
        continue;
      }

      // ── SYSTEM|READY — old-style fallback ─────────────────────────
      if (raw.startsWith("SYSTEM|READY")) {
        bsMega.linked = true;
        if (appState == S_STARTUP) drawStartupScreen();
        continue;
      }

      // ── MODE ──────────────────────────────────────────────────────
      if (raw.startsWith("MODE:")) {
        String mode = raw.substring(5);
        if      (mode == "NORMAL")    botMode = 0;
        else if (mode == "BODYGUARD") botMode = 1;
        else if (mode == "GUARD DOG" || mode == "DOG") botMode = 2;
        continue;
      }

      // ── BAT ───────────────────────────────────────────────────────
      if (raw.startsWith("BAT:")) {
        alertBatWarn = alertBatLow = alertBatCritical = false;
        if (raw == "BAT:WARN") {
          alertBatWarn = true;
          snprintf(alertText, sizeof(alertText), "Battery warning");
          showAlert(SEV_INFO, "BAT WARNING", "Battery below optimal. Consider charging soon.", true);
        } else if (raw == "BAT:LOW") {
          alertBatLow = true;
          snprintf(alertText, sizeof(alertText), "Battery low");
          showAlert(SEV_WARN, "BAT LOW", "Battery is running low. Speed reduced. Charge when possible.", true);
        } else if (raw == "BAT:CRITICAL") {
          alertBatCritical = true;
          snprintf(alertText, sizeof(alertText), "Battery critical");
          showAlert(SEV_CRITICAL, "BAT CRITICAL", "Battery critically low. BuddyBot has stopped. Charge immediately.", false);
        }
        continue;
      }

      // ── SAFETY ────────────────────────────────────────────────────
      if (raw.startsWith("SAFETY:")) {
        alertTilt = alertFlame = alertGas = false;
        if (raw == "SAFETY:TILT") {
          alertTilt = true;
          snprintf(alertText, sizeof(alertText), "Tilt detected");
          showAlert(SEV_WARN, "TILT ALERT", "BuddyBot has been tilted or knocked over. Motors stopped.", true);
        } else if (raw == "SAFETY:FLAME_ALERT") {
          alertFlame = true;
          snprintf(alertText, sizeof(alertText), "Flame alert!");
          showAlert(SEV_CRITICAL, "FLAME ALERT", "Flame detected nearby! Check surroundings immediately.", false);
        } else if (raw == "SAFETY:GAS_ALERT") {
          alertGas = true;
          snprintf(alertText, sizeof(alertText), "Gas alert!");
          showAlert(SEV_WARN, "GAS ALERT", "Gas sensor triggered. Ensure adequate ventilation.", true);
        }
        continue;
      }

      // ── GESTURE ───────────────────────────────────────────────────
      if (raw.startsWith("GESTURE:")) { continue; }

      // ── SENS_ST| ──────────────────────────────────────────────────
      if (raw.startsWith("SENS_ST|")) {
        int end2 = raw.indexOf("|END");
        lastSensStatus = (end2 > 0) ? raw.substring(8, end2) : raw.substring(8);
        sensNeedsRedraw = true;
        continue;
      }

      // ── US: front,rear,left,right ─────────────────────────────────
      if (raw.startsWith("US:")) {
        sscanf(buf + 3, "%d,%d,%d,%d", &sF, &sR, &sL, &sRi);
        sensNeedsRedraw = true;
        continue;
      }

      // ── STAT:gas:temp:hum:haz:pir:tilt:flame:ir:volt:pct:amps ────
      if (raw.startsWith("STAT:")) {
        tok = strtok(buf + 5, ":"); int i = 0;
        while (tok) {
          if (i==0)  botGas  = atoi(tok);
          if (i==1)  botTemp = atof(tok);
          if (i==2)  botHum  = atof(tok);
          if (i==3)  botHaz  = atoi(tok);
          if (i==8)  botVolt = atof(tok);
          if (i==9)  botPct  = constrain(atoi(tok),0,100);
          if (i==10) botAmps     = atof(tok);
          if (i==11) botBoostVolt = atof(tok);   // boost converter output V
          tok = strtok(NULL,":"); i++;
        }
        continue;
      }

      // ── PWR:volt:amps:x:x:pct ────────────────────────────────────
      if (raw.startsWith("PWR:")) {
        tok = strtok(buf + 4, ":"); int i = 0;
        while (tok) {
          if (i==0) botVolt = atof(tok);
          if (i==1) botAmps = atof(tok);
          if (i==4) botPct  = constrain(atoi(tok),0,100);
          tok = strtok(NULL,":"); i++;
        }
        continue;
      }

    } else {
      if (megaLen + 1 < sizeof(megaBuf)) {
        megaBuf[megaLen++] = c;
      } else {
        megaLen = 0;  // overflow guard
      }
    }
  }
}


void setup() {
  Serial1.begin(115200);
  randomSeed(analogRead(A5) ^ (unsigned long)millis());

  gfx->begin();
  gfx->setRotation(0);
  gfx->invertDisplay(true);
  gfx->fillScreen(C_BLACK);

  memset(logLines, 0, sizeof(logLines));

  // Initialise startup tracking
  startMs          = millis();
  sniffWindowStart = millis();
  qualWindowStart  = millis();
  pingSeq          = 0;
  pingOkCount      = 0;
  sniffTotal       = 0;
  sniffWr          = 0;
  windowBytes      = 0;

  // D1: Pre-fill fault messages for boards that haven't spoken yet
  snprintf(bsMega.faultMsg,  sizeof(bsMega.faultMsg),  "Waiting — check TX18>D0 RX19<D1");
  snprintf(bsR3.faultMsg,    sizeof(bsR3.faultMsg),    "Waiting for Mega R3 report");
  snprintf(bsS9.faultMsg,    sizeof(bsS9.faultMsg),    "Connect S9 USB or wireless ADB");
  snprintf(bsESP32.faultMsg, sizeof(bsESP32.faultMsg), "Waiting for WiFi IP from Mega");

  appState = S_STARTUP;
  drawStartupScreen();   // show diagnostic screen — no splash until boards ready
}

// ══════════════════════════════════════════════════════════════════════
//  LOOP
// ══════════════════════════════════════════════════════════════════════

void loop() {
  handleMegaLink();

  unsigned long nowMs = millis();

  // ── B1: Send periodic PING to Mega ───────────────────────────────
  if (nowMs - lastPingSentMs >= PING_IV_MS) {
    lastPingSentMs = nowMs;
    pingSeq++;
    Serial1.print(F("PING_R4:"));
    Serial1.println(pingSeq);
  }

  // ── B6: Line idle detection — update fault if no bytes ───────────
  if (!bsMega.linked && sniffTotal == 0 && (nowMs - startMs) > 6000) {
    snprintf(bsMega.faultMsg, sizeof(bsMega.faultMsg), "NO RX BYTES — check wiring TX18>D0");
    if (appState == S_STARTUP) drawStartupScreen();
  }

  // ── STARTUP SCREEN — owns loop until boards ready + tapped ───────
  if (appState == S_STARTUP) {
    // Check if critical boards confirmed
    bool critOK = bsMega.ready && bsR3.ready;
    if (critOK && !startReady) {
      startReady = true;
      drawStartupScreen();   // redraw with TAP START button
    }

    // Periodic redraw during startup (every 2s) to update sniffer + quality
    static unsigned long lastStartRedraw = 0;
    if (nowMs - lastStartRedraw > 2000) {
      lastStartRedraw = nowMs;
      if (!startReady) drawStartupScreen();   // live update while waiting
    }

    handleTouch();
    return;
  }

  // ── Celebration owns the loop until done ─────────────────────────
  if (appState == S_CELEB) { animateCeleb(); return; }

  // ── E1: Ongoing Mega link watchdog ───────────────────────────────
  if (bsMega.ready && bsMega.lastRxMs > 0 && (nowMs - bsMega.lastRxMs > 10000)) {
    // Connection lost — mark degraded but don't panic
    snprintf(bsMega.faultMsg, sizeof(bsMega.faultMsg), "LINK LOST — no data 10s");
    bsMega.qualPct = (bsMega.qualPct > 10) ? bsMega.qualPct - 10 : 0;
  }

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

  // Refresh live sensor screen every 600ms OR when new data arrives
  if (appState == S_SENSORS && (millis() - sensRefMs > 600 || sensNeedsRedraw)) {
    sensRefMs = millis();
    sensNeedsRedraw = false;
    drawUSHUD();
    char v[16];
    snprintf(v,sizeof(v),"%.1f C",botTemp); drawEnvRow(205,"TEMPERATURE:",v,C_NYEL);
    snprintf(v,sizeof(v),"%.0f %%",botHum); drawEnvRow(218,"HUMIDITY:   ",v,C_NCYAN);
    snprintf(v,sizeof(v),"%d",botGas);       drawEnvRow(231,"GAS LEVEL:  ",v,botGas>400?G_CORAL:C_NLIME);
    snprintf(v,sizeof(v),"%.1fV %d%%",botVolt,botPct); drawEnvRow(244,"BATTERY:    ",v,botPct<20?G_CORAL:C_NLIME);
    snprintf(v,sizeof(v),"%s",botEstop?"E-STOP!":(botAuto?"AUTO":"MANUAL")); drawEnvRow(257,"BOT STATE:  ",v,botEstop?G_RED:C_NLIME);
    snprintf(v,sizeof(v),"%.2fV",botBoostVolt); drawEnvRow(270,"BOOST OUT:  ",v,(botBoostVolt>0.5f)?C_NLIME:C_GRAY);
    drawPowerBar();
  }

  // Auto-refresh serial comms log when new data
  refreshSerialLog();

  handleTouch();
}
