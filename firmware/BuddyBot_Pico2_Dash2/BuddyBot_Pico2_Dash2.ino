/*
 * ════════════════════════════════════════════════════════════════════
 *  BUDDYBOT — Pico Dashboard  V6.0
 *  Cyberpunk Neon Theme  ·  Portrait 320x480  ·  TFT_eSPI
 *  Board: ArtronShop RP2 Nano (RP2040)
 *  UART0: GP0=TX→Mega19(RX1)  GP1=RX←Mega18(TX1) via divider
 * ════════════════════════════════════════════════════════════════════
 */

#include <TFT_eSPI.h>
#include <Wire.h>
#include <SPI.h>

TFT_eSPI tft = TFT_eSPI();

#define SCR_W       320
#define SCR_H       480
#define MEGA_SERIAL Serial1

// ── Cyberpunk Neon Palette (RGB565) ──────────────────────────────────
#define C_BG     0x0009   // near-black deep space
#define C_SURF   0x0811   // dark panel surface
#define C_SURF2  0x1082   // slightly lighter panel
#define C_CYAN   0x07FF   // neon cyan
#define C_PINK   0xF81F   // hot magenta
#define C_YELLOW 0xFFE0   // neon yellow
#define C_GREEN  0x07E0   // neon green
#define C_ORANGE 0xFC60   // neon orange
#define C_PURPLE 0x801F   // neon purple
#define C_RED    0xF800   // alert red
#define C_WHITE  0xFFFF
#define C_LGRAY  0x8410
#define C_DGRAY  0x2104
#define C_MGRAY  0x4208

// ── Telemetry ─────────────────────────────────────────────────────────
struct Telem {
  float temp     = 25.0f;
  float hum      = 50.0f;
  float volt     = 8.4f;
  int   pct      = 100;
  float amps     = 0.0f;
  int   gas      = 0;
  long  dFront   = -1, dRear = -1, dLeft = -1, dRight = -1;
  bool  r3ok     = false;
  bool  espok    = false;
  bool  s9ok     = false;
  bool  estop    = false;
  bool  autoM    = false;
  bool  charging = false;
  char  fw[16]   = "V31";
  char  mode[16] = "NORMAL";
} T;

// ── Parser state ──────────────────────────────────────────────────────
char          megaBuf[256];
uint16_t      megaBufLen = 0;
unsigned long lastMegaRx = 0;
bool          megaLinked = false;
bool          needsRedraw = true;
unsigned long lastPing    = 0;
uint8_t       pingSeq     = 0;

// ── Scanline ──────────────────────────────────────────────────────────
int16_t       scanY     = 0;
unsigned long lastScan  = 0;

// ════════════════════════════════════════════════════════════════════
//  DRAW PRIMITIVES
// ════════════════════════════════════════════════════════════════════

void neonBox(int x, int y, int w, int h, uint16_t col) {
  tft.fillRoundRect(x, y, w, h, 4, C_SURF);
  tft.drawRoundRect(x, y, w, h, 4, col);
  tft.drawRoundRect(x+1, y+1, w-2, h-2, 3, (uint16_t)(col >> 2));
  tft.fillRect(x+6, y, w-12, 2, col);
}

void sectionLabel(int y, const char* label, uint16_t col) {
  tft.drawFastHLine(0, y, SCR_W, C_DGRAY);
  int lw = strlen(label)*6 + 8;
  int lx = (SCR_W - lw) / 2;
  tft.fillRect(lx, y-4, lw, 9, C_BG);
  tft.setTextColor(col, C_BG);
  tft.setTextSize(1);
  tft.setCursor(lx+4, y-3);
  tft.print(label);
}

void drawCell(int x, int y, int w, int h,
              const char* label, const char* value, uint16_t col) {
  neonBox(x, y, w, h, col);
  tft.setTextSize(1);
  tft.setTextColor(col, C_SURF);
  tft.setCursor(x+5, y+5);
  tft.print(label);
  tft.setTextSize(2);
  tft.setTextColor(C_WHITE, C_SURF);
  int vw = strlen(value)*12;
  tft.setCursor(x + (w-vw)/2, y + h/2 - 4);
  tft.print(value);
}

void drawPill(int x, int y, const char* label, bool active, uint16_t col) {
  uint16_t bg = active ? (uint16_t)(col >> 3) : C_DGRAY;
  uint16_t fg = active ? col : C_MGRAY;
  int w = strlen(label)*6 + 10;
  tft.fillRoundRect(x, y, w, 13, 3, bg);
  tft.drawRoundRect(x, y, w, 13, 3, fg);
  tft.setTextSize(1);
  tft.setTextColor(fg, bg);
  tft.setCursor(x+5, y+3);
  tft.print(label);
}

void drawBattBar(int x, int y, int w, int h) {
  uint16_t col = T.pct > 50 ? C_GREEN : T.pct > 20 ? C_YELLOW : C_RED;
  tft.drawRect(x, y, w, h, C_MGRAY);
  int fill = (w-2) * T.pct / 100;
  tft.fillRect(x+1, y+1, fill,     h-2, col);
  tft.fillRect(x+1+fill, y+1, w-2-fill, h-2, C_DGRAY);
  tft.fillRect(x+w, y+2, 3, h-4, C_MGRAY);
}

void drawUSBar(int x, int y, int w, int h, long dist) {
  tft.drawRect(x, y, w, h, C_DGRAY);
  if (dist <= 0) { tft.fillRect(x+1,y+1,w-2,h-2,C_DGRAY); return; }
  long cap = dist > 200 ? 200 : dist;
  uint16_t col = dist < 20 ? C_RED : dist < 50 ? C_YELLOW : C_CYAN;
  int fill = (w-2)*cap/200;
  tft.fillRect(x+1, y+1, fill,     h-2, col);
  tft.fillRect(x+1+fill, y+1, w-2-fill, h-2, C_DGRAY);
}

// ════════════════════════════════════════════════════════════════════
//  SCREEN SECTIONS
// ════════════════════════════════════════════════════════════════════

void drawHeader() {
  tft.fillRect(0, 0, SCR_W, 46, C_SURF);
  tft.drawFastHLine(0, 46, SCR_W, C_CYAN);
  tft.drawFastHLine(0, 47, SCR_W, (uint16_t)(C_CYAN >> 2));

  tft.setTextSize(3);
  tft.setTextColor(C_CYAN, C_SURF);
  tft.setCursor(6, 6);
  tft.print("BUDDY");
  tft.setTextColor(C_PINK, C_SURF);
  tft.print("BOT");

  tft.setTextSize(1);
  tft.setTextColor(C_LGRAY, C_SURF);
  tft.setCursor(6, 34);
  tft.print("NEXUS HMI  ");
  tft.setTextColor(C_CYAN, C_SURF);
  tft.print(T.fw);

  // Mode badge
  tft.setTextColor(C_YELLOW, C_SURF);
  int mw = strlen(T.mode)*6;
  tft.setCursor(SCR_W - mw - 8, 12);
  tft.print(T.mode);

  // Link indicator
  uint16_t lc = megaLinked ? C_GREEN : C_RED;
  tft.fillCircle(SCR_W-8, 34, 4, lc);

  // ESTOP
  if (T.estop) {
    tft.fillRect(SCR_W/2-30, 28, 60, 14, C_RED);
    tft.setTextColor(C_WHITE, C_RED);
    tft.setCursor(SCR_W/2-26, 32);
    tft.print("!! ESTOP !!");
  }
}

void drawPower() {
  int y = 54;
  sectionLabel(y, "POWER", C_YELLOW);
  y += 6;

  char buf[20];
  snprintf(buf, sizeof(buf), "%.2fV", T.volt);
  uint16_t vc = T.volt > 7.8f ? C_GREEN : T.volt > 7.2f ? C_YELLOW : C_RED;
  drawCell(4, y, 150, 54, "VOLTAGE", buf, vc);

  snprintf(buf, sizeof(buf), "%.2fA", T.amps);
  drawCell(162, y, 150, 54, "CURRENT", buf, C_CYAN);

  y += 60;

  // Battery bar
  tft.setTextSize(1);
  tft.setTextColor(C_YELLOW, C_BG);
  tft.setCursor(4, y);
  tft.print("BATTERY");
  char pct[8]; snprintf(pct, sizeof(pct), "%d%%", T.pct);
  uint16_t pc = T.pct > 50 ? C_GREEN : T.pct > 20 ? C_YELLOW : C_RED;
  tft.setTextColor(pc, C_BG);
  tft.setCursor(SCR_W-26, y);
  tft.print(pct);
  if (T.charging) {
    tft.setTextColor(C_GREEN, C_BG);
    tft.setCursor(SCR_W/2-12, y);
    tft.print("CHG");
  }
  drawBattBar(4, y+10, SCR_W-8, 14);
}

void drawEnvironment() {
  int y = 192;
  sectionLabel(y, "ENVIRONMENT", C_PINK);
  y += 6;

  char buf[20];
  snprintf(buf, sizeof(buf), "%.1fC", T.temp);
  uint16_t tc = T.temp > 38 ? C_RED : T.temp > 32 ? C_YELLOW : C_CYAN;
  drawCell(4,   y, 95, 52, "TEMP", buf, tc);

  snprintf(buf, sizeof(buf), "%.0f%%", T.hum);
  drawCell(107, y, 95, 52, "HUM",  buf, C_PINK);

  snprintf(buf, sizeof(buf), "%d", T.gas);
  uint16_t gc = T.gas > 600 ? C_RED : T.gas > 400 ? C_YELLOW : C_GREEN;
  drawCell(210, y, 106, 52, "GAS", buf, gc);
}

void drawUltrasonics() {
  int y = 304;
  sectionLabel(y, "PROXIMITY", C_CYAN);
  y += 6;

  struct { const char* l; long d; } us[] = {
    {"FRONT", T.dFront}, {"REAR",  T.dRear},
    {"LEFT",  T.dLeft},  {"RIGHT", T.dRight}
  };

  int bw = (SCR_W - 16) / 2;
  for (int i = 0; i < 4; i++) {
    int cx = 4 + (i%2)*(bw+8);
    int cy = y + (i/2)*36;

    tft.setTextSize(1);
    tft.setTextColor(C_CYAN, C_BG);
    tft.setCursor(cx, cy);
    tft.print(us[i].l);

    char buf[12];
    if (us[i].d <= 0) snprintf(buf, sizeof(buf), "--");
    else              snprintf(buf, sizeof(buf), "%ldcm", us[i].d);
    tft.setTextColor(C_WHITE, C_BG);
    tft.setCursor(cx + bw - strlen(buf)*6 - 2, cy);
    tft.print(buf);

    drawUSBar(cx, cy+10, bw-2, 10, us[i].d);
  }
}

void drawStatus() {
  int y = 424;
  sectionLabel(y, "SYSTEMS", C_PURPLE);
  y += 6;

  int x = 4;
  drawPill(x, y, "MEGA", megaLinked, C_GREEN); x += 50;
  drawPill(x, y, "R3",   T.r3ok,    C_GREEN);  x += 38;
  drawPill(x, y, "ESP",  T.espok,   C_CYAN);   x += 40;
  drawPill(x, y, "S9",   T.s9ok,    C_PINK);   x += 36;
  drawPill(x, y, "AUTO", T.autoM,   C_YELLOW);
}

void drawScanline() {
  if (millis() - lastScan < 30) return;
  lastScan = millis();
  tft.drawFastHLine(0, scanY, SCR_W, C_BG);
  scanY = (scanY + 3) % SCR_H;
  tft.drawFastHLine(0, scanY, SCR_W, 0x0421);
}

void drawScreen() {
  tft.fillScreen(C_BG);
  drawHeader();
  drawPower();
  drawEnvironment();
  drawUltrasonics();
  drawStatus();
}

// ════════════════════════════════════════════════════════════════════
//  PARSERS  (zero heap)
// ════════════════════════════════════════════════════════════════════

void parseStat(const char* s) {
  static char buf[160]; static char* f[11]; int n=0;
  strncpy(buf,s+5,sizeof(buf)-1); buf[sizeof(buf)-1]=0;
  char* p=strtok(buf,":"); while(p&&n<11){f[n++]=p;p=strtok(NULL,":");}
  if(n<10)return;
  T.gas=atoi(f[0]); T.temp=atof(f[1]); T.hum=atof(f[2]);
  T.volt=atof(f[7]); T.pct=atoi(f[8]); T.amps=atof(f[9]);
  needsRedraw=true;
}

void parseUS(const char* s) {
  static char buf[64]; static char* f[4]; int n=0;
  strncpy(buf,s+3,sizeof(buf)-1); buf[sizeof(buf)-1]=0;
  char* p=strtok(buf,","); while(p&&n<4){f[n++]=p;p=strtok(NULL,",");}
  if(n<4)return;
  T.dFront=atol(f[0]); T.dRear=atol(f[1]);
  T.dLeft=atol(f[2]);  T.dRight=atol(f[3]);
  needsRedraw=true;
}

void parseStatus(const char* s) {
  T.estop    = strstr(s,"ESTOP:YES")!=NULL;
  T.autoM    = strstr(s,"AUTO:ON")  !=NULL;
  T.r3ok     = strstr(s,"R3:OK")    !=NULL;
  T.espok    = strstr(s,"ESP:OK")   !=NULL;
  T.s9ok     = strstr(s,"S9:OK")    !=NULL;
  T.charging = strstr(s,"CHG:YES")  !=NULL;
  const char* fw=strstr(s,"FW:");
  if(fw){fw+=3; const char* e=strchr(fw,'|');
    size_t l=e?(size_t)(e-fw):strlen(fw); if(l>15)l=15;
    memcpy(T.fw,fw,l); T.fw[l]=0;}
  const char* mo=strstr(s,"MODE:");
  if(mo){mo+=5; const char* e=strchr(mo,'|');
    size_t l=e?(size_t)(e-mo):strlen(mo); if(l>15)l=15;
    memcpy(T.mode,mo,l); T.mode[l]=0;}
  needsRedraw=true;
}

// ════════════════════════════════════════════════════════════════════
//  SERIAL HANDLER
// ════════════════════════════════════════════════════════════════════

void handleMegaLine(const char* line) {
  lastMegaRx=millis(); megaLinked=true;
  if      (strncmp(line,"STAT:",5)==0)          parseStat(line);
  else if (strncmp(line,"US:",3)==0)             parseUS(line);
  else if (strncmp(line,"STATUS|",7)==0)         parseStatus(line);
  else if (strncmp(line,"SYSTEM|READY|",13)==0) {parseStatus(line); MEGA_SERIAL.println("SENSOR_STATUS");}
  else if (strcmp(line,"PING")==0)               MEGA_SERIAL.println("PONG");
}

void handleMegaSerial() {
  while(MEGA_SERIAL.available()){
    char c=MEGA_SERIAL.read();
    if(c=='\n'){
      megaBuf[megaBufLen]=0;
      if(megaBufLen>0) handleMegaLine(megaBuf);
      megaBufLen=0;
    } else if(c!='\r'){
      if(megaBufLen<sizeof(megaBuf)-1) megaBuf[megaBufLen++]=c;
      else megaBufLen=0;
    }
  }
}

void handlePing() {
  if(millis()-lastPing>5000){
    lastPing=millis();
    MEGA_SERIAL.print("PING_PICO:"); MEGA_SERIAL.println(pingSeq++);
  }
  if(megaLinked && millis()-lastMegaRx>12000){
    megaLinked=false; needsRedraw=true;
  }
}

// ════════════════════════════════════════════════════════════════════
//  SETUP & LOOP
// ════════════════════════════════════════════════════════════════════

void setup() {
  tft.init();
  tft.setRotation(0);   // portrait 320x480
  tft.fillScreen(C_BG);

  tft.setTextSize(2);
  tft.setTextColor(C_CYAN, C_BG);
  tft.setCursor(8, 8);
  tft.print("BUDDYBOT");
  tft.setTextColor(C_PINK, C_BG);
  tft.setCursor(8, 30);
  tft.print("NEXUS HMI v6.0");
  tft.setTextSize(1);
  tft.setTextColor(C_LGRAY, C_BG);
  tft.setCursor(8, 56);
  tft.print("Waiting for Mega...");

  Serial1.setTX(0);
  Serial1.setRX(1);
  MEGA_SERIAL.begin(115200);
  delay(200);
  MEGA_SERIAL.println("PONG");
  MEGA_SERIAL.println("SENSOR_STATUS");
}

void loop() {
  handleMegaSerial();
  handlePing();

  static unsigned long lastDraw=0;
  if(needsRedraw && millis()-lastDraw>200){
    lastDraw=millis();
    needsRedraw=false;
    drawScreen();
  }

  drawScanline();
}
