/*
 * ════════════════════════════════════════════════════════════════════
 *  BUDDYBOT — Pico (RP2040) Orbital HMI Dashboard  V2.0
 * ════════════════════════════════════════════════════════════════════
 *  No TFT_eSPI. Uses direct Arduino SPI — proven working on this hw.
 *
 *  WIRING (same as before — nothing changes on the physical board)
 *  Display SPI0:  GP16=MISO  GP17=CS  GP18=SCK  GP19=MOSI
 *                 GP20=RST   GP21=DC  GP22=BL
 *  Touch  I2C:    GP26=SDA   GP27=SCL GP28=INT  GP15=RST
 *  Mega Serial1:  GP0=TX     GP1=RX
 * ════════════════════════════════════════════════════════════════════
 */
#include <SPI.h>
#include <Wire.h>

// Forward declarations — required because Arduino IDE auto-prototype
// generation fails on user-defined struct types in function signatures
struct Touch { int16_t x, y; bool pressed; };
Touch readTouch();
bool  hit(const Touch& t, int16_t x, int16_t y, int16_t w, int16_t h);
bool  drawBackBtn(Touch& t);
void  drawMain(Touch& t);
void  drawRadar(Touch& t);
void  drawGamesMenu(Touch& t);
void  drawGameColor(Touch& t);
void  drawGameShape(Touch& t);
void  drawGameCount(Touch& t);
void  drawInfo(Touch& t);
void  drawAlert(Touch& t);

// ── Pin map ───────────────────────────────────────────────────────
#define PIN_BL       22
#define PIN_CS       17
#define PIN_DC       21
#define PIN_RST      20
#define PIN_CTP_SDA  26
#define PIN_CTP_SCL  27
#define PIN_CTP_INT  28
#define PIN_CTP_RST  15

// ── ST7796S commands ──────────────────────────────────────────────
#define ST_SWRESET 0x01
#define ST_SLPOUT  0x11
#define ST_COLMOD  0x3A
#define ST_MADCTL  0x36
#define ST_DISPON  0x29
#define ST_CASET   0x2A
#define ST_PASET   0x2B
#define ST_RAMWR   0x2C

// ── Screen dimensions (landscape via MADCTL) ──────────────────────
#define SCR_W  480
#define SCR_H  320
#define HDR_H   42
#define FTR_H   38
#define CNT_Y  HDR_H
#define CNT_H  (SCR_H - HDR_H - FTR_H)

// ── Colour palette (RGB565) ───────────────────────────────────────
#define C_BG     0x0209u
#define C_SURF   0x0862u
#define C_SURF2  0x10A3u
#define C_LINE   0x2124u
#define C_CYAN   0x07FFu
#define C_DCYAN  0x0398u
#define C_MINT   0x07E4u
#define C_AMBER  0xFD20u
#define C_CORAL  0xFB0Cu
#define C_MAG    0xF81Fu
#define C_PURP   0x801Fu
#define C_YLLOW  0xFFE0u
#define C_WHITE  0xFFFFu
#define C_LGRAY  0x8C71u
#define C_MGRAY  0x528Au
#define C_BLACK  0x0000u
#define C_RED    0xF800u
#define C_GREEN  0x07E0u
#define C_BLUE   0x001Fu

// ════════════════════════════════════════════════════════════════════
//  LOW-LEVEL SPI DRIVER
// ════════════════════════════════════════════════════════════════════
static SPISettings _spiCfg(20000000, MSBFIRST, SPI_MODE0);

inline void _cmd(uint8_t c) {
  digitalWrite(PIN_DC, LOW);
  digitalWrite(PIN_CS, LOW);
  SPI.transfer(c);
  digitalWrite(PIN_CS, HIGH);
}
inline void _dat(uint8_t d) {
  digitalWrite(PIN_DC, HIGH);
  digitalWrite(PIN_CS, LOW);
  SPI.transfer(d);
  digitalWrite(PIN_CS, HIGH);
}
inline void _dat16(uint16_t d) {
  digitalWrite(PIN_DC, HIGH);
  digitalWrite(PIN_CS, LOW);
  SPI.transfer(d >> 8);
  SPI.transfer(d & 0xFF);
  digitalWrite(PIN_CS, HIGH);
}

void displayInit() {
  pinMode(PIN_CS,  OUTPUT); digitalWrite(PIN_CS,  HIGH);
  pinMode(PIN_DC,  OUTPUT); digitalWrite(PIN_DC,  HIGH);
  pinMode(PIN_RST, OUTPUT);
  pinMode(PIN_BL,  OUTPUT); digitalWrite(PIN_BL, HIGH);

  digitalWrite(PIN_RST, HIGH); delay(20);
  digitalWrite(PIN_RST, LOW);  delay(50);
  digitalWrite(PIN_RST, HIGH); delay(150);

  SPI.setTX(19); SPI.setRX(16); SPI.setSCK(18);
  SPI.begin();
  SPI.beginTransaction(_spiCfg);

  _cmd(ST_SWRESET); delay(120);
  _cmd(ST_SLPOUT);  delay(120);
  _cmd(ST_COLMOD);  _dat(0x55);        // 16-bit RGB565
  // MADCTL: landscape, MX+MV = rotation 1 equivalent, BGR colour order
  _cmd(ST_MADCTL);  _dat(0x68);
  _cmd(ST_DISPON);  delay(50);
}

void setWindow(int16_t x0, int16_t y0, int16_t x1, int16_t y1) {
  _cmd(ST_CASET); _dat(x0>>8); _dat(x0); _dat(x1>>8); _dat(x1);
  _cmd(ST_PASET); _dat(y0>>8); _dat(y0); _dat(y1>>8); _dat(y1);
  _cmd(ST_RAMWR);
}

void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c) {
  if (w <= 0 || h <= 0) return;
  setWindow(x, y, x+w-1, y+h-1);
  digitalWrite(PIN_DC, HIGH);
  digitalWrite(PIN_CS, LOW);
  uint32_t n = (uint32_t)w * h;
  uint8_t hi = c >> 8, lo = c & 0xFF;
  for (uint32_t i = 0; i < n; i++) { SPI.transfer(hi); SPI.transfer(lo); }
  digitalWrite(PIN_CS, HIGH);
}

void fillScreen(uint16_t c) { fillRect(0, 0, SCR_W, SCR_H, c); }

void drawPixel(int16_t x, int16_t y, uint16_t c) {
  if (x < 0 || x >= SCR_W || y < 0 || y >= SCR_H) return;
  setWindow(x, y, x, y); _dat16(c);
}

// ════════════════════════════════════════════════════════════════════
//  FONT / TEXT ENGINE  (simple 6x8 bitmap, built-in)
// ════════════════════════════════════════════════════════════════════
// 6x8 font — printable ASCII 0x20-0x7E, 5 wide + 1 gap
static const uint8_t FONT6x8[][5] PROGMEM = {
  {0x00,0x00,0x00,0x00,0x00}, // 0x20 space
  {0x00,0x00,0x5F,0x00,0x00}, // !
  {0x00,0x07,0x00,0x07,0x00}, // "
  {0x14,0x7F,0x14,0x7F,0x14}, // #
  {0x24,0x2A,0x7F,0x2A,0x12}, // $
  {0x23,0x13,0x08,0x64,0x62}, // %
  {0x36,0x49,0x55,0x22,0x50}, // &
  {0x00,0x05,0x03,0x00,0x00}, // '
  {0x00,0x1C,0x22,0x41,0x00}, // (
  {0x00,0x41,0x22,0x1C,0x00}, // )
  {0x08,0x2A,0x1C,0x2A,0x08}, // *
  {0x08,0x08,0x3E,0x08,0x08}, // +
  {0x00,0x50,0x30,0x00,0x00}, // ,
  {0x08,0x08,0x08,0x08,0x08}, // -
  {0x00,0x60,0x60,0x00,0x00}, // .
  {0x20,0x10,0x08,0x04,0x02}, // /
  {0x3E,0x51,0x49,0x45,0x3E}, // 0
  {0x00,0x42,0x7F,0x40,0x00}, // 1
  {0x42,0x61,0x51,0x49,0x46}, // 2
  {0x21,0x41,0x45,0x4B,0x31}, // 3
  {0x18,0x14,0x12,0x7F,0x10}, // 4
  {0x27,0x45,0x45,0x45,0x39}, // 5
  {0x3C,0x4A,0x49,0x49,0x30}, // 6
  {0x01,0x71,0x09,0x05,0x03}, // 7
  {0x36,0x49,0x49,0x49,0x36}, // 8
  {0x06,0x49,0x49,0x29,0x1E}, // 9
  {0x00,0x36,0x36,0x00,0x00}, // :
  {0x00,0x56,0x36,0x00,0x00}, // ;
  {0x08,0x14,0x22,0x41,0x00}, // <
  {0x14,0x14,0x14,0x14,0x14}, // =
  {0x00,0x41,0x22,0x14,0x08}, // >
  {0x02,0x01,0x51,0x09,0x06}, // ?
  {0x32,0x49,0x79,0x41,0x3E}, // @
  {0x7E,0x11,0x11,0x11,0x7E}, // A
  {0x7F,0x49,0x49,0x49,0x36}, // B
  {0x3E,0x41,0x41,0x41,0x22}, // C
  {0x7F,0x41,0x41,0x22,0x1C}, // D
  {0x7F,0x49,0x49,0x49,0x41}, // E
  {0x7F,0x09,0x09,0x09,0x01}, // F
  {0x3E,0x41,0x49,0x49,0x7A}, // G
  {0x7F,0x08,0x08,0x08,0x7F}, // H
  {0x00,0x41,0x7F,0x41,0x00}, // I
  {0x20,0x40,0x41,0x3F,0x01}, // J
  {0x7F,0x08,0x14,0x22,0x41}, // K
  {0x7F,0x40,0x40,0x40,0x40}, // L
  {0x7F,0x02,0x0C,0x02,0x7F}, // M
  {0x7F,0x04,0x08,0x10,0x7F}, // N
  {0x3E,0x41,0x41,0x41,0x3E}, // O
  {0x7F,0x09,0x09,0x09,0x06}, // P
  {0x3E,0x41,0x51,0x21,0x5E}, // Q
  {0x7F,0x09,0x19,0x29,0x46}, // R
  {0x46,0x49,0x49,0x49,0x31}, // S
  {0x01,0x01,0x7F,0x01,0x01}, // T
  {0x3F,0x40,0x40,0x40,0x3F}, // U
  {0x1F,0x20,0x40,0x20,0x1F}, // V
  {0x3F,0x40,0x38,0x40,0x3F}, // W
  {0x63,0x14,0x08,0x14,0x63}, // X
  {0x07,0x08,0x70,0x08,0x07}, // Y
  {0x61,0x51,0x49,0x45,0x43}, // Z
  {0x00,0x7F,0x41,0x41,0x00}, // [
  {0x02,0x04,0x08,0x10,0x20}, // backslash
  {0x00,0x41,0x41,0x7F,0x00}, // ]
  {0x04,0x02,0x01,0x02,0x04}, // ^
  {0x40,0x40,0x40,0x40,0x40}, // _
  {0x00,0x01,0x02,0x04,0x00}, // `
  {0x20,0x54,0x54,0x54,0x78}, // a
  {0x7F,0x48,0x44,0x44,0x38}, // b
  {0x38,0x44,0x44,0x44,0x20}, // c
  {0x38,0x44,0x44,0x48,0x7F}, // d
  {0x38,0x54,0x54,0x54,0x18}, // e
  {0x08,0x7E,0x09,0x01,0x02}, // f
  {0x0C,0x52,0x52,0x52,0x3E}, // g
  {0x7F,0x08,0x04,0x04,0x78}, // h
  {0x00,0x44,0x7D,0x40,0x00}, // i
  {0x20,0x40,0x44,0x3D,0x00}, // j
  {0x7F,0x10,0x28,0x44,0x00}, // k
  {0x00,0x41,0x7F,0x40,0x00}, // l
  {0x7C,0x04,0x18,0x04,0x78}, // m
  {0x7C,0x08,0x04,0x04,0x78}, // n
  {0x38,0x44,0x44,0x44,0x38}, // o
  {0x7C,0x14,0x14,0x14,0x08}, // p
  {0x08,0x14,0x14,0x18,0x7C}, // q
  {0x7C,0x08,0x04,0x04,0x08}, // r
  {0x48,0x54,0x54,0x54,0x20}, // s
  {0x04,0x3F,0x44,0x40,0x20}, // t
  {0x3C,0x40,0x40,0x40,0x3C}, // u
  {0x1C,0x20,0x40,0x20,0x1C}, // v
  {0x3C,0x40,0x30,0x40,0x3C}, // w
  {0x44,0x28,0x10,0x28,0x44}, // x
  {0x0C,0x50,0x50,0x50,0x3C}, // y
  {0x44,0x64,0x54,0x4C,0x44}, // z
  {0x00,0x08,0x36,0x41,0x00}, // {
  {0x00,0x00,0x7F,0x00,0x00}, // |
  {0x00,0x41,0x36,0x08,0x00}, // }
  {0x08,0x04,0x08,0x10,0x08}, // ~
};

static uint16_t _fg = C_WHITE, _bg = C_BG;
static uint8_t  _sz = 1;
static int16_t  _cx = 0, _cy = 0;

void setTextColor(uint16_t fg, uint16_t bg) { _fg = fg; _bg = bg; }
void setTextColor(uint16_t fg)              { _fg = fg; _bg = 0xFFFF; } // 0xFFFF = transparent
void setTextSize(uint8_t s)                 { _sz = s ? s : 1; }
void setCursor(int16_t x, int16_t y)        { _cx = x; _cy = y; }

int16_t textWidth(const char* s) {
  return strlen(s) * 6 * _sz;
}

void drawChar(int16_t x, int16_t y, char ch, uint16_t fg, uint16_t bg, uint8_t sz) {
  if (ch < 0x20 || ch > 0x7E) ch = '?';
  const uint8_t* glyph = FONT6x8[ch - 0x20];
  for (int8_t col = 0; col < 6; col++) {
    uint8_t line = (col < 5) ? pgm_read_byte(&glyph[col]) : 0;
    for (int8_t row = 0; row < 8; row++) {
      uint16_t c = (line & 0x01) ? fg : bg;
      if (c != 0xFFFF) fillRect(x + col*sz, y + row*sz, sz, sz, c);
      line >>= 1;
    }
  }
}

void drawStr(int16_t x, int16_t y, const char* s, uint16_t fg, uint16_t bg, uint8_t sz) {
  while (*s) { drawChar(x, y, *s++, fg, bg, sz); x += 6*sz; }
}

// Centred print helper
void drawStrCentered(int16_t cx, int16_t y, const char* s, uint16_t fg, uint16_t bg, uint8_t sz) {
  int16_t w = strlen(s) * 6 * sz;
  drawStr(cx - w/2, y, s, fg, bg, sz);
}

// ════════════════════════════════════════════════════════════════════
//  SHAPE PRIMITIVES
// ════════════════════════════════════════════════════════════════════
void drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t c) {
  fillRect(x, y, w, 1, c);
}
void drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t c) {
  fillRect(x, y, 1, h, c);
}
void drawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c) {
  drawFastHLine(x, y, w, c);
  drawFastHLine(x, y+h-1, w, c);
  drawFastVLine(x, y, h, c);
  drawFastVLine(x+w-1, y, h, c);
}
void fillRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t c) {
  fillRect(x+r, y, w-2*r, h, c);
  fillRect(x, y+r, r, h-2*r, c);
  fillRect(x+w-r, y+r, r, h-2*r, c);
}
void drawRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t c) {
  drawFastHLine(x+r, y, w-2*r, c);
  drawFastHLine(x+r, y+h-1, w-2*r, c);
  drawFastVLine(x, y+r, h-2*r, c);
  drawFastVLine(x+w-1, y+r, h-2*r, c);
}
void fillCircle(int16_t cx, int16_t cy, int16_t r, uint16_t c) {
  for (int16_t dy = -r; dy <= r; dy++) {
    int16_t dx = (int16_t)sqrt((float)(r*r - dy*dy));
    fillRect(cx-dx, cy+dy, dx*2+1, 1, c);
  }
}
void drawCircle(int16_t cx, int16_t cy, int16_t r, uint16_t c) {
  int16_t x=r,y=0,err=0;
  while (x>=y) {
    drawPixel(cx+x,cy+y,c); drawPixel(cx+y,cy+x,c);
    drawPixel(cx-y,cy+x,c); drawPixel(cx-x,cy+y,c);
    drawPixel(cx-x,cy-y,c); drawPixel(cx-y,cy-x,c);
    drawPixel(cx+y,cy-x,c); drawPixel(cx+x,cy-y,c);
    if (err<=0){y++;err+=2*y+1;}
    if (err>0) {x--;err-=2*x+1;}
  }
}
void fillTriangle(int16_t x0,int16_t y0,int16_t x1,int16_t y1,int16_t x2,int16_t y2,uint16_t c){
  int16_t a,b,y,last;
  if(y0>y1){int16_t t=y0;y0=y1;y1=t;t=x0;x0=x1;x1=t;}
  if(y1>y2){int16_t t=y1;y1=y2;y2=t;t=x1;x1=x2;x2=t;}
  if(y0>y1){int16_t t=y0;y0=y1;y1=t;t=x0;x0=x1;x1=t;}
  if(y0==y2){return;}
  for(y=y0;y<=y2;y++){
    if(y<y1){a=x0+(x1-x0)*(y-y0)/(y1-y0);}
    else    {a=x1+(x2-x1)*(y-y1)/(y2-y1);}
    b=x0+(x2-x0)*(y-y0)/(y2-y0);
    if(a>b){int16_t t=a;a=b;b=t;}
    drawFastHLine(a,y,b-a+1,c);
  }
}

void drawButton(int16_t x,int16_t y,int16_t w,int16_t h,uint16_t bg,uint16_t border,const char* lbl,uint8_t sz=2){
  fillRoundRect(x,y,w,h,8,bg);
  drawRoundRect(x,y,w,h,8,border);
  int16_t tw=strlen(lbl)*6*sz, th=8*sz;
  drawStr(x+(w-tw)/2, y+(h-th)/2, lbl, border, bg, sz);
}

// ════════════════════════════════════════════════════════════════════
//  TOUCH (FT6336U I2C — identical to original)
// ════════════════════════════════════════════════════════════════════
#define CTP_ADDR 0x38
// Touch struct declared above as forward declaration;

Touch readTouch() {
  Touch t={0,0,false};
  Wire.beginTransmission(CTP_ADDR);
  Wire.write(0x02);
  if(Wire.endTransmission(false)!=0) return t;
  Wire.requestFrom(CTP_ADDR,6);
  if(Wire.available()<6) return t;
  uint8_t n=Wire.read()&0x0F;
  uint8_t xH=Wire.read(),xL=Wire.read();
  uint8_t yH=Wire.read(),yL=Wire.read();
  Wire.read();
  if(n==0||n>2) return t;
  int16_t rx=((xH&0x0F)<<8)|xL;
  int16_t ry=((yH&0x0F)<<8)|yL;
  t.x=ry; t.y=319-rx; t.pressed=true;
  return t;
}

bool hit(const Touch& t,int16_t x,int16_t y,int16_t w,int16_t h){
  return t.pressed&&t.x>=x&&t.x<x+w&&t.y>=y&&t.y<y+h;
}

// ════════════════════════════════════════════════════════════════════
//  APP STATE
// ════════════════════════════════════════════════════════════════════
struct Telemetry {
  int gas=0; float temp=0,hum=0;
  bool haz=false,pir=false,tilt=false,flame=false,ir=false;
  float volt=8.4f; int pct=100; float amps=0,boost=0;
  long dFront=-1,dRear=-1,dLeft=-1,dRight=-1;
  bool estop=false,autoM=false;
  String mode="NORMAL",fw="";
  bool r3ok=false,espok=false,s9ok=false;
} T;

enum Screen { SCR_BOOT,SCR_MAIN,SCR_RADAR,SCR_GAMES,
              SCR_GAME_COLOR,SCR_GAME_SHAPE,SCR_GAME_COUNT,
              SCR_INFO,SCR_SENSORS };
Screen curScr=SCR_BOOT;
bool   scrDirty=true;
bool   alertOn=false;
String alertTitle="",alertMsg="";
uint16_t alertCol=C_CORAL;
unsigned long alertTs=0;

#define MEGA_SERIAL     Serial1
String        megaBuf="";
bool          megaLinked=false;
unsigned long lastMegaRx=0,lastPing=0;
uint16_t      pingSeq=0;

const uint16_t GCOLS[]={C_RED,C_GREEN,C_BLUE,C_YLLOW};
const char* GCOLNAMES[]={"RED","GREEN","BLUE","YELLOW"};
int gTarget=0,gScore=0;
unsigned long gFeedbackUntil=0;
unsigned long lastTelemDraw=0,lastTouchMs=0;

// ════════════════════════════════════════════════════════════════════
//  COMMON UI CHROME
// ════════════════════════════════════════════════════════════════════
void drawHeader(const char* title) {
  fillRect(0,0,SCR_W,HDR_H,C_SURF);
  drawFastHLine(0,HDR_H-1,SCR_W,C_CYAN);
  uint16_t mc=C_CYAN;
  if(T.estop) mc=C_CORAL;
  else if(T.mode=="BODYGUARD") mc=C_AMBER;
  else if(T.mode=="DOG")       mc=C_MINT;
  else if(T.mode=="UNHINGED")  mc=C_MAG;
  else if(T.mode=="PARTY")     mc=C_PURP;
  const char* ms=T.estop?"E-STOP":T.mode.c_str();
  fillRect(0,0,108,HDR_H,mc);
  drawStrCentered(54,(HDR_H-16)/2,ms,C_BLACK,mc,2);
  drawStrCentered(SCR_W/2,(HDR_H-16)/2,title,C_WHITE,C_SURF,2);
  char bat[20]; snprintf(bat,sizeof(bat),"%d%% %.1fV",T.pct,T.volt);
  uint16_t bc=T.pct>50?C_MINT:(T.pct>20?C_AMBER:C_CORAL);
  int16_t tw=strlen(bat)*6;
  drawStr(SCR_W-tw-14,(HDR_H-8)/2,bat,bc,C_SURF,1);
  fillCircle(SCR_W-7,HDR_H/2,4,megaLinked?C_MINT:C_CORAL);
}

void drawFooter() {
  int fy=SCR_H-FTR_H;
  fillRect(0,fy,SCR_W,FTR_H,C_SURF);
  drawFastHLine(0,fy,SCR_W,C_LINE);
  uint16_t ec=T.estop?C_AMBER:C_CORAL;
  const char* el=T.estop?"CLR STP":"E-STOP";
  drawButton(4,fy+4,88,FTR_H-8,ec,C_WHITE,el,1);
  int ix=100;
  if(T.flame){drawStr(ix,fy+13,"FLAME",C_CORAL,C_SURF,1);ix+=46;}
  if(T.tilt) {drawStr(ix,fy+13,"TILT", C_AMBER,C_SURF,1);ix+=36;}
  if(T.haz)  {drawStr(ix,fy+13,"HAZ",  C_AMBER,C_SURF,1);}
  if(megaLinked){
    unsigned long age=(millis()-lastMegaRx)/1000;
    char buf[22];
    if(age<5) snprintf(buf,sizeof(buf),"Mega: LIVE");
    else      snprintf(buf,sizeof(buf),"Mega: %lus",age);
    uint16_t lc=age<5?C_MINT:C_AMBER;
    int16_t tw=strlen(buf)*6;
    drawStr(SCR_W-tw-4,fy+13,buf,lc,C_SURF,1);
  } else {
    drawStr(SCR_W-56,fy+13,"Mega: --",C_CORAL,C_SURF,1);
  }
}

bool drawBackBtn(Touch& t){
  drawButton(112,CNT_Y+4,68,26,C_SURF,C_DCYAN,"< BACK",1);
  return hit(t,112,CNT_Y+4,68,26);
}

// ════════════════════════════════════════════════════════════════════
//  SCREENS
// ════════════════════════════════════════════════════════════════════
void drawBoot(){
  static unsigned long lastDot=0; static uint8_t dotIdx=0;
  if(scrDirty){
    fillScreen(C_BG);
    drawStrCentered(SCR_W/2,72,"BuddyBot",C_CYAN,C_BG,4);
    drawStrCentered(SCR_W/2,120,"Orbital HMI  v2.0  |  Pico",C_LGRAY,C_BG,2);
    drawFastHLine(60,148,SCR_W-120,C_LINE);
    drawStrCentered(SCR_W/2,158,"ST7796S  480x320  |  FT6336U",C_DCYAN,C_BG,1);
    drawStrCentered(SCR_W/2,200,"Waiting for Mega...",C_AMBER,C_BG,2);
    scrDirty=false;
  }
  if(millis()-lastDot>300){
    lastDot=millis(); dotIdx=(dotIdx+1)%6;
    int cx=SCR_W/2-48;
    for(int i=0;i<6;i++) fillCircle(cx+i*20,248,6,(i<=dotIdx)?C_CYAN:C_LINE);
  }
}

void drawMain(Touch& t){
  if(scrDirty){ fillRect(0,CNT_Y,SCR_W,CNT_H,C_BG); scrDirty=false; }
  drawHeader("BuddyBot Dashboard");
  drawFooter();
  const int NW=108,NG=4;
  const int BH=(CNT_H-NG*3)/4;
  const int cy=CNT_Y;
  struct{const char* l;uint16_t c;Screen s;}nav[]={
    {"RADAR",C_CYAN,SCR_RADAR},{"GAMES",C_GREEN,SCR_GAMES},
    {"SENSORS",C_AMBER,SCR_SENSORS},{"INFO",C_PURP,SCR_INFO}};
  for(int i=0;i<4;i++){
    int by=cy+i*(BH+NG);
    drawButton(4,by,NW,BH,C_SURF,nav[i].c,nav[i].l,2);
    if(hit(t,4,by,NW,BH)){curScr=nav[i].s;scrDirty=true;t.pressed=false;return;}
  }
  const int MX=NW+10,MW=118,MH=(CNT_H-NG*2)/3;
  struct{const char* l;uint16_t c;const char* cmd;}modes[]={
    {"NORMAL",C_CYAN,"NORMAL"},{"BODYGUARD",C_AMBER,"BODYGUARD"},{"DOG",C_GREEN,"DOG"}};
  for(int i=0;i<3;i++){
    int by=cy+i*(MH+NG);
    bool act=(T.mode==modes[i].cmd);
    uint16_t bg=act?modes[i].c:C_SURF, fg=act?C_BLACK:modes[i].c;
    fillRoundRect(MX,by,MW,MH,8,bg);
    drawRoundRect(MX,by,MW,MH,8,modes[i].c);
    drawStrCentered(MX+MW/2,by+(MH-8)/2,modes[i].l,fg,bg,1);
    if(hit(t,MX,by,MW,MH)){
      MEGA_SERIAL.print("MODE:");MEGA_SERIAL.println(modes[i].cmd);
      T.mode=modes[i].cmd;t.pressed=false;
    }
  }
  const int TX=MX+MW+8,TW=SCR_W-TX-4;
  fillRect(TX,cy,TW,CNT_H,C_SURF);
  drawRect(TX,cy,TW,CNT_H,C_LINE);
  int row=cy+5;
  auto tr=[&](const char* lbl,const char* val,uint16_t vc){
    int16_t tw=strlen(lbl)*6;
    drawStr(TX+4,row,lbl,C_MGRAY,C_SURF,1);
    drawStr(TX+TW-strlen(val)*6-4,row,val,vc,C_SURF,1);
    drawFastHLine(TX+2,row+11,TW-4,C_LINE);
    row+=15;
  };
  char buf[30];
  snprintf(buf,sizeof(buf),"%.1fC %.0f%%",T.temp,T.hum);   tr("Temp/Hum",buf,C_CYAN);
  snprintf(buf,sizeof(buf),"F:%ldcm",T.dFront);             tr("Front",buf,T.dFront>0&&T.dFront<30?C_CORAL:C_CYAN);
  snprintf(buf,sizeof(buf),"R:%ld L:%ld Ri:%ld",T.dRear,T.dLeft,T.dRight); tr("R/L/Ri",buf,C_CYAN);
  snprintf(buf,sizeof(buf),"%d",T.gas);                     tr("Gas",buf,T.gas>300?C_CORAL:C_MINT);
  tr("Flame",T.flame?"ALERT":"clear",T.flame?C_CORAL:C_MINT);
  tr("PIR/IR",(T.pir?"MOTION":(T.ir?"IR blk":"clear")),(T.pir||T.ir)?C_AMBER:C_MINT);
  snprintf(buf,sizeof(buf),"%.2fA",T.amps);                 tr("Current",buf,C_LGRAY);
  snprintf(buf,sizeof(buf),"%s %s %s",T.r3ok?"R3":"R3:--",T.espok?"ESP":"ESP:--",T.s9ok?"S9":"S9:--");
  tr("Links",buf,C_LGRAY);
  tr("Auto",T.autoM?"ON":"OFF",T.autoM?C_GREEN:C_MGRAY);
}

void drawRadar(Touch& t){
  if(scrDirty){fillRect(0,CNT_Y,SCR_W,CNT_H,C_BG);scrDirty=false;}
  drawHeader("Proximity Radar"); drawFooter();
  if(drawBackBtn(t)){curScr=SCR_MAIN;scrDirty=true;t.pressed=false;return;}
  const int CX=240,CY=CNT_Y+CNT_H/2+4,MR=88;
  for(int r=1;r<=4;r++){
    uint16_t rc=(r==1)?C_CORAL:(r==2)?C_AMBER:C_LINE;
    drawCircle(CX,CY,(MR*r)/4,rc);
  }
  fillCircle(CX,CY,5,C_CYAN);
  auto plot=[&](long d,float ang,const char* l){
    float a=ang*0.01745329f;
    float fr=(d>0&&d<100)?(1.0f-d/100.0f):0;
    int r=(int)(fr*MR),px=CX+(int)(sinf(a)*r),py=CY-(int)(cosf(a)*r);
    uint16_t c=(d>0&&d<25)?C_CORAL:(d>0&&d<50)?C_AMBER:C_MINT;
    if(r>0){fillCircle(px,py,7,c);}
    char db[14];
    if(d>0) snprintf(db,sizeof(db),"%s:%ld",l,d);
    else    snprintf(db,sizeof(db),"%s:--",l);
    int lx=CX+(int)(sinf(a)*(MR+18))-strlen(db)*3;
    int ly=CY-(int)(cosf(a)*(MR+18))-4;
    lx=constrain(lx,2,SCR_W-strlen(db)*6-2);
    ly=constrain(ly,CNT_Y+34,SCR_H-FTR_H-12);
    drawStr(lx,ly,db,c,C_BG,1);
  };
  plot(T.dFront,0,"F"); plot(T.dRear,180,"R");
  plot(T.dLeft,-90,"L"); plot(T.dRight,90,"Ri");
}

void drawGamesMenu(Touch& t){
  if(scrDirty){fillRect(0,CNT_Y,SCR_W,CNT_H,C_BG);scrDirty=false;}
  drawHeader("Games for AJ"); drawFooter();
  if(drawBackBtn(t)){curScr=SCR_MAIN;scrDirty=true;t.pressed=false;return;}
  struct{const char* n;Screen s;uint16_t c;const char* d;}g[]={
    {"COLOURS",SCR_GAME_COLOR,C_RED,"Match the colour!"},
    {"SHAPES",SCR_GAME_SHAPE,C_CYAN,"Find the shape!"},
    {"COUNTING",SCR_GAME_COUNT,C_GREEN,"Count the stars!"}};
  int gw=(SCR_W-24)/3-6,gy=CNT_Y+36,gh=CNT_H-44;
  for(int i=0;i<3;i++){
    int gx=8+i*(gw+8);
    fillRoundRect(gx,gy,gw,gh,14,C_SURF);
    drawRoundRect(gx,gy,gw,gh,14,g[i].c);
    drawStrCentered(gx+gw/2,gy+14,g[i].n,g[i].c,C_SURF,2);
    drawStrCentered(gx+gw/2,gy+gh-16,g[i].d,C_LGRAY,C_SURF,1);
    if(hit(t,gx,gy,gw,gh)){
      gTarget=random(i==2?5:(i==0?4:3));
      if(i==2)gTarget++;
      gScore=0;gFeedbackUntil=0;
      curScr=g[i].s;scrDirty=true;t.pressed=false;return;
    }
  }
}

void gameCorrect(uint8_t s,int newMax){
  gScore++;
  fillRect(0,CNT_Y+34,SCR_W,CNT_H-34,C_MINT);
  drawStrCentered(SCR_W/2,SCR_H/2-20,"CORRECT!",C_BLACK,C_MINT,4);
  char sb[16];snprintf(sb,sizeof(sb),"Score: %d",gScore);
  drawStrCentered(SCR_W/2,SCR_H/2+24,sb,C_BLACK,C_MINT,2);
  gTarget=random(newMax)+(s==SCR_GAME_COUNT?1:0);
  gFeedbackUntil=millis()+800;
}
void gameWrong(){
  fillRect(0,CNT_Y+34,SCR_W,CNT_H-34,C_CORAL);
  drawStrCentered(SCR_W/2,SCR_H/2-20,"Try again!",C_WHITE,C_CORAL,4);
  gFeedbackUntil=millis()+700;
}

void drawGameColor(Touch& t){
  if(millis()<gFeedbackUntil)return;
  if(scrDirty){fillRect(0,CNT_Y,SCR_W,CNT_H,C_BG);scrDirty=false;}
  drawHeader("Colour Match");drawFooter();
  if(drawBackBtn(t)){curScr=SCR_GAMES;scrDirty=true;t.pressed=false;return;}
  char sb[16];snprintf(sb,sizeof(sb),"Score: %d",gScore);
  drawStr(SCR_W-strlen(sb)*6-8,CNT_Y+6,sb,C_CYAN,C_BG,2);
  drawStrCentered(SCR_W/2,CNT_Y+38,"Touch the colour:",C_WHITE,C_BG,2);
  drawStrCentered(SCR_W/2,CNT_Y+64,GCOLNAMES[gTarget],GCOLS[gTarget],C_BG,4);
  int bw=(SCR_W-24)/2-4,bh=50;
  for(int i=0;i<4;i++){
    int bx=8+(i%2)*(bw+8);
    int by=(i<2)?(CNT_Y+118):(CNT_Y+174);
    fillRoundRect(bx,by,bw,bh,10,GCOLS[i]);
    drawRoundRect(bx,by,bw,bh,10,C_WHITE);
    if(hit(t,bx,by,bw,bh)){
      t.pressed=false;
      (i==gTarget)?gameCorrect(SCR_GAME_COLOR,4):gameWrong();
      scrDirty=true;return;
    }
  }
}

void drawGameShape(Touch& t){
  const char* names[]={"CIRCLE","SQUARE","TRIANGLE"};
  if(millis()<gFeedbackUntil)return;
  if(scrDirty){fillRect(0,CNT_Y,SCR_W,CNT_H,C_BG);scrDirty=false;}
  drawHeader("Shape Match");drawFooter();
  if(drawBackBtn(t)){curScr=SCR_GAMES;scrDirty=true;t.pressed=false;return;}
  char sb[16];snprintf(sb,sizeof(sb),"Score: %d",gScore);
  drawStr(SCR_W-strlen(sb)*6-8,CNT_Y+6,sb,C_CYAN,C_BG,2);
  drawStrCentered(SCR_W/2,CNT_Y+36,"Find the shape:",C_WHITE,C_BG,2);
  drawStrCentered(SCR_W/2,CNT_Y+62,names[gTarget],C_CYAN,C_BG,4);
  int bw=(SCR_W-24)/3-4,bh=96,by=CNT_Y+118;
  for(int i=0;i<3;i++){
    int bx=8+i*(bw+6);
    fillRoundRect(bx,by,bw,bh,8,C_SURF);
    drawRoundRect(bx,by,bw,bh,8,C_CYAN);
    int cx2=bx+bw/2,cy2=by+bh/2,r=bh/3-4;
    if(i==0)fillCircle(cx2,cy2,r,C_CYAN);
    else if(i==1)fillRect(cx2-r,cy2-r,r*2,r*2,C_CYAN);
    else fillTriangle(cx2,cy2-r,cx2-r,cy2+r,cx2+r,cy2+r,C_CYAN);
    if(hit(t,bx,by,bw,bh)){
      t.pressed=false;
      (i==gTarget)?gameCorrect(SCR_GAME_SHAPE,3):gameWrong();
      scrDirty=true;return;
    }
  }
}

void drawGameCount(Touch& t){
  if(millis()<gFeedbackUntil)return;
  if(scrDirty){fillRect(0,CNT_Y,SCR_W,CNT_H,C_BG);scrDirty=false;}
  drawHeader("Counting Stars");drawFooter();
  if(drawBackBtn(t)){curScr=SCR_GAMES;scrDirty=true;t.pressed=false;return;}
  char sb[16];snprintf(sb,sizeof(sb),"Score: %d",gScore);
  drawStr(SCR_W-strlen(sb)*6-8,CNT_Y+6,sb,C_CYAN,C_BG,2);
  drawStrCentered(SCR_W/2,CNT_Y+36,"How many stars?",C_WHITE,C_BG,2);
  fillRoundRect(20,CNT_Y+62,SCR_W-40,72,8,C_SURF);
  int spread=(gTarget>1)?((SCR_W-80)/(gTarget-1)):0;
  for(int i=0;i<gTarget;i++){
    int sx=(gTarget>1)?(36+i*spread):(SCR_W/2-3);
    drawStr(sx,CNT_Y+74,"*",C_YLLOW,C_SURF,4);
  }
  int bw=(SCR_W-24)/5-3,bh=52,by=CNT_Y+148;
  for(int i=0;i<5;i++){
    int bx=8+i*(bw+4);
    char nb[3];snprintf(nb,sizeof(nb),"%d",i+1);
    fillRoundRect(bx,by,bw,bh,8,C_SURF);
    drawRoundRect(bx,by,bw,bh,8,C_YLLOW);
    drawStrCentered(bx+bw/2,by+(bh-32)/2,nb,C_YLLOW,C_SURF,4);
    if(hit(t,bx,by,bw,bh)){
      t.pressed=false;
      (i+1==gTarget)?gameCorrect(SCR_GAME_COUNT,5):gameWrong();
      scrDirty=true;return;
    }
  }
}

void drawInfo(Touch& t){
  if(scrDirty){fillRect(0,CNT_Y,SCR_W,CNT_H,C_BG);scrDirty=false;}
  drawHeader("System Info");drawFooter();
  if(drawBackBtn(t)){curScr=SCR_MAIN;scrDirty=true;t.pressed=false;return;}
  int row=CNT_Y+36;
  auto ir=[&](const char* l,const char* v,uint16_t vc){
    drawStr(10,row,l,C_MGRAY,C_BG,1);
    drawStr(210,row,v,vc,C_BG,1);
    drawFastHLine(8,row+12,SCR_W-16,C_LINE);
    row+=17;
  };
  char buf[28];
  ir("Mega FW",T.fw.length()?T.fw.c_str():"--",C_CYAN);
  snprintf(buf,sizeof(buf),"%.2f V",T.volt); ir("Battery",buf,T.volt>7?C_MINT:C_CORAL);
  snprintf(buf,sizeof(buf),"%d %%",T.pct);   ir("Percent",buf,T.pct>50?C_MINT:C_AMBER);
  snprintf(buf,sizeof(buf),"%.2f A",T.amps); ir("Current",buf,C_LGRAY);
  ir("R3 Motors",T.r3ok?"OK":"FAIL",T.r3ok?C_MINT:C_CORAL);
  ir("ESP32",T.espok?"OK":"WAIT",T.espok?C_MINT:C_AMBER);
  ir("S9 App",T.s9ok?"OK":"WAIT",T.s9ok?C_MINT:C_AMBER);
  ir("Auto mode",T.autoM?"ON":"OFF",T.autoM?C_GREEN:C_MGRAY);
  snprintf(buf,sizeof(buf),"%lu s",millis()/1000); ir("Uptime",buf,C_LGRAY);
  snprintf(buf,sizeof(buf),"%lu s ago",(millis()-lastMegaRx)/1000);
  ir("Last Mega",buf,megaLinked?C_MINT:C_CORAL);
}

void drawAlert(Touch& t){
  const int AX=48,AY=64,AW=SCR_W-96,AH=SCR_H-128;
  fillRoundRect(AX,AY,AW,AH,12,alertCol);
  drawRoundRect(AX,AY,AW,AH,12,C_WHITE);
  drawStrCentered(AX+AW/2,AY+20,alertTitle.c_str(),C_WHITE,alertCol,3);
  drawStrCentered(AX+AW/2,AY+68,alertMsg.c_str(),C_WHITE,alertCol,2);
  int bx=AX+(AW-110)/2,by=AY+AH-50;
  drawButton(bx,by,110,34,C_WHITE,alertCol,"DISMISS",2);
  if(hit(t,bx,by,110,34)||(alertTs>0&&millis()-alertTs>10000)){
    alertOn=false;alertTs=0;scrDirty=true;t.pressed=false;
  }
}

// ════════════════════════════════════════════════════════════════════
//  MEGA PROTOCOL PARSER
// ════════════════════════════════════════════════════════════════════
void raisAlert(const char* title,const char* msg,uint16_t col){
  alertTitle=title;alertMsg=msg;alertCol=col;alertOn=true;alertTs=millis();
}
void parseStat(const String& s){
  String f[13];int n=0,st=5;
  for(int i=5;i<=(int)s.length()&&n<13;i++){
    if(i==(int)s.length()||s[i]==':'){f[n++]=s.substring(st,i);st=i+1;}
  }
  if(n<11)return;
  T.gas=f[0].toInt();T.temp=f[1].toFloat();T.hum=f[2].toFloat();
  T.haz=f[3].toInt();T.pir=f[4].toInt();T.tilt=f[5].toInt();
  T.flame=f[6].toInt();T.ir=f[7].toInt();
  T.volt=f[8].toFloat();T.pct=f[9].toInt();T.amps=f[10].toFloat();
  if(n>11)T.boost=f[11].toFloat();
}
void parseUS(const String& s){
  String tmp=s.substring(3);
  String f[4];int n=0,st=0;
  for(int i=0;i<=(int)tmp.length()&&n<4;i++){
    if(i==(int)tmp.length()||tmp[i]==','){f[n++]=tmp.substring(st,i);st=i+1;}
  }
  if(n<4)return;
  T.dFront=f[0].toInt();T.dRear=f[1].toInt();
  T.dLeft=f[2].toInt();T.dRight=f[3].toInt();
}
void parseStatus(const String& s){
  T.estop=(s.indexOf("ESTOP:YES")>=0);
  T.autoM=(s.indexOf("AUTO:ON")>=0);
  T.r3ok =(s.indexOf("R3:OK")>=0);
  T.espok=(s.indexOf("ESP:OK")>=0);
  T.s9ok =(s.indexOf("S9:OK")>=0);
  int fi=s.indexOf("FW:");
  if(fi>=0){int fe=s.indexOf('|',fi);T.fw=(fe>0)?s.substring(fi+3,fe):s.substring(fi+3);}
}
void handleMegaLine(String& line){
  line.trim();if(!line.length())return;
  lastMegaRx=millis();megaLinked=true;
  if(line.startsWith("STAT:"))          parseStat(line);
  else if(line.startsWith("US:"))       parseUS(line);
  else if(line.startsWith("STATUS|"))   parseStatus(line);
  else if(line.startsWith("MODE:"))     T.mode=line.substring(5);
  else if(line=="PING")                 MEGA_SERIAL.println("PONG");
  else if(line.startsWith("MEGA_READY|")||line.startsWith("SYSTEM|READY|")){
    parseStatus(line);MEGA_SERIAL.println("SENSOR_STATUS");
  }
  else if(line.startsWith("BAT:WARN"))  raisAlert("Battery Low","Charge soon",C_AMBER);
  else if(line.startsWith("BAT:LOW"))   raisAlert("Battery Critical","Plug in NOW",C_CORAL);
  else if(line.startsWith("SAFETY:FLAME"))raisAlert("FLAME DETECTED","Check area",C_CORAL);
  else if(line.startsWith("SAFETY:TILT"))raisAlert("Robot Tilted","Check robot",C_AMBER);
  else if(line.startsWith("SAFETY:GAS")) raisAlert("Gas Detected","Ventilate",C_AMBER);
}
void handleMegaSerial(){
  int budget=64;
  while(MEGA_SERIAL.available()&&budget-->0){
    char c=MEGA_SERIAL.read();
    if(c=='\n'){handleMegaLine(megaBuf);megaBuf="";}
    else if(c!='\r'){megaBuf+=c;if(megaBuf.length()>128)megaBuf="";}
  }
}

// ════════════════════════════════════════════════════════════════════
//  SETUP & LOOP
// ════════════════════════════════════════════════════════════════════
void setup(){
  delay(500);
  displayInit();

  // Diagnostic flash — colour at each setup step so we can see where it crashes
  fillScreen(C_RED);   delay(300);   // Step 1 - display OK
  fillScreen(C_GREEN); delay(300);   // Step 2 - display OK

  fillScreen(C_AMBER);               // STEP 3: Touch RST
  pinMode(PIN_CTP_RST,OUTPUT);
  digitalWrite(PIN_CTP_RST,LOW);delay(20);
  digitalWrite(PIN_CTP_RST,HIGH);delay(100);
  pinMode(PIN_CTP_INT,INPUT);
  delay(200);

  fillScreen(C_CYAN);                // STEP 4: Wire init
  Wire.setSDA(PIN_CTP_SDA);
  Wire.setSCL(PIN_CTP_SCL);
  Wire.begin();
  delay(200);                         // No Wire.setClock — default 100kHz is safe

  fillScreen(C_PURP);                // STEP 5: Serial1 init
  Serial1.setTX(0);Serial1.setRX(1);
  MEGA_SERIAL.begin(115200);
  delay(200);

  fillScreen(C_MINT);                // STEP 6: all setup done
  delay(400);
  fillScreen(C_BG);                  // Normal background — entering loop

  MEGA_SERIAL.println("PONG");
  MEGA_SERIAL.println("PING_R4:0");
  randomSeed(analogRead(A0));
  scrDirty=true;
}

void loop(){
  handleMegaSerial();
  if(millis()-lastPing>5000){
    lastPing=millis();
    MEGA_SERIAL.print("PING_R4:");MEGA_SERIAL.println(pingSeq++);
    if(pingSeq>9999)pingSeq=0;
  }
  if(megaLinked&&millis()-lastMegaRx>12000) megaLinked=false;
  if(curScr==SCR_BOOT&&megaLinked){curScr=SCR_MAIN;scrDirty=true;}

  Touch t={0,0,false};
  if(millis()-lastTouchMs>80&&!digitalRead(PIN_CTP_INT)){
    t=readTouch();if(t.pressed)lastTouchMs=millis();
  }
  if(hit(t,4,SCR_H-FTR_H+4,88,FTR_H-8)){
    if(T.estop){MEGA_SERIAL.println("ESTOP_CLEAR");T.estop=false;}
    else{MEGA_SERIAL.println("EMERGENCY_STOP");T.estop=true;}
    scrDirty=true;t.pressed=false;
  }
  if(alertOn){drawHeader("Alert");drawFooter();drawAlert(t);return;}
  if(curScr==SCR_MAIN&&millis()-lastTelemDraw>1000){lastTelemDraw=millis();scrDirty=true;}
  switch(curScr){
    case SCR_BOOT:       drawBoot();       break;
    case SCR_MAIN:       drawMain(t);      break;
    case SCR_RADAR:      drawRadar(t);     break;
    case SCR_GAMES:      drawGamesMenu(t); break;
    case SCR_GAME_COLOR: drawGameColor(t); break;
    case SCR_GAME_SHAPE: drawGameShape(t); break;
    case SCR_GAME_COUNT: drawGameCount(t); break;
    case SCR_INFO:       drawInfo(t);      break;
    default:             drawMain(t);      break;
  }
}
