/*
 * ════════════════════════════════════════════════════════════════════
 *  BUDDYBOT  —  Pico RP2040  ·  Orbital HMI  ·  V3.0
 *  Portrait 320×480  |  Direct SPI  |  No external display library
 * ════════════════════════════════════════════════════════════════════
 *  WIRING
 *  SPI0   GP16=MISO  GP17=CS  GP18=SCK  GP19=MOSI  GP20=RST  GP21=DC  GP22=BL
 *  I2C1   GP26=SDA   GP27=SCL  GP28=INT  GP15=RST  (FT6336U touch)
 *  UART0  GP0=TX→Mega19  GP1=RX←Mega18  (voltage divider on RX)
 * ════════════════════════════════════════════════════════════════════
 */
#include <SPI.h>
#include <Wire.h>

// Forward declarations
struct Touch { int16_t x, y; bool pressed; };
Touch readTouch();
bool  hit(const Touch& t, int16_t x, int16_t y, int16_t w, int16_t h);
void  drawMain(Touch& t);
void  drawRadar(Touch& t);
void  drawGamesMenu(Touch& t);
void  drawGameColor(Touch& t);
void  drawGameShape(Touch& t);
void  drawGameCount(Touch& t);
void  drawInfo(Touch& t);
void  drawSensors(Touch& t);
void  drawAlert(Touch& t);
bool  drawBackBtn(Touch& t);

// ── Pins ─────────────────────────────────────────────────────────────
#define PIN_BL       22
#define PIN_CS       17
#define PIN_DC       21
#define PIN_RST      20
#define PIN_CTP_SDA  26
#define PIN_CTP_SCL  27
#define PIN_CTP_INT  28
#define PIN_CTP_RST  15

// ── ST7796S commands ──────────────────────────────────────────────────
#define ST_SWRESET 0x01
#define ST_SLPOUT  0x11
#define ST_COLMOD  0x3A
#define ST_MADCTL  0x36
#define ST_DISPON  0x29
#define ST_CASET   0x2A
#define ST_PASET   0x2B
#define ST_RAMWR   0x2C

// ── Portrait 320×480 ──────────────────────────────────────────────────
#define SCR_W   320
#define SCR_H   480

// ── Colour palette (RGB565) ───────────────────────────────────────────
#define C_BG      0x0841u   // #080808 near-black
#define C_SURF    0x1082u   // #101010 card surface
#define C_SURF2   0x18C3u   // #181818 lifted surface
#define C_LINE    0x2104u   // subtle divider
#define C_CYAN    0x07FFu   // electric cyan — primary accent
#define C_DCYAN   0x03EFu   // dim cyan
#define C_MINT    0x3FE6u   // mint green — OK/safe
#define C_AMBER   0xFD20u   // amber — warning
#define C_CORAL   0xF944u   // coral red — danger
#define C_PURP    0x801Fu   // purple — info
#define C_MAG     0xF81Fu   // magenta — unhinged
#define C_YLLOW   0xFFE0u   // yellow — stars
#define C_WHITE   0xFFFFu
#define C_LGRAY   0x8C71u
#define C_MGRAY   0x4228u
#define C_DGRAY   0x2104u
#define C_BLACK   0x0000u
#define C_RED     0xF800u
#define C_GREEN   0x07E0u
#define C_BLUE    0x001Fu

// ── Layout constants ──────────────────────────────────────────────────
#define HDR_H   54    // header
#define FTR_H   56    // footer (E-STOP lives here)
#define CNT_Y   HDR_H
#define CNT_H   (SCR_H - HDR_H - FTR_H)   // 370 px

// ════════════════════════════════════════════════════════════════════
//  LOW-LEVEL SPI + DISPLAY INIT
// ════════════════════════════════════════════════════════════════════
static SPISettings _spi(20000000, MSBFIRST, SPI_MODE0);

inline void _cmd(uint8_t c){
  digitalWrite(PIN_DC,LOW); digitalWrite(PIN_CS,LOW);
  SPI.transfer(c); digitalWrite(PIN_CS,HIGH);
}
inline void _dat(uint8_t d){
  digitalWrite(PIN_DC,HIGH); digitalWrite(PIN_CS,LOW);
  SPI.transfer(d); digitalWrite(PIN_CS,HIGH);
}

void setWindow(int16_t x0,int16_t y0,int16_t x1,int16_t y1){
  _cmd(ST_CASET); _dat(x0>>8); _dat(x0); _dat(x1>>8); _dat(x1);
  _cmd(ST_PASET); _dat(y0>>8); _dat(y0); _dat(y1>>8); _dat(y1);
  _cmd(ST_RAMWR);
}

void fillRect(int16_t x,int16_t y,int16_t w,int16_t h,uint16_t c){
  if(w<=0||h<=0) return;
  if(x<0){w+=x;x=0;} if(y<0){h+=y;y=0;}
  if(x+w>SCR_W) w=SCR_W-x; if(y+h>SCR_H) h=SCR_H-y;
  if(w<=0||h<=0) return;
  setWindow(x,y,x+w-1,y+h-1);
  digitalWrite(PIN_DC,HIGH); digitalWrite(PIN_CS,LOW);
  uint32_t n=(uint32_t)w*h;
  uint8_t hi=c>>8, lo=c&0xFF;
  for(uint32_t i=0;i<n;i++){SPI.transfer(hi);SPI.transfer(lo);}
  digitalWrite(PIN_CS,HIGH);
}

void fillScreen(uint16_t c){ fillRect(0,0,SCR_W,SCR_H,c); }

void drawPixel(int16_t x,int16_t y,uint16_t c){
  if(x<0||x>=SCR_W||y<0||y>=SCR_H) return;
  setWindow(x,y,x,y); _dat(c>>8); _dat(c&0xFF);
}

// Gradient fill — two colours, vertical blend over h rows
void fillGrad(int16_t x,int16_t y,int16_t w,int16_t h,uint16_t c1,uint16_t c2){
  uint8_t r1=c1>>11,g1=(c1>>5)&0x3F,b1=c1&0x1F;
  uint8_t r2=c2>>11,g2=(c2>>5)&0x3F,b2=c2&0x1F;
  for(int16_t i=0;i<h;i++){
    uint8_t r=r1+(int)(r2-r1)*i/h;
    uint8_t g=g1+(int)(g2-g1)*i/h;
    uint8_t b=b1+(int)(b2-b1)*i/h;
    uint16_t col=(r<<11)|(g<<5)|b;
    fillRect(x,y+i,w,1,col);
  }
}

void displayInit(){
  pinMode(PIN_CS,OUTPUT);  digitalWrite(PIN_CS,HIGH);
  pinMode(PIN_DC,OUTPUT);  digitalWrite(PIN_DC,HIGH);
  pinMode(PIN_RST,OUTPUT);
  pinMode(PIN_BL,OUTPUT);  digitalWrite(PIN_BL,HIGH);
  digitalWrite(PIN_RST,HIGH); delay(20);
  digitalWrite(PIN_RST,LOW);  delay(50);
  digitalWrite(PIN_RST,HIGH); delay(150);
  SPI.setTX(19); SPI.setRX(16); SPI.setSCK(18);
  SPI.begin();
  SPI.beginTransaction(_spi);
  _cmd(ST_SWRESET); delay(120);
  _cmd(ST_SLPOUT);  delay(120);
  _cmd(ST_COLMOD);  _dat(0x55);   // 16-bit RGB565
  // MADCTL 0x48: MX | BGR  — portrait, left-right corrected
  _cmd(ST_MADCTL);  _dat(0x48);
  _cmd(ST_DISPON);  delay(50);
}

// ════════════════════════════════════════════════════════════════════
//  FONT (6×8 bitmap — built-in, no library)
// ════════════════════════════════════════════════════════════════════
static const uint8_t F6x8[][5] PROGMEM = {
  {0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x5F,0x00,0x00},{0x00,0x07,0x00,0x07,0x00},
  {0x14,0x7F,0x14,0x7F,0x14},{0x24,0x2A,0x7F,0x2A,0x12},{0x23,0x13,0x08,0x64,0x62},
  {0x36,0x49,0x55,0x22,0x50},{0x00,0x05,0x03,0x00,0x00},{0x00,0x1C,0x22,0x41,0x00},
  {0x00,0x41,0x22,0x1C,0x00},{0x08,0x2A,0x1C,0x2A,0x08},{0x08,0x08,0x3E,0x08,0x08},
  {0x00,0x50,0x30,0x00,0x00},{0x08,0x08,0x08,0x08,0x08},{0x00,0x60,0x60,0x00,0x00},
  {0x20,0x10,0x08,0x04,0x02},{0x3E,0x51,0x49,0x45,0x3E},{0x00,0x42,0x7F,0x40,0x00},
  {0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4B,0x31},{0x18,0x14,0x12,0x7F,0x10},
  {0x27,0x45,0x45,0x45,0x39},{0x3C,0x4A,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},
  {0x36,0x49,0x49,0x49,0x36},{0x06,0x49,0x49,0x29,0x1E},{0x00,0x36,0x36,0x00,0x00},
  {0x00,0x56,0x36,0x00,0x00},{0x08,0x14,0x22,0x41,0x00},{0x14,0x14,0x14,0x14,0x14},
  {0x00,0x41,0x22,0x14,0x08},{0x02,0x01,0x51,0x09,0x06},{0x32,0x49,0x79,0x41,0x3E},
  {0x7E,0x11,0x11,0x11,0x7E},{0x7F,0x49,0x49,0x49,0x36},{0x3E,0x41,0x41,0x41,0x22},
  {0x7F,0x41,0x41,0x22,0x1C},{0x7F,0x49,0x49,0x49,0x41},{0x7F,0x09,0x09,0x09,0x01},
  {0x3E,0x41,0x49,0x49,0x7A},{0x7F,0x08,0x08,0x08,0x7F},{0x00,0x41,0x7F,0x41,0x00},
  {0x20,0x40,0x41,0x3F,0x01},{0x7F,0x08,0x14,0x22,0x41},{0x7F,0x40,0x40,0x40,0x40},
  {0x7F,0x02,0x0C,0x02,0x7F},{0x7F,0x04,0x08,0x10,0x7F},{0x3E,0x41,0x41,0x41,0x3E},
  {0x7F,0x09,0x09,0x09,0x06},{0x3E,0x41,0x51,0x21,0x5E},{0x7F,0x09,0x19,0x29,0x46},
  {0x46,0x49,0x49,0x49,0x31},{0x01,0x01,0x7F,0x01,0x01},{0x3F,0x40,0x40,0x40,0x3F},
  {0x1F,0x20,0x40,0x20,0x1F},{0x3F,0x40,0x38,0x40,0x3F},{0x63,0x14,0x08,0x14,0x63},
  {0x07,0x08,0x70,0x08,0x07},{0x61,0x51,0x49,0x45,0x43},{0x00,0x7F,0x41,0x41,0x00},
  {0x02,0x04,0x08,0x10,0x20},{0x00,0x41,0x41,0x7F,0x00},{0x04,0x02,0x01,0x02,0x04},
  {0x40,0x40,0x40,0x40,0x40},{0x00,0x01,0x02,0x04,0x00},{0x20,0x54,0x54,0x54,0x78},
  {0x7F,0x48,0x44,0x44,0x38},{0x38,0x44,0x44,0x44,0x20},{0x38,0x44,0x44,0x48,0x7F},
  {0x38,0x54,0x54,0x54,0x18},{0x08,0x7E,0x09,0x01,0x02},{0x0C,0x52,0x52,0x52,0x3E},
  {0x7F,0x08,0x04,0x04,0x78},{0x00,0x44,0x7D,0x40,0x00},{0x20,0x40,0x44,0x3D,0x00},
  {0x7F,0x10,0x28,0x44,0x00},{0x00,0x41,0x7F,0x40,0x00},{0x7C,0x04,0x18,0x04,0x78},
  {0x7C,0x08,0x04,0x04,0x78},{0x38,0x44,0x44,0x44,0x38},{0x7C,0x14,0x14,0x14,0x08},
  {0x08,0x14,0x14,0x18,0x7C},{0x7C,0x08,0x04,0x04,0x08},{0x48,0x54,0x54,0x54,0x20},
  {0x04,0x3F,0x44,0x40,0x20},{0x3C,0x40,0x40,0x40,0x3C},{0x1C,0x20,0x40,0x20,0x1C},
  {0x3C,0x40,0x30,0x40,0x3C},{0x44,0x28,0x10,0x28,0x44},{0x0C,0x50,0x50,0x50,0x3C},
  {0x44,0x64,0x54,0x4C,0x44},{0x00,0x08,0x36,0x41,0x00},{0x00,0x00,0x7F,0x00,0x00},
  {0x00,0x41,0x36,0x08,0x00},{0x08,0x04,0x08,0x10,0x08},
};

void drawChar(int16_t x,int16_t y,char ch,uint16_t fg,uint16_t bg,uint8_t sz){
  if(ch<0x20||ch>0x7E) ch='?';
  const uint8_t* g=F6x8[ch-0x20];
  for(int8_t col=0;col<6;col++){
    uint8_t line=(col<5)?pgm_read_byte(&g[col]):0;
    for(int8_t row=0;row<8;row++){
      uint16_t c=(line&1)?fg:bg;
      if(c!=0xFFFF) fillRect(x+col*sz,y+row*sz,sz,sz,c);
      line>>=1;
    }
  }
}

void drawStr(int16_t x,int16_t y,const char* s,uint16_t fg,uint16_t bg,uint8_t sz){
  while(*s){drawChar(x,y,*s++,fg,bg,sz);x+=6*sz;}
}

void drawStrC(int16_t cx,int16_t y,const char* s,uint16_t fg,uint16_t bg,uint8_t sz){
  drawStr(cx-(int16_t)(strlen(s)*6*sz/2),y,s,fg,bg,sz);
}

int16_t strW(const char* s,uint8_t sz=1){ return strlen(s)*6*sz; }

// ════════════════════════════════════════════════════════════════════
//  PRIMITIVES
// ════════════════════════════════════════════════════════════════════
void drawFastHLine(int16_t x,int16_t y,int16_t w,uint16_t c){fillRect(x,y,w,1,c);}
void drawFastVLine(int16_t x,int16_t y,int16_t h,uint16_t c){fillRect(x,y,1,h,c);}

void drawRoundRect(int16_t x,int16_t y,int16_t w,int16_t h,int16_t r,uint16_t c){
  drawFastHLine(x+r,y,w-2*r,c); drawFastHLine(x+r,y+h-1,w-2*r,c);
  drawFastVLine(x,y+r,h-2*r,c); drawFastVLine(x+w-1,y+r,h-2*r,c);
}
void fillRoundRect(int16_t x,int16_t y,int16_t w,int16_t h,int16_t r,uint16_t c){
  fillRect(x+r,y,w-2*r,h,c);
  fillRect(x,y+r,r,h-2*r,c);
  fillRect(x+w-r,y+r,r,h-2*r,c);
}
void fillCircle(int16_t cx,int16_t cy,int16_t r,uint16_t c){
  for(int16_t dy=-r;dy<=r;dy++){
    int16_t dx=(int16_t)sqrtf((float)(r*r-dy*dy));
    fillRect(cx-dx,cy+dy,dx*2+1,1,c);
  }
}
void drawCircle(int16_t cx,int16_t cy,int16_t r,uint16_t c){
  int16_t x=r,y=0,e=0;
  while(x>=y){
    drawPixel(cx+x,cy+y,c);drawPixel(cx+y,cy+x,c);
    drawPixel(cx-y,cy+x,c);drawPixel(cx-x,cy+y,c);
    drawPixel(cx-x,cy-y,c);drawPixel(cx-y,cy-x,c);
    drawPixel(cx+y,cy-x,c);drawPixel(cx+x,cy-y,c);
    if(e<=0){y++;e+=2*y+1;} if(e>0){x--;e-=2*x+1;}
  }
}
void fillTriangle(int16_t x0,int16_t y0,int16_t x1,int16_t y1,int16_t x2,int16_t y2,uint16_t c){
  if(y0>y1){int16_t t=y0;y0=y1;y1=t;t=x0;x0=x1;x1=t;}
  if(y1>y2){int16_t t=y1;y1=y2;y2=t;t=x1;x1=x2;x2=t;}
  if(y0>y1){int16_t t=y0;y0=y1;y1=t;t=x0;x0=x1;x1=t;}
  for(int16_t y=y0;y<=y2;y++){
    int16_t a=(y<y1)?x0+(x1-x0)*(y-y0)/(y1-y0):x1+(x2-x1)*(y-y1)/(y2-y1);
    int16_t b=x0+(x2-x0)*(y-y0)/(y2-y0);
    if(a>b){int16_t t=a;a=b;b=t;}
    drawFastHLine(a,y,b-a+1,c);
  }
}

// ════════════════════════════════════════════════════════════════════
//  PREMIUM UI COMPONENTS
// ════════════════════════════════════════════════════════════════════

// Card with optional accent bar on left edge
void drawCard(int16_t x,int16_t y,int16_t w,int16_t h,uint16_t accent=0){
  fillRoundRect(x,y,w,h,6,C_SURF);
  drawRoundRect(x,y,w,h,6,C_LINE);
  if(accent) fillRect(x,y+6,3,h-12,accent);
}

// Pill badge
void drawBadge(int16_t x,int16_t y,int16_t w,int16_t h,uint16_t bg,const char* txt,uint8_t sz=1){
  fillRoundRect(x,y,w,h,h/2,bg);
  drawStrC(x+w/2,y+(h-8*sz)/2,txt,C_BLACK,bg,sz);
}

// Glowing button — filled with subtle gradient + bright border
void drawBtn(int16_t x,int16_t y,int16_t w,int16_t h,uint16_t col,const char* lbl,uint8_t sz=2,bool active=false){
  uint16_t bg = active ? col : C_SURF2;
  uint16_t fg = active ? C_BLACK : col;
  fillGrad(x+1,y+1,w-2,h-2,bg,active?col:C_SURF);
  drawRoundRect(x,y,w,h,7,active?col:C_DGRAY);
  if(active) drawRoundRect(x+1,y+1,w-2,h-2,6,col);  // inner glow
  drawStrC(x+w/2,y+(h-8*sz)/2,lbl,fg,0xFFFF,sz);
}

// Thin separator line with label
void drawDivider(int16_t y,const char* lbl=nullptr){
  drawFastHLine(0,y,SCR_W,C_LINE);
  if(lbl){
    int16_t tw=strW(lbl);
    fillRect(SCR_W/2-tw/2-4,y-4,tw+8,9,C_BG);
    drawStrC(SCR_W/2,y-4,lbl,C_MGRAY,C_BG,1);
  }
}

// Value row — label left, value right, hairline below
void valueRow(int16_t x,int16_t y,int16_t w,const char* lbl,const char* val,uint16_t vc,uint16_t bg=C_SURF){
  drawStr(x+8,y+4,lbl,C_MGRAY,bg,1);
  drawStr(x+w-strW(val)-8,y+4,val,vc,bg,1);
  drawFastHLine(x+4,y+17,w-8,C_LINE);
}

// Touch hit test
bool hit(const Touch& t,int16_t x,int16_t y,int16_t w,int16_t h){
  return t.pressed&&t.x>=x&&t.x<x+w&&t.y>=y&&t.y<y+h;
}

// ════════════════════════════════════════════════════════════════════
//  APP STATE
// ════════════════════════════════════════════════════════════════════
struct Telemetry {
  int   gas=0; float temp=0,hum=0;
  bool  haz=false,pir=false,tilt=false,flame=false,ir=false;
  float volt=8.4f; int pct=100; float amps=0,boost=0;
  long  dFront=-1,dRear=-1,dLeft=-1,dRight=-1;
  bool  estop=false,autoM=false;
  String mode="NORMAL",fw="";
  bool  r3ok=false,espok=false,s9ok=false;
} T;

struct SensFlags { bool dht=true,gas=true,flame=true,pir=false,tilt=true,ir=true,us=true,cur=true; } SF;

enum Screen { SCR_BOOT,SCR_MAIN,SCR_RADAR,SCR_GAMES,
              SCR_GAME_COLOR,SCR_GAME_SHAPE,SCR_GAME_COUNT,
              SCR_INFO,SCR_SENSORS };
Screen curScr=SCR_BOOT;
bool   scrDirty=true;

bool   alertOn=false;
String alertTitle="",alertMsg="";
uint16_t alertCol=C_CORAL;
unsigned long alertTs=0;

#define MEGA_SERIAL  Serial1
String        megaBuf="";
bool          megaLinked=false;
unsigned long lastMegaRx=0,lastPing=0;
uint16_t      pingSeq=0;

// Games
const uint16_t GCOLS[]={C_RED,C_GREEN,C_BLUE,C_YLLOW};
const char*    GCNAMES[]={"RED","GREEN","BLUE","YELLOW"};
int   gTarget=0,gScore=0;
unsigned long gFeedbackUntil=0;

unsigned long lastTelemDraw=0,lastTouchMs=0;

// ════════════════════════════════════════════════════════════════════
//  TOUCH  (FT6336U via Wire1 = I2C1)
// ════════════════════════════════════════════════════════════════════
#define CTP_ADDR 0x38
Touch readTouch(){
  Touch t={0,0,false};
  Wire1.beginTransmission(CTP_ADDR);
  Wire1.write(0x02);
  if(Wire1.endTransmission(false)!=0) return t;
  Wire1.requestFrom(CTP_ADDR,6);
  if(Wire1.available()<6) return t;
  uint8_t n=Wire1.read()&0x0F;
  uint8_t xH=Wire1.read(),xL=Wire1.read();
  uint8_t yH=Wire1.read(),yL=Wire1.read();
  Wire1.read();
  if(n==0||n>2) return t;
  // FT6336U portrait coords match display directly
  t.x=((xH&0x0F)<<8)|xL;
  t.y=((yH&0x0F)<<8)|yL;
  // Clamp to screen
  t.x=constrain(t.x,0,SCR_W-1);
  t.y=constrain(t.y,0,SCR_H-1);
  t.pressed=true;
  return t;
}

// ════════════════════════════════════════════════════════════════════
//  CHROME — Header & Footer (drawn on every screen)
// ════════════════════════════════════════════════════════════════════
void drawHeader(const char* title){
  // Deep gradient header
  fillGrad(0,0,SCR_W,HDR_H,0x0821u,C_BG);
  drawFastHLine(0,HDR_H-1,SCR_W,C_CYAN);

  // Mode badge — left
  uint16_t mc=C_CYAN;
  if(T.estop)              mc=C_CORAL;
  else if(T.mode=="BODYGUARD") mc=C_AMBER;
  else if(T.mode=="DOG")       mc=C_MINT;
  else if(T.mode=="UNHINGED")  mc=C_MAG;
  const char* ms=T.estop?"E-STOP":T.mode.c_str();
  drawBadge(6,14,strW(ms,1)+12,22,mc,ms,1);

  // Title — centre
  drawStrC(SCR_W/2,(HDR_H-16)/2,title,C_WHITE,0xFFFF,2);

  // Battery + link dot — right
  uint16_t bc=T.pct>50?C_MINT:(T.pct>20?C_AMBER:C_CORAL);
  char bat[8]; snprintf(bat,sizeof(bat),"%d%%",T.pct);
  drawStr(SCR_W-strW(bat)-18,(HDR_H-8)/2,bat,bc,0xFFFF,1);
  fillCircle(SCR_W-8,HDR_H/2,5,megaLinked?C_MINT:C_CORAL);
  if(megaLinked) drawCircle(SCR_W-8,HDR_H/2,6,0x03E0u);
}

void drawFooter(){
  int fy=SCR_H-FTR_H;
  fillGrad(0,fy,SCR_W,FTR_H,C_BG,0x0821u);
  drawFastHLine(0,fy,SCR_W,C_LINE);

  // E-STOP pill — always left
  uint16_t ec=T.estop?C_AMBER:C_CORAL;
  const char* el=T.estop?"RESUME":"E-STOP";
  drawBadge(8,fy+10,82,36,ec,el,1);

  // Hazard chips — centre
  int ix=100;
  if(T.flame){ drawBadge(ix,fy+14,44,24,C_CORAL,"FLAME",1); ix+=50; }
  if(T.tilt)  { drawBadge(ix,fy+14,36,24,C_AMBER,"TILT",1); ix+=42; }

  // Mega link age — right
  char buf[16];
  if(megaLinked){
    unsigned long age=(millis()-lastMegaRx)/1000;
    if(age<5) snprintf(buf,sizeof(buf),"LIVE");
    else      snprintf(buf,sizeof(buf),"%lus",age);
    uint16_t lc=age<5?C_MINT:C_AMBER;
    drawStr(SCR_W-strW(buf)-8,fy+22,buf,lc,0xFFFF,1);
    drawStr(SCR_W-strW(buf)-8-strW("Mega ")-2,fy+22,"Mega",C_MGRAY,0xFFFF,1);
  } else {
    drawStr(SCR_W-50,fy+22,"No Mega",C_CORAL,0xFFFF,1);
  }
}

bool drawBackBtn(Touch& t){
  int fy=SCR_H-FTR_H;
  // Back arrow button reuses right side of footer
  drawBadge(SCR_W-72,fy+10,64,36,C_SURF2,"< BACK",1);
  return hit(t,SCR_W-72,fy+10,64,36);
}

// ════════════════════════════════════════════════════════════════════
//  BOOT SCREEN
// ════════════════════════════════════════════════════════════════════
void drawBoot(){
  static unsigned long lastDot=0; static uint8_t di=0;
  if(scrDirty){
    fillScreen(C_BG);
    // Logo area — top third
    fillGrad(0,0,SCR_W,160,0x0821u,C_BG);
    drawStrC(SCR_W/2,52,"BuddyBot",C_CYAN,0xFFFF,4);
    drawStrC(SCR_W/2,90,"Orbital HMI  v3.0",C_LGRAY,0xFFFF,1);
    drawFastHLine(40,110,SCR_W-80,C_LINE);
    drawStrC(SCR_W/2,120,"For AJ  ♡",C_DCYAN,0xFFFF,1);

    // Hardware info card
    drawCard(16,148,SCR_W-32,72);
    drawStrC(SCR_W/2,160,"Hardware",C_MGRAY,0xFFFF,1);
    drawStrC(SCR_W/2,178,"ST7796S  320×480  SPI",C_LGRAY,0xFFFF,1);
    drawStrC(SCR_W/2,194,"FT6336U  Capacitive Touch",C_LGRAY,0xFFFF,1);
    drawStrC(SCR_W/2,210,"RP2040  Raspberry Pi Pico",C_LGRAY,0xFFFF,1);

    drawStrC(SCR_W/2,250,"Waiting for Mega...",C_AMBER,0xFFFF,2);
    scrDirty=false;
  }
  // Animated dot strip
  if(millis()-lastDot>250){ lastDot=millis(); di=(di+1)%6; }
  int dx=SCR_W/2-38;
  for(int i=0;i<6;i++) fillCircle(dx+i*16,290,i==di?6:3,i<=di?C_CYAN:C_DGRAY);
}

// ════════════════════════════════════════════════════════════════════
//  MAIN DASHBOARD
// ════════════════════════════════════════════════════════════════════
void drawMain(Touch& t){
  if(scrDirty){ fillRect(0,CNT_Y,SCR_W,CNT_H,C_BG); scrDirty=false; }
  drawHeader("Dashboard");
  drawFooter();

  int y=CNT_Y+6, pad=8;

  // ── MODE SELECTOR — 3 wide buttons ───────────────────────────────
  struct { const char* l; uint16_t c; const char* cmd; } modes[]={
    {"NORMAL",C_CYAN,"NORMAL"},
    {"BODYGUARD",C_AMBER,"BODYGUARD"},
    {"DOG GUARD",C_MINT,"DOG"},
  };
  int mw=(SCR_W-pad*4)/3;
  for(int i=0;i<3;i++){
    int mx=pad+i*(mw+pad);
    bool act=(T.mode==modes[i].cmd);
    drawBtn(mx,y,mw,40,modes[i].c,modes[i].l,1,act);
    if(hit(t,mx,y,mw,40)){
      MEGA_SERIAL.print("MODE:"); MEGA_SERIAL.println(modes[i].cmd);
      T.mode=modes[i].cmd; t.pressed=false;
    }
  }
  y+=48;

  // ── TELEMETRY CARD ────────────────────────────────────────────────
  int cw=SCR_W-pad*2, rh=22;
  drawCard(pad,y,cw,rh*5+10,C_DCYAN);
  char buf[32];
  snprintf(buf,sizeof(buf),"%.1f C    %.0f%%",T.temp,T.hum);
  valueRow(pad,y+2,cw,"Temp / Humidity",buf,C_CYAN,C_SURF);
  snprintf(buf,sizeof(buf),"F:%ldcm",T.dFront);
  uint16_t dc=T.dFront>0&&T.dFront<30?C_CORAL:T.dFront>0&&T.dFront<60?C_AMBER:C_CYAN;
  valueRow(pad,y+2+rh,cw,"Front sensor",buf,dc,C_SURF);
  snprintf(buf,sizeof(buf),"R:%ld  L:%ld  Ri:%ld",T.dRear,T.dLeft,T.dRight);
  valueRow(pad,y+2+rh*2,cw,"R / L / Right",buf,C_CYAN,C_SURF);
  snprintf(buf,sizeof(buf),"%d",T.gas);
  valueRow(pad,y+2+rh*3,cw,"Gas level",buf,T.gas>300?C_CORAL:C_MINT,C_SURF);
  snprintf(buf,sizeof(buf),"%.2fA   %.2fV",T.amps,T.volt);
  uint16_t vc=T.volt>7.5f?C_MINT:(T.volt>7.0f?C_AMBER:C_CORAL);
  valueRow(pad,y+2+rh*4,cw,"Current / Voltage",buf,vc,C_SURF);
  y+=rh*5+18;

  // ── STATUS CHIPS ─────────────────────────────────────────────────
  struct { const char* l; bool ok; } chips[]={
    {"R3",T.r3ok},{"ESP",T.espok},{"S9",T.s9ok},{"AUTO",T.autoM}
  };
  int cw2=(SCR_W-pad*5)/4;
  for(int i=0;i<4;i++){
    int cx=pad+i*(cw2+pad);
    uint16_t cc=chips[i].ok?C_MINT:C_CORAL;
    fillRoundRect(cx,y,cw2,26,6,chips[i].ok?0x0240u:0x2800u);
    drawRoundRect(cx,y,cw2,26,6,cc);
    drawStrC(cx+cw2/2,y+9,chips[i].l,cc,0xFFFF,1);
  }
  y+=34;

  // ── NAV GRID — 4 tiles ────────────────────────────────────────────
  struct { const char* l; uint16_t c; Screen s; } nav[]={
    {"RADAR",C_CYAN,SCR_RADAR},
    {"GAMES",C_MINT,SCR_GAMES},
    {"SENSORS",C_AMBER,SCR_SENSORS},
    {"INFO",C_PURP,SCR_INFO},
  };
  int tw=(SCR_W-pad*3)/2;
  int th=(CNT_H+CNT_Y-y-pad*3)/2;
  th=max(th,44);
  for(int i=0;i<4;i++){
    int tx=pad+(i%2)*(tw+pad);
    int ty=y+(i/2)*(th+pad);
    drawBtn(tx,ty,tw,th,nav[i].c,nav[i].l,2,false);
    if(hit(t,tx,ty,tw,th)){ curScr=nav[i].s; scrDirty=true; t.pressed=false; return; }
  }
}

// ════════════════════════════════════════════════════════════════════
//  RADAR
// ════════════════════════════════════════════════════════════════════
void drawRadar(Touch& t){
  if(scrDirty){ fillRect(0,CNT_Y,SCR_W,CNT_H,C_BG); scrDirty=false; }
  drawHeader("Proximity Radar"); drawFooter();
  if(drawBackBtn(t)){ curScr=SCR_MAIN; scrDirty=true; t.pressed=false; return; }

  const int CX=SCR_W/2, CY=CNT_Y+CNT_H/2+10, MR=110;
  for(int r=1;r<=4;r++){
    uint16_t rc=(r==1)?C_CORAL:(r==2)?C_AMBER:C_DGRAY;
    drawCircle(CX,CY,(MR*r)/4,rc);
    char rb[8]; snprintf(rb,sizeof(rb),"%dcm",r*25);
    drawStr(CX+(MR*r)/4+3,CY-5,rb,C_DGRAY,0xFFFF,1);
  }
  drawFastHLine(CX-MR,CY,MR*2,C_SURF);
  drawFastVLine(CX,CY-MR,MR*2,C_SURF);
  fillCircle(CX,CY,6,C_CYAN);
  drawCircle(CX,CY,8,C_DCYAN);

  auto plot=[&](long d,float ang,const char* l){
    float a=ang*0.01745329f;
    int r=(d>0&&d<100)?(int)((1.0f-d/100.0f)*MR):0;
    int px=CX+(int)(sinf(a)*r), py=CY-(int)(cosf(a)*r);
    uint16_t c=(d>0&&d<25)?C_CORAL:(d>0&&d<50)?C_AMBER:C_MINT;
    if(r>0){ fillCircle(px,py,8,c); drawCircle(px,py,10,c); }
    char db[14];
    if(d>0) snprintf(db,sizeof(db),"%s:%ld",l,d);
    else    snprintf(db,sizeof(db),"%s:--",l);
    int lx=CX+(int)(sinf(a)*(MR+20))-strW(db)*3;
    int ly=CY-(int)(cosf(a)*(MR+20))-4;
    lx=constrain(lx,2,SCR_W-strW(db)-2);
    ly=constrain(ly,CNT_Y+4,SCR_H-FTR_H-12);
    drawStr(lx,ly,db,c,0xFFFF,1);
  };
  plot(T.dFront,0,"F"); plot(T.dRear,180,"R");
  plot(T.dLeft,-90,"L"); plot(T.dRight,90,"Ri");
}

// ════════════════════════════════════════════════════════════════════
//  GAMES MENU
// ════════════════════════════════════════════════════════════════════
void drawGamesMenu(Touch& t){
  if(scrDirty){ fillRect(0,CNT_Y,SCR_W,CNT_H,C_BG); scrDirty=false; }
  drawHeader("Games for AJ"); drawFooter();
  if(drawBackBtn(t)){ curScr=SCR_MAIN; scrDirty=true; t.pressed=false; return; }

  drawStrC(SCR_W/2,CNT_Y+16,"Tap a game to play!",C_MGRAY,0xFFFF,1);

  struct { const char* n; Screen s; uint16_t c; const char* d; const char* icon; } g[]={
    {"COLOURS",  SCR_GAME_COLOR, C_RED,   "Match the colour", "C"},
    {"SHAPES",   SCR_GAME_SHAPE, C_CYAN,  "Find the shape",   "S"},
    {"COUNTING", SCR_GAME_COUNT, C_YLLOW, "Count the stars",  "*"},
  };
  int gh=(CNT_H-80)/3;
  for(int i=0;i<3;i++){
    int gy=CNT_Y+40+i*(gh+10);
    drawCard(10,gy,SCR_W-20,gh,g[i].c);
    // Icon circle
    fillCircle(48,gy+gh/2,22,0x0841u);
    drawCircle(48,gy+gh/2,22,g[i].c);
    drawStrC(48,gy+gh/2-8,g[i].icon,g[i].c,0xFFFF,3);
    // Text
    drawStr(80,gy+gh/2-14,g[i].n,g[i].c,0xFFFF,2);
    drawStr(80,gy+gh/2+4,g[i].d,C_MGRAY,0xFFFF,1);
    if(hit(t,10,gy,SCR_W-20,gh)){
      gTarget=random(i==2?5:(i==0?4:3));
      if(i==2) gTarget++;
      gScore=0; gFeedbackUntil=0;
      curScr=g[i].s; scrDirty=true; t.pressed=false; return;
    }
  }
}

// Game feedback
void gameCorrect(uint8_t s,int nm){
  gScore++;
  fillRect(0,CNT_Y,SCR_W,CNT_H,0x0240u);
  drawStrC(SCR_W/2,SCR_H/2-30,"CORRECT!",C_MINT,0xFFFF,4);
  char sb[16]; snprintf(sb,sizeof(sb),"Score: %d",gScore);
  drawStrC(SCR_W/2,SCR_H/2+10,sb,C_WHITE,0xFFFF,2);
  fillCircle(SCR_W/2,SCR_H/2+60,24,C_MINT);
  drawStrC(SCR_W/2,SCR_H/2+52,"OK",C_BLACK,0xFFFF,2);
  gTarget=random(nm)+(s==SCR_GAME_COUNT?1:0);
  gFeedbackUntil=millis()+900;
}
void gameWrong(){
  fillRect(0,CNT_Y,SCR_W,CNT_H,0x2800u);
  drawStrC(SCR_W/2,SCR_H/2-30,"Try Again!",C_CORAL,0xFFFF,3);
  drawStrC(SCR_W/2,SCR_H/2+10,"That's not it...",C_LGRAY,0xFFFF,1);
  gFeedbackUntil=millis()+700;
}

void drawGameColor(Touch& t){
  if(millis()<gFeedbackUntil) return;
  if(scrDirty){ fillRect(0,CNT_Y,SCR_W,CNT_H,C_BG); scrDirty=false; }
  drawHeader("Colour Match"); drawFooter();
  if(drawBackBtn(t)){ curScr=SCR_GAMES; scrDirty=true; t.pressed=false; return; }
  char sb[16]; snprintf(sb,sizeof(sb),"Score: %d",gScore);
  drawStr(SCR_W-strW(sb)-8,CNT_Y+8,sb,C_CYAN,0xFFFF,1);
  drawStrC(SCR_W/2,CNT_Y+30,"Touch the colour:",C_LGRAY,0xFFFF,1);
  drawStrC(SCR_W/2,CNT_Y+50,GCNAMES[gTarget],GCOLS[gTarget],0xFFFF,3);
  int bw=(SCR_W-30)/2,bh=64;
  for(int i=0;i<4;i++){
    int bx=8+(i%2)*(bw+14), by=CNT_Y+110+(i/2)*(bh+10);
    fillRoundRect(bx,by,bw,bh,10,GCOLS[i]);
    drawRoundRect(bx,by,bw,bh,10,C_WHITE);
    drawStrC(bx+bw/2,by+bh/2-8,GCNAMES[i],C_WHITE,0xFFFF,2);
    if(hit(t,bx,by,bw,bh)){ t.pressed=false; (i==gTarget)?gameCorrect(SCR_GAME_COLOR,4):gameWrong(); scrDirty=true; return; }
  }
}

void drawGameShape(Touch& t){
  const char* SN[]={"CIRCLE","SQUARE","TRIANGLE"};
  if(millis()<gFeedbackUntil) return;
  if(scrDirty){ fillRect(0,CNT_Y,SCR_W,CNT_H,C_BG); scrDirty=false; }
  drawHeader("Shape Match"); drawFooter();
  if(drawBackBtn(t)){ curScr=SCR_GAMES; scrDirty=true; t.pressed=false; return; }
  char sb[16]; snprintf(sb,sizeof(sb),"Score: %d",gScore);
  drawStr(SCR_W-strW(sb)-8,CNT_Y+8,sb,C_CYAN,0xFFFF,1);
  drawStrC(SCR_W/2,CNT_Y+30,"Find the shape:",C_LGRAY,0xFFFF,1);
  drawStrC(SCR_W/2,CNT_Y+52,SN[gTarget],C_CYAN,0xFFFF,3);
  int bw=SCR_W-24, bh=66, by=CNT_Y+120;
  for(int i=0;i<3;i++){
    drawCard(12,by,bw,bh);
    int cx2=70, cy2=by+bh/2, r=22;
    if(i==0) fillCircle(cx2,cy2,r,C_CYAN);
    else if(i==1) fillRect(cx2-r,cy2-r,r*2,r*2,C_CYAN);
    else fillTriangle(cx2,cy2-r,cx2-r,cy2+r,cx2+r,cy2+r,C_CYAN);
    drawStr(104,by+bh/2-8,SN[i],C_WHITE,0xFFFF,2);
    if(hit(t,12,by,bw,bh)){ t.pressed=false; (i==gTarget)?gameCorrect(SCR_GAME_SHAPE,3):gameWrong(); scrDirty=true; return; }
    by+=bh+8;
  }
}

void drawGameCount(Touch& t){
  if(millis()<gFeedbackUntil) return;
  if(scrDirty){ fillRect(0,CNT_Y,SCR_W,CNT_H,C_BG); scrDirty=false; }
  drawHeader("Count the Stars"); drawFooter();
  if(drawBackBtn(t)){ curScr=SCR_GAMES; scrDirty=true; t.pressed=false; return; }
  char sb[16]; snprintf(sb,sizeof(sb),"Score: %d",gScore);
  drawStr(SCR_W-strW(sb)-8,CNT_Y+8,sb,C_CYAN,0xFFFF,1);
  drawStrC(SCR_W/2,CNT_Y+30,"How many stars?",C_LGRAY,0xFFFF,1);
  // Stars display
  drawCard(12,CNT_Y+54,SCR_W-24,80,C_YLLOW);
  int sp=(gTarget>1)?((SCR_W-60)/(gTarget-1)):0;
  for(int i=0;i<gTarget;i++){
    int sx=(gTarget>1)?(26+i*sp):(SCR_W/2-9);
    drawStrC(sx,CNT_Y+74,"*",C_YLLOW,0xFFFF,3);
  }
  // Number buttons
  int bw=(SCR_W-36)/5, bh=60, by=CNT_Y+150;
  for(int i=0;i<5;i++){
    int bx=8+i*(bw+4);
    char nb[3]; snprintf(nb,sizeof(nb),"%d",i+1);
    bool sel=(i+1==gTarget);
    fillRoundRect(bx,by,bw,bh,8,sel?0x0840u:C_SURF2);
    drawRoundRect(bx,by,bw,bh,8,C_YLLOW);
    drawStrC(bx+bw/2,by+bh/2-16,nb,C_YLLOW,0xFFFF,4);
    if(hit(t,bx,by,bw,bh)){ t.pressed=false; (i+1==gTarget)?gameCorrect(SCR_GAME_COUNT,5):gameWrong(); scrDirty=true; return; }
  }
}

// ════════════════════════════════════════════════════════════════════
//  INFO SCREEN
// ════════════════════════════════════════════════════════════════════
void drawInfo(Touch& t){
  if(scrDirty){ fillRect(0,CNT_Y,SCR_W,CNT_H,C_BG); scrDirty=false; }
  drawHeader("System Info"); drawFooter();
  if(drawBackBtn(t)){ curScr=SCR_MAIN; scrDirty=true; t.pressed=false; return; }

  int y=CNT_Y+6, cw=SCR_W-16, rh=22;
  drawCard(8,y,cw,rh*10+10,C_PURP);

  auto ir=[&](const char* l,const char* v,uint16_t vc){
    valueRow(8,y+2,cw,l,v,vc,C_SURF); y+=rh;
  };
  char buf[28];
  ir("Mega firmware", T.fw.length()?T.fw.c_str():"--", C_CYAN);
  snprintf(buf,sizeof(buf),"%.2f V",T.volt);
  ir("Battery voltage",buf,T.volt>7.5f?C_MINT:T.volt>7.0f?C_AMBER:C_CORAL);
  snprintf(buf,sizeof(buf),"%d%%",T.pct);
  ir("Battery level",buf,T.pct>50?C_MINT:C_AMBER);
  snprintf(buf,sizeof(buf),"%.2f A",T.amps);
  ir("Current draw",buf,C_LGRAY);
  ir("R3 motors",T.r3ok?"LINKED":"OFFLINE",T.r3ok?C_MINT:C_CORAL);
  ir("ESP32 bridge",T.espok?"LINKED":"WAITING",T.espok?C_MINT:C_AMBER);
  ir("S9 Android",T.s9ok?"LINKED":"WAITING",T.s9ok?C_MINT:C_AMBER);
  ir("Autonomous",T.autoM?"ACTIVE":"OFF",T.autoM?C_MINT:C_MGRAY);
  snprintf(buf,sizeof(buf),"%lu s",millis()/1000);
  ir("Pico uptime",buf,C_LGRAY);
  snprintf(buf,sizeof(buf),"%lu s ago",(millis()-lastMegaRx)/1000);
  ir("Last Mega rx",buf,megaLinked?C_MINT:C_CORAL);
}

// ════════════════════════════════════════════════════════════════════
//  SENSORS SCREEN
// ════════════════════════════════════════════════════════════════════
void drawSensors(Touch& t){
  if(scrDirty){ fillRect(0,CNT_Y,SCR_W,CNT_H,C_BG); scrDirty=false; }
  drawHeader("Sensor Config"); drawFooter();
  if(drawBackBtn(t)){ curScr=SCR_MAIN; scrDirty=true; t.pressed=false; return; }

  struct SBtn { const char* id; bool* f; const char* l; };
  SBtn sb[]={
    {"DHT",&SF.dht,"DHT Temp"},{"GAS",&SF.gas,"Gas"},
    {"FLAME",&SF.flame,"Flame"},{"PIR",&SF.pir,"PIR"},
    {"TILT",&SF.tilt,"Tilt"},{"IR",&SF.ir,"IR Obst."},
    {"US",&SF.us,"Ultrasonic"},{"CURRENT",&SF.cur,"Current"},
  };
  int bw=(SCR_W-24)/2, bh=40;
  for(int i=0;i<8;i++){
    int bx=8+(i%2)*(bw+8), by=CNT_Y+8+(i/2)*(bh+8);
    bool on=*sb[i].f;
    uint16_t bc=on?C_MINT:C_CORAL;
    fillRoundRect(bx,by,bw,bh,8,on?0x0240u:0x2800u);
    drawRoundRect(bx,by,bw,bh,8,bc);
    drawStr(bx+10,by+8,sb[i].l,bc,0xFFFF,1);
    drawStr(bx+bw-strW(on?"ON":"OFF")-8,by+8,on?"ON":"OFF",bc,0xFFFF,1);
    if(hit(t,bx,by,bw,bh)){
      *sb[i].f=!(*sb[i].f);
      MEGA_SERIAL.print("TOGGLE_SENSOR:"); MEGA_SERIAL.print(sb[i].id);
      MEGA_SERIAL.println(*sb[i].f?":ON":":OFF");
      scrDirty=true; t.pressed=false;
    }
  }
}

// ════════════════════════════════════════════════════════════════════
//  ALERT OVERLAY
// ════════════════════════════════════════════════════════════════════
void drawAlert(Touch& t){
  const int AX=20,AY=100,AW=SCR_W-40,AH=240;
  fillGrad(AX,AY,AW,AH,alertCol,0x0821u);
  drawRoundRect(AX,AY,AW,AH,12,C_WHITE);
  drawRoundRect(AX+2,AY+2,AW-4,AH-4,10,C_WHITE);
  drawStrC(AX+AW/2,AY+30,alertTitle.c_str(),C_WHITE,0xFFFF,3);
  drawFastHLine(AX+20,AY+72,AW-40,0x8410u);
  drawStrC(AX+AW/2,AY+88,alertMsg.c_str(),C_WHITE,0xFFFF,2);
  int bx=AX+(AW-100)/2, by=AY+AH-52;
  fillRoundRect(bx,by,100,38,8,C_WHITE);
  drawStrC(bx+50,by+15,"DISMISS",alertCol,0xFFFF,2);
  if(hit(t,bx,by,100,38)||(alertTs&&millis()-alertTs>10000)){
    alertOn=false; alertTs=0; scrDirty=true; t.pressed=false;
  }
}

// ════════════════════════════════════════════════════════════════════
//  MEGA PROTOCOL PARSER
// ════════════════════════════════════════════════════════════════════
void raisAlert(const char* ti,const char* msg,uint16_t c){
  alertTitle=ti; alertMsg=msg; alertCol=c; alertOn=true; alertTs=millis();
}
void parseStat(const String& s){
  String f[13]; int n=0,st=5;
  for(int i=5;i<=(int)s.length()&&n<13;i++){
    if(i==(int)s.length()||s[i]==':'){f[n++]=s.substring(st,i);st=i+1;}
  }
  if(n<11) return;
  T.gas=f[0].toInt(); T.temp=f[1].toFloat(); T.hum=f[2].toFloat();
  T.haz=f[3].toInt(); T.pir=f[4].toInt(); T.tilt=f[5].toInt();
  T.flame=f[6].toInt(); T.ir=f[7].toInt();
  T.volt=f[8].toFloat(); T.pct=f[9].toInt(); T.amps=f[10].toFloat();
  if(n>11) T.boost=f[11].toFloat();
}
void parseUS(const String& s){
  String tmp=s.substring(3); String f[4]; int n=0,st=0;
  for(int i=0;i<=(int)tmp.length()&&n<4;i++){
    if(i==(int)tmp.length()||tmp[i]==','){f[n++]=tmp.substring(st,i);st=i+1;}
  }
  if(n<4) return;
  T.dFront=f[0].toInt(); T.dRear=f[1].toInt();
  T.dLeft=f[2].toInt();  T.dRight=f[3].toInt();
}
void parseStatus(const String& s){
  T.estop=(s.indexOf("ESTOP:YES")>=0);
  T.autoM=(s.indexOf("AUTO:ON")>=0);
  T.r3ok=(s.indexOf("R3:OK")>=0);
  T.espok=(s.indexOf("ESP:OK")>=0);
  T.s9ok=(s.indexOf("S9:OK")>=0);
  int fi=s.indexOf("FW:");
  if(fi>=0){ int fe=s.indexOf('|',fi); T.fw=(fe>0)?s.substring(fi+3,fe):s.substring(fi+3); }
}
void handleMegaLine(String& line){
  line.trim(); if(!line.length()) return;
  lastMegaRx=millis(); megaLinked=true;
  if(line.startsWith("STAT:"))           parseStat(line);
  else if(line.startsWith("US:"))        parseUS(line);
  else if(line.startsWith("STATUS|")||line.startsWith("MEGA_READY|")||line.startsWith("SYSTEM|READY|")) parseStatus(line);
  else if(line.startsWith("MODE:"))      T.mode=line.substring(5);
  else if(line=="PING")                  MEGA_SERIAL.println("PONG");
  else if(line.startsWith("CONN_STATUS|"))  parseStatus(line);   // boot status from Mega
  else if(line.startsWith("SENS_ST|"))      parseSensStatus(line);
  else if(line.startsWith("BAT:WARN"))   raisAlert("Battery Low","Charge soon",C_AMBER);
  else if(line.startsWith("BAT:LOW"))    raisAlert("Battery Critical","Plug in NOW",C_CORAL);
  else if(line.startsWith("SAFETY:FLAME"))raisAlert("FLAME DETECTED","Check area",C_CORAL);
  else if(line.startsWith("SAFETY:TILT"))raisAlert("Robot Tilted","Check robot",C_AMBER);
  else if(line.startsWith("SAFETY:GAS")) raisAlert("Gas Detected","Ventilate now",C_AMBER);
  else if(line.startsWith("SAFETY:OVER"))raisAlert("OVERHEATING","Battery too hot",C_CORAL);
  if(line.startsWith("MEGA_READY|")||line.startsWith("SYSTEM|READY|"))
    MEGA_SERIAL.println("SENSOR_STATUS");
}
void handleMegaSerial(){
  int budget=64;
  while(MEGA_SERIAL.available()&&budget-->0){
    char c=MEGA_SERIAL.read();
    if(c=='\n'){ handleMegaLine(megaBuf); megaBuf=""; }
    else if(c!='\r'){ megaBuf+=c; if(megaBuf.length()>128) megaBuf=""; }
  }
}

// ════════════════════════════════════════════════════════════════════
//  SETUP & LOOP
// ════════════════════════════════════════════════════════════════════
void setup(){
  delay(500);
  displayInit();
  // Diagnostic — confirms display alive
  fillScreen(C_RED);   delay(200);
  fillScreen(C_GREEN); delay(200);
  fillScreen(C_BG);

  // Touch controller
  pinMode(PIN_CTP_RST,OUTPUT);
  digitalWrite(PIN_CTP_RST,LOW); delay(20);
  digitalWrite(PIN_CTP_RST,HIGH); delay(100);
  pinMode(PIN_CTP_INT,INPUT);
  Wire1.setSDA(PIN_CTP_SDA);
  Wire1.setSCL(PIN_CTP_SCL);
  Wire1.begin();
  delay(100);

  // Mega UART
  Serial1.setTX(0); Serial1.setRX(1);
  MEGA_SERIAL.begin(115200);
  delay(200);
  MEGA_SERIAL.println("PONG");
  MEGA_SERIAL.println("PING_R4:0");

  randomSeed(analogRead(A0));
  scrDirty=true;
}

void loop(){
  handleMegaSerial();

  // Heartbeat
  if(millis()-lastPing>5000){
    lastPing=millis();
    MEGA_SERIAL.print("PING_R4:"); MEGA_SERIAL.println(pingSeq++);
    if(pingSeq>9999) pingSeq=0;
  }
  // Watchdog
  if(megaLinked&&millis()-lastMegaRx>12000) megaLinked=false;
  // Boot advance
  if(curScr==SCR_BOOT&&megaLinked){ curScr=SCR_MAIN; scrDirty=true; }

  // Touch read (debounced 80ms)
  Touch t={0,0,false};
  if(millis()-lastTouchMs>80&&!digitalRead(PIN_CTP_INT)){
    t=readTouch(); if(t.pressed) lastTouchMs=millis();
  }

  // Global E-STOP — footer hit, always active
  if(hit(t,8,SCR_H-FTR_H+10,82,36)){
    if(T.estop){ MEGA_SERIAL.println("ESTOP_CLEAR"); T.estop=false; }
    else        { MEGA_SERIAL.println("EMERGENCY_STOP"); T.estop=true; }
    scrDirty=true; t.pressed=false;
  }

  // Alert overlay
  if(alertOn){ drawHeader("Alert!"); drawFooter(); drawAlert(t); return; }

  // Telem refresh on main
  if(curScr==SCR_MAIN&&millis()-lastTelemDraw>1000){ lastTelemDraw=millis(); scrDirty=true; }

  switch(curScr){
    case SCR_BOOT:       drawBoot();       break;
    case SCR_MAIN:       drawMain(t);      break;
    case SCR_RADAR:      drawRadar(t);     break;
    case SCR_GAMES:      drawGamesMenu(t); break;
    case SCR_GAME_COLOR: drawGameColor(t); break;
    case SCR_GAME_SHAPE: drawGameShape(t); break;
    case SCR_GAME_COUNT: drawGameCount(t); break;
    case SCR_INFO:       drawInfo(t);      break;
    case SCR_SENSORS:    drawSensors(t);   break;
    default:             drawMain(t);      break;
  }
}
