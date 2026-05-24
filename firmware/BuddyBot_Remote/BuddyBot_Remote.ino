/*
 * ════════════════════════════════════════════════════════════════════
 *  BUDDYBOT  —  Remote Control  ·  SAMD21 WiFi  ·  V1.0
 * ════════════════════════════════════════════════════════════════════
 *
 *  HARDWARE
 *  ─────────────────────────────────────────────────────────────────
 *  MCU     : SAMD21 with WiFi (e.g. MKR WiFi 1010 or Nano 33 IoT)
 *  Display : 1.5" colour OLED  128×128  (SSD1351, SPI)
 *  Encoder : XC3736 incremental rotary encoder (CLK/DT/SW)
 *  RF TX   : 433 MHz transmitter module (DATA pin)
 *  Power   : 173040 3.7V LiPo via onboard regulator
 *
 *  WIRING
 *  ─────────────────────────────────────────────────────────────────
 *  OLED (SPI):
 *    MOSI  → D11 (SPI MOSI)
 *    SCK   → D13 (SPI SCK)
 *    CS    → D10
 *    DC    → D9
 *    RST   → D8
 *    VCC   → 3.3V   GND → GND
 *
 *  Rotary Encoder (XC3736):
 *    CLK   → D2   (interrupt-capable)
 *    DT    → D3   (interrupt-capable)
 *    SW    → D4   (button press)
 *    +     → 3.3V   GND → GND
 *
 *  RF Transmitter:
 *    DATA  → D6
 *    VCC   → 5V (or 3.3V depending on module) GND → GND
 *
 *  LIBRARIES REQUIRED (Arduino Library Manager)
 *  ─────────────────────────────────────────────────────────────────
 *  1. Adafruit SSD1351 (for OLED display)
 *  2. Adafruit GFX
 *  3. RCSwitch  (for 433 MHz RF transmission)
 *
 * ════════════════════════════════════════════════════════════════════
 */

#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1351.h>
#include <RCSwitch.h>

// ── Pin assignments ───────────────────────────────────────────────
#define OLED_CS   10
#define OLED_DC    9
#define OLED_RST   8

#define ENC_CLK    2   // interrupt-capable
#define ENC_DT     3   // interrupt-capable
#define ENC_SW     4   // button

#define RF_PIN     6   // RF transmitter DATA

// ── Display dimensions ────────────────────────────────────────────
#define OLED_W  128
#define OLED_H  128

// ── RF codes (must match robot's RCSwitch receiver) ───────────────
// Using 24-bit codes — protocol 1 — 189us pulse width
#define RF_FORWARD  0xA10001UL
#define RF_BACKWARD 0xA10002UL
#define RF_LEFT     0xA10003UL
#define RF_RIGHT    0xA10004UL
#define RF_STOP     0xA10005UL
#define RF_SPEED_UP 0xA10006UL
#define RF_SPEED_DN 0xA10007UL
#define RF_MODE_NRM 0xA10010UL
#define RF_MODE_BOD 0xA10011UL
#define RF_MODE_DOG 0xA10012UL
#define RF_MODE_UNH 0xA10013UL
#define RF_ESTOP    0xA100FFul
#define RF_DANCE    0xA10020UL
#define RF_DEFENSE  0xA10021UL

// ── Colour palette (RGB565) ───────────────────────────────────────
#define C_BG      0x0000
#define C_SURF    0x0821
#define C_CYAN    0x07FF
#define C_MINT    0x07E4
#define C_AMBER   0xFD20
#define C_CORAL   0xF944
#define C_PURPLE  0x781F
#define C_WHITE   0xFFFF
#define C_LGRAY   0x8C71
#define C_MGRAY   0x4228
#define C_DGRAY   0x2104
#define C_RED     0xF800
#define C_GREEN   0x07E0
#define C_BLUE    0x001F

// ── Objects ───────────────────────────────────────────────────────
Adafruit_SSD1351 oled(OLED_W, OLED_H, &SPI, OLED_CS, OLED_DC, OLED_RST);
RCSwitch         rfSwitch;

// ── State ─────────────────────────────────────────────────────────
enum RemoteState { ST_DRIVE, ST_MODE, ST_SPECIAL, ST_SPEED };
RemoteState state     = ST_DRIVE;
int8_t      encPos    = 0;
int8_t      lastPos   = 0;
bool        btnPress  = false;
bool        longPress = false;

// Speed: 0=SLOW 1=NORMAL 2=FAST
uint8_t speedLevel = 1;
const char* speedNames[] = { "SLOW", "NORMAL", "FAST" };
const uint32_t speedCodes[] = { RF_SPEED_DN, RF_FORWARD, RF_SPEED_UP };

// Mode selection
uint8_t modeIdx = 0;
const char* modeNames[]  = { "NORMAL", "BODYGUARD", "DOG GUARD", "UNHINGED" };
const uint32_t modeCodes[] = { RF_MODE_NRM, RF_MODE_BOD, RF_MODE_DOG, RF_MODE_UNH };

// Drive direction (encoder position → direction)
// Encoder click 0 = STOP, rotating CW = FWD/RIGHT, CCW = BACK/LEFT
int8_t driveDir = 0;  // -2=back-left, -1=back, 0=stop, 1=forward, 2=forward-right

// Current transmit code
uint32_t lastCode = 0;

// Button timing
unsigned long btnDownMs   = 0;
bool          btnWasDown  = false;
#define LONG_PRESS_MS 600

// Display refresh
unsigned long lastDraw    = 0;
#define DRAW_INTERVAL_MS  80

// Battery (ADC on A0 via voltage divider)
#define BAT_PIN   A0
float batVoltage  = 3.7f;
uint8_t batPct    = 100;
unsigned long lastBatRead = 0;

// ── Encoder ISR ───────────────────────────────────────────────────
volatile int8_t encDelta = 0;
volatile uint8_t lastEncState = 0;

void IRAM_ATTR encoderISR() {
  uint8_t clk = digitalRead(ENC_CLK);
  uint8_t dt  = digitalRead(ENC_DT);
  uint8_t encState = (clk << 1) | dt;
  if ((lastEncState == 0b00 && encState == 0b01) ||
      (lastEncState == 0b01 && encState == 0b11) ||
      (lastEncState == 0b11 && encState == 0b10) ||
      (lastEncState == 0b10 && encState == 0b00)) {
    encDelta++;
  } else if ((lastEncState == 0b00 && encState == 0b10) ||
             (lastEncState == 0b10 && encState == 0b11) ||
             (lastEncState == 0b11 && encState == 0b01) ||
             (lastEncState == 0b01 && encState == 0b00)) {
    encDelta--;
  }
  lastEncState = encState;
}

// ════════════════════════════════════════════════════════════════════
//  DISPLAY HELPERS
// ════════════════════════════════════════════════════════════════════

void drawBg() {
  oled.fillScreen(C_BG);
  // Subtle grid
  for (int x = 0; x < OLED_W; x += 8)
    oled.drawFastVLine(x, 0, OLED_H, 0x0421);
  for (int y = 0; y < OLED_H; y += 8)
    oled.drawFastHLine(0, y, OLED_W, 0x0421);
}

void neonRect(int x, int y, int w, int h, uint16_t col) {
  oled.fillRect(x, y, w, h, C_SURF);
  oled.drawRect(x, y, w, h, col);
  oled.drawFastHLine(x + 2, y, w - 4, col);   // top accent
}

void drawLabel(int x, int y, const char* txt, uint16_t col) {
  oled.setTextSize(1);
  oled.setTextColor(col);
  oled.setCursor(x, y);
  oled.print(txt);
}

void drawLabelCentered(int y, const char* txt, uint16_t col, uint8_t sz = 1) {
  oled.setTextSize(sz);
  oled.setTextColor(col);
  int16_t tw = strlen(txt) * 6 * sz;
  oled.setCursor((OLED_W - tw) / 2, y);
  oled.print(txt);
}

void drawPill(int x, int y, int w, int h, uint16_t bg, uint16_t fg, const char* txt) {
  oled.fillRoundRect(x, y, w, h, h / 2, bg);
  oled.drawRoundRect(x, y, w, h, h / 2, fg);
  oled.setTextSize(1);
  oled.setTextColor(fg);
  int16_t tw = strlen(txt) * 6;
  oled.setCursor(x + (w - tw) / 2, y + (h - 8) / 2);
  oled.print(txt);
}

void drawBattery(int x, int y) {
  uint16_t col = batPct > 50 ? C_MINT : batPct > 20 ? C_AMBER : C_CORAL;
  oled.drawRect(x, y, 20, 10, col);
  oled.fillRect(x + 20, y + 3, 2, 4, col);   // terminal
  int fill = (int)(batPct / 100.0f * 18.0f);
  if (fill > 0) oled.fillRect(x + 1, y + 1, fill, 8, col);
  oled.setTextSize(1);
  oled.setTextColor(col);
  char buf[6]; snprintf(buf, sizeof(buf), "%d%%", batPct);
  oled.setCursor(x + 24, y + 1);
  oled.print(buf);
}

// ════════════════════════════════════════════════════════════════════
//  DRIVE DIRECTION ARROW
// ════════════════════════════════════════════════════════════════════
void drawArrow(int cx, int cy, int dir, uint16_t col) {
  // dir: 0=stop, 1=up, 2=down, 3=left, 4=right, 5=ul, 6=ur, 7=dl, 8=dr
  const int R = 14;
  oled.fillCircle(cx, cy, R + 2, C_SURF);
  oled.drawCircle(cx, cy, R + 2, col);
  if (dir == 0) {
    // Stop square
    oled.fillRect(cx - 5, cy - 5, 10, 10, col);
    return;
  }
  // Arrow tip direction
  int dx = 0, dy = 0;
  if (dir == 1) dy = -1;
  else if (dir == 2) dy = 1;
  else if (dir == 3) dx = -1;
  else if (dir == 4) dx = 1;
  else if (dir == 5) { dx = -1; dy = -1; }
  else if (dir == 6) { dx =  1; dy = -1; }
  else if (dir == 7) { dx = -1; dy =  1; }
  else if (dir == 8) { dx =  1; dy =  1; }
  int tx = cx + dx * R, ty = cy + dy * R;
  // Arrowhead triangle
  int px = -dy, py = dx;   // perpendicular
  oled.fillTriangle(tx, ty,
                    cx + px * 5, cy + py * 5,
                    cx - px * 5, cy - py * 5,
                    col);
  // Shaft
  oled.drawLine(cx, cy, tx - dx * 4, ty - dy * 4, col);
}

// ════════════════════════════════════════════════════════════════════
//  SCREEN RENDERERS
// ════════════════════════════════════════════════════════════════════

void drawDriveScreen() {
  drawBg();

  // Header
  oled.fillRect(0, 0, OLED_W, 16, C_SURF);
  oled.drawFastHLine(0, 16, OLED_W, C_CYAN);
  drawLabelCentered(4, "BUDDYBOT RC", C_CYAN);
  drawBattery(OLED_W - 48, 3);

  // Direction arrow (centre)
  int arrowDir = 0;
  uint32_t sendCode = RF_STOP;
  if      (driveDir ==  2) { arrowDir = 1; sendCode = RF_FORWARD;  }
  else if (driveDir == -2) { arrowDir = 2; sendCode = RF_BACKWARD; }
  else if (driveDir == -1) { arrowDir = 3; sendCode = RF_LEFT;     }
  else if (driveDir ==  1) { arrowDir = 4; sendCode = RF_RIGHT;    }
  else                     { arrowDir = 0; sendCode = RF_STOP;     }

  uint16_t arrowCol = driveDir == 0 ? C_CORAL :
                      (abs(driveDir) == 2 ? C_CYAN : C_AMBER);
  drawArrow(64, 64, arrowDir, arrowCol);

  // Direction label
  const char* dirTxt = "STOP";
  if (driveDir ==  2) dirTxt = "FORWARD";
  if (driveDir == -2) dirTxt = "REVERSE";
  if (driveDir == -1) dirTxt = "LEFT";
  if (driveDir ==  1) dirTxt = "RIGHT";
  drawLabelCentered(88, dirTxt, arrowCol);

  // Speed pill
  uint16_t spCol = speedLevel == 0 ? C_MINT : speedLevel == 1 ? C_CYAN : C_CORAL;
  char spBuf[14]; snprintf(spBuf, sizeof(spBuf), "SPD: %s", speedNames[speedLevel]);
  drawPill(4, 100, 70, 14, C_SURF, spCol, spBuf);

  // Mode pill
  drawPill(78, 100, 46, 14, C_SURF, C_PURPLE, modeNames[modeIdx]);

  // Bottom hint
  drawLabelCentered(116, "TURN=steer PRESS=menu", C_DGRAY);

  // Send RF if changed
  if (sendCode != lastCode) {
    rfSwitch.send(sendCode, 24);
    lastCode = sendCode;
  }
}

void drawModeScreen() {
  drawBg();
  oled.fillRect(0, 0, OLED_W, 16, C_SURF);
  oled.drawFastHLine(0, 16, OLED_W, C_PURPLE);
  drawLabelCentered(4, "SELECT MODE", C_PURPLE);

  const uint16_t cols[] = { C_CYAN, C_AMBER, C_MINT, C_CORAL };
  for (int i = 0; i < 4; i++) {
    bool sel = (i == modeIdx);
    int y = 22 + i * 22;
    uint16_t bg = sel ? cols[i] : C_SURF;
    uint16_t fg = sel ? C_BG    : cols[i];
    oled.fillRoundRect(8, y, 112, 18, 4, bg);
    oled.drawRoundRect(8, y, 112, 18, 4, cols[i]);
    if (sel) oled.drawFastHLine(12, y, 104, C_WHITE);
    oled.setTextSize(1); oled.setTextColor(fg);
    int tw = strlen(modeNames[i]) * 6;
    oled.setCursor((OLED_W - tw) / 2, y + 5);
    oled.print(modeNames[i]);
  }

  drawLabelCentered(116, "TURN=scroll PRESS=set", C_DGRAY);
}

void drawSpecialScreen() {
  drawBg();
  oled.fillRect(0, 0, OLED_W, 16, C_SURF);
  oled.drawFastHLine(0, 16, OLED_W, C_MINT);
  drawLabelCentered(4, "SPECIAL", C_MINT);

  struct { const char* name; uint16_t col; uint32_t code; } cmds[] = {
    { "DANCE",   C_PURPLE, RF_DANCE   },
    { "DEFENSE", C_AMBER,  RF_DEFENSE },
    { "E-STOP",  C_CORAL,  RF_ESTOP   },
    { "< BACK",  C_LGRAY,  0          },
  };
  int sel = ((encPos % 4) + 4) % 4;
  for (int i = 0; i < 4; i++) {
    bool isSel = (i == sel);
    int y = 22 + i * 22;
    uint16_t bg = isSel ? cmds[i].col : C_SURF;
    uint16_t fg = isSel ? C_BG        : cmds[i].col;
    oled.fillRoundRect(8, y, 112, 18, 4, bg);
    oled.drawRoundRect(8, y, 112, 18, 4, cmds[i].col);
    if (isSel) oled.drawFastHLine(12, y, 104, C_WHITE);
    oled.setTextSize(1); oled.setTextColor(fg);
    int tw = strlen(cmds[i].name) * 6;
    oled.setCursor((OLED_W - tw) / 2, y + 5);
    oled.print(cmds[i].name);
  }
  drawLabelCentered(116, "TURN=scroll PRESS=run", C_DGRAY);
}

void drawSpeedScreen() {
  drawBg();
  oled.fillRect(0, 0, OLED_W, 16, C_SURF);
  oled.drawFastHLine(0, 16, OLED_W, C_AMBER);
  drawLabelCentered(4, "SPEED", C_AMBER);

  const uint16_t sCols[] = { C_MINT, C_CYAN, C_CORAL };
  for (int i = 0; i < 3; i++) {
    bool sel = (i == speedLevel);
    int y = 28 + i * 26;
    uint16_t bg = sel ? sCols[i] : C_SURF;
    uint16_t fg = sel ? C_BG     : sCols[i];
    oled.fillRoundRect(12, y, 104, 22, 5, bg);
    oled.drawRoundRect(12, y, 104, 22, 5, sCols[i]);
    if (sel) {
      oled.drawFastHLine(16, y, 96, C_WHITE);
      oled.drawFastHLine(16, y + 21, 96, C_WHITE);
    }
    oled.setTextSize(2); oled.setTextColor(fg);
    int tw = strlen(speedNames[i]) * 12;
    oled.setCursor((OLED_W - tw) / 2, y + 7);
    oled.print(speedNames[i]);
  }
  drawLabelCentered(108, "TURN=change PRESS=set", C_DGRAY);
}

// ════════════════════════════════════════════════════════════════════
//  BATTERY MONITOR
// ════════════════════════════════════════════════════════════════════
void updateBattery() {
  if (millis() - lastBatRead < 10000) return;  // every 10s
  lastBatRead = millis();
  // 173040 LiPo: 3.0V (0%) → 4.2V (100%)
  // ADC via voltage divider (2x100k = half voltage on 3.3V ref)
  int raw = analogRead(BAT_PIN);
  float measuredV = (raw / 1023.0f) * 3.3f * 2.0f;  // x2 for divider
  batVoltage = measuredV;
  batPct = (uint8_t)(constrain((measuredV - 3.0f) / 1.2f * 100.0f, 0.0f, 100.0f));
}

// ════════════════════════════════════════════════════════════════════
//  BUTTON HANDLER
// ════════════════════════════════════════════════════════════════════
struct BtnResult { bool click; bool longClick; };

BtnResult handleButton() {
  BtnResult r = { false, false };
  bool down = (digitalRead(ENC_SW) == LOW);

  if (down && !btnWasDown) {
    btnDownMs  = millis();
    btnWasDown = true;
  }

  if (!down && btnWasDown) {
    unsigned long held = millis() - btnDownMs;
    btnWasDown = false;
    if (held >= LONG_PRESS_MS) r.longClick = true;
    else                        r.click     = true;
  }
  return r;
}

// ════════════════════════════════════════════════════════════════════
//  ENCODER READER
// ════════════════════════════════════════════════════════════════════
int8_t readEncoder() {
  noInterrupts();
  int8_t d = encDelta;
  encDelta  = 0;
  interrupts();
  return d;
}

// ════════════════════════════════════════════════════════════════════
//  STATE MACHINE TRANSITIONS
// ════════════════════════════════════════════════════════════════════
void handleDriveState(int8_t delta, BtnResult btn) {
  // Encoder rotation → drive direction
  // CW: advance forward or right; CCW: backward or left
  if (delta > 0) {
    driveDir = constrain(driveDir + 1, -2, 2);
  } else if (delta < 0) {
    driveDir = constrain(driveDir - 1, -2, 2);
  }

  // Short press → cycle to next screen
  if (btn.click) {
    state    = ST_MODE;
    encPos   = modeIdx;
    driveDir = 0;
    rfSwitch.send(RF_STOP, 24);
    lastCode = RF_STOP;
    oled.fillScreen(C_BG);
  }

  // Long press → special commands
  if (btn.longClick) {
    state   = ST_SPECIAL;
    encPos  = 0;
    driveDir = 0;
    rfSwitch.send(RF_STOP, 24);
    lastCode = RF_STOP;
    oled.fillScreen(C_BG);
  }
}

void handleModeState(int8_t delta, BtnResult btn) {
  if (delta != 0) {
    modeIdx = ((int)modeIdx + delta + 4) % 4;
  }

  // Short press → set mode + go to speed screen
  if (btn.click) {
    rfSwitch.send(modeCodes[modeIdx], 24);
    state  = ST_SPEED;
    encPos = speedLevel;
    oled.fillScreen(C_BG);
  }

  // Long press → back to drive
  if (btn.longClick) {
    state = ST_DRIVE;
    oled.fillScreen(C_BG);
  }
}

void handleSpecialState(int8_t delta, BtnResult btn) {
  if (delta != 0) {
    encPos = ((int)encPos + delta + 4) % 4;
  }

  // Short press → execute
  if (btn.click) {
    int sel = ((encPos % 4) + 4) % 4;
    if (sel == 0)      rfSwitch.send(RF_DANCE,   24);
    else if (sel == 1) rfSwitch.send(RF_DEFENSE, 24);
    else if (sel == 2) rfSwitch.send(RF_ESTOP,   24);
    else {
      // BACK
      state = ST_DRIVE;
      oled.fillScreen(C_BG);
      return;
    }
    delay(200);
  }

  // Long press → back to drive
  if (btn.longClick) {
    state = ST_DRIVE;
    oled.fillScreen(C_BG);
  }
}

void handleSpeedState(int8_t delta, BtnResult btn) {
  if (delta > 0 && speedLevel < 2) speedLevel++;
  if (delta < 0 && speedLevel > 0) speedLevel--;

  // Short press → set + go back to drive
  if (btn.click) {
    // Send speed command to robot
    if (speedLevel == 0)      rfSwitch.send(0xA10030UL, 24); // SLOW
    else if (speedLevel == 1) rfSwitch.send(0xA10031UL, 24); // NORMAL
    else                      rfSwitch.send(0xA10032UL, 24); // FAST
    state    = ST_DRIVE;
    driveDir = 0;
    oled.fillScreen(C_BG);
  }

  if (btn.longClick) {
    state = ST_DRIVE;
    oled.fillScreen(C_BG);
  }
}

// ════════════════════════════════════════════════════════════════════
//  SETUP
// ════════════════════════════════════════════════════════════════════
void setup() {
  // Encoder pins
  pinMode(ENC_CLK, INPUT_PULLUP);
  pinMode(ENC_DT,  INPUT_PULLUP);
  pinMode(ENC_SW,  INPUT_PULLUP);

  // Encoder ISR — trigger on any change
  attachInterrupt(digitalPinToInterrupt(ENC_CLK), encoderISR, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENC_DT),  encoderISR, CHANGE);

  // RF transmitter
  rfSwitch.enableTransmit(RF_PIN);
  rfSwitch.setProtocol(1);
  rfSwitch.setPulseLength(189);
  rfSwitch.setRepeatTransmit(5);  // 5 repeats for reliability

  // Battery ADC
  analogReadResolution(10);

  // OLED
  oled.begin();
  oled.setRotation(0);

  // Splash
  oled.fillScreen(C_BG);
  for (int x = 0; x < OLED_W; x += 8) oled.drawFastVLine(x, 0, OLED_H, 0x0421);
  for (int y = 0; y < OLED_H; y += 8) oled.drawFastHLine(0, y, OLED_W, 0x0421);
  oled.drawRect(8, 24, 112, 80, C_CYAN);
  oled.drawRect(10, 26, 108, 76, C_CYAN);
  oled.drawFastHLine(12, 26, 104, C_WHITE);
  drawLabelCentered(38,  "BUDDYBOT",  C_CYAN,  2);
  drawLabelCentered(60,  "REMOTE",    C_CYAN,  2);
  drawLabelCentered(80,  "v1.0",      C_LGRAY, 1);
  drawLabelCentered(112, "Reinsma Innovations", C_MGRAY, 1);
  delay(1800);
  oled.fillScreen(C_BG);

  // Initial battery read
  lastBatRead = 0;
  updateBattery();
}

// ════════════════════════════════════════════════════════════════════
//  LOOP
// ════════════════════════════════════════════════════════════════════
void loop() {
  // Read encoder delta accumulated since last loop
  int8_t delta = readEncoder();

  // Clamp large deltas (debounce)
  if      (delta >  2) delta =  1;
  else if (delta < -2) delta = -1;

  // Button
  BtnResult btn = handleButton();

  // State machine
  switch (state) {
    case ST_DRIVE:   handleDriveState(delta, btn);   break;
    case ST_MODE:    handleModeState(delta, btn);     break;
    case ST_SPECIAL: handleSpecialState(delta, btn);  break;
    case ST_SPEED:   handleSpeedState(delta, btn);    break;
  }

  // Battery update (non-blocking)
  updateBattery();

  // Redraw display at 12.5 fps max
  if (millis() - lastDraw >= DRAW_INTERVAL_MS) {
    lastDraw = millis();
    switch (state) {
      case ST_DRIVE:   drawDriveScreen();   break;
      case ST_MODE:    drawModeScreen();    break;
      case ST_SPECIAL: drawSpecialScreen(); break;
      case ST_SPEED:   drawSpeedScreen();   break;
    }
  }

  delay(10);
}
