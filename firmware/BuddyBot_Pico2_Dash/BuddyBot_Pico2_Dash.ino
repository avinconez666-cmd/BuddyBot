/*
 * ════════════════════════════════════════════════════════════════════
 *  BUDDYBOT  —  Pico RP2040  ·  NEXUS HMI  ·  V4.0
 *  Cyberpunk / Sci-Fi Dashboard  |  Portrait 320×480  |  Direct SPI
 * ════════════════════════════════════════════════════════════════════
 *  WIRING (unchanged)
 *  SPI0  GP16=MISO GP17=CS GP18=SCK GP19=MOSI GP20=RST GP21=DC GP22=BL
 *  I2C1  GP26=SDA  GP27=SCL  GP28=INT  GP15=RST  (FT6336U touch)
 *  UART0 GP0=TX→Mega18(RX)  GP1=RX←Mega19(TX via divider)
 * ════════════════════════════════════════════════════════════════════
 */
#include <SPI.h>
#include <Wire.h>
#include "logos.h"  // RGB565 bitmap arrays for splash screen

// ── Forward declarations ──────────────────────────────────────────────
void drawH(int16_t x,int16_t y,int16_t w,uint16_t c);
void drawV(int16_t x,int16_t y,int16_t h,uint16_t c);
void drawRR(int16_t x,int16_t y,int16_t w,int16_t h,int16_t r,uint16_t c);
void fillCircle(int16_t cx,int16_t cy,int16_t r,uint16_t c);
struct Touch { int16_t x,y; bool pressed; };
Touch  readTouch();
bool   hit(const Touch&,int16_t,int16_t,int16_t,int16_t);
void   drawMain(Touch&);
void   drawRadar(Touch&);
void   drawGamesMenu(Touch&);
void   drawGameColor(Touch&);
void   drawGameShape(Touch&);
void   drawGameCount(Touch&);
void   drawInfo(Touch&);
void   drawSensors(Touch&);
void   drawAlert(Touch&);
bool   drawBackBtn(Touch&);

// ── Pins ──────────────────────────────────────────────────────────────
#define PIN_BL       22
#define PIN_CS       17
#define PIN_DC       21
#define PIN_RST      20
#define PIN_CTP_SDA  26
#define PIN_CTP_SCL  27
#define PIN_CTP_INT  28
#define PIN_CTP_RST  29   // FIX #1: was GP15 — wiring diagram shows GP29

// ── ST7796S commands ──────────────────────────────────────────────────
#define ST_SWRESET 0x01
#define ST_SLPOUT  0x11
#define ST_COLMOD  0x3A
#define ST_MADCTL  0x36
#define ST_DISPON  0x29
#define ST_CASET   0x2A
#define ST_PASET   0x2B
#define ST_RAMWR   0x2C

// ── Screen constants ──────────────────────────────────────────────────
#define SCR_W  320
#define SCR_H  480
#define HDR_H   50
#define FTR_H   48
#define CNT_Y   HDR_H
#define CNT_H   (SCR_H - HDR_H - FTR_H)

// ══════════════════════════════════════════════════════════════════════
//  CYBERPUNK COLOUR PALETTE  (RGB565)
// ══════════════════════════════════════════════════════════════════════
#define C_VOID    0x0000u   // absolute black
#define C_BG      0x0820u   // #081018 deep space
#define C_SURF    0x0C41u   // #0C0820 card surface
#define C_SURF2   0x1082u   // #101020 raised
#define C_LINE    0x2124u   // subtle grid line
#define C_GLOW    0x0410u   // dark glow base

// Neon accents
#define C_CYAN    0x07FFu   // #00D4FF electric cyan
#define C_DCYAN   0x03EFu   // dim cyan
#define C_XCYAN   0x001Fu   // deep cyan-blue
#define C_MINT    0x07E4u   // #00FF90 mint green
#define C_PURPLE  0x781Fu   // #7800FF ultraviolet
#define C_DPURP   0x3009u   // dim purple
#define C_AMBER   0xFD20u   // #FFB800 amber
#define C_CORAL   0xF944u   // #FF3355 coral red
#define C_MAG     0xF81Fu   // magenta
#define C_YLLOW   0xFFE0u   // yellow
#define C_WHITE   0xFFFFu
#define C_LGRAY   0x8C71u
#define C_MGRAY   0x4228u
#define C_DGRAY   0x2104u
#define C_RED     0xF800u
#define C_GREEN   0x07E0u
#define C_BLUE    0x001Fu

// ── Forward declarations (after colour defines so defaults resolve) ────
void neonBox(int16_t x,int16_t y,int16_t w,int16_t h,uint16_t col,uint16_t bg);
void hexFrame(int16_t x,int16_t y,int16_t w,int16_t h,uint16_t col,uint8_t sz);
void glowBar(int16_t x,int16_t y,int16_t w,int16_t h,float frac,uint16_t col,uint16_t bg);
void drawH(int16_t x,int16_t y,int16_t w,uint16_t c);
void drawV(int16_t x,int16_t y,int16_t h,uint16_t c);
void drawRR(int16_t x,int16_t y,int16_t w,int16_t h,int16_t r,uint16_t c);
void fillCircle(int16_t cx,int16_t cy,int16_t r,uint16_t c);

// ══════════════════════════════════════════════════════════════════════
//  LOW-LEVEL SPI DISPLAY DRIVER
// ══════════════════════════════════════════════════════════════════════
static SPISettings _spi(20000000, MSBFIRST, SPI_MODE0);

inline void _cmd(uint8_t c){
  digitalWrite(PIN_DC,LOW);  digitalWrite(PIN_CS,LOW);
  SPI.transfer(c);           digitalWrite(PIN_CS,HIGH);
}
inline void _dat(uint8_t d){
  digitalWrite(PIN_DC,HIGH); digitalWrite(PIN_CS,LOW);
  SPI.transfer(d);           digitalWrite(PIN_CS,HIGH);
}

void setWindow(int16_t x0,int16_t y0,int16_t x1,int16_t y1){
  _cmd(ST_CASET); _dat(x0>>8); _dat(x0&0xFF); _dat(x1>>8); _dat(x1&0xFF);
  _cmd(ST_PASET); _dat(y0>>8); _dat(y0&0xFF); _dat(y1>>8); _dat(y1&0xFF);
  _cmd(ST_RAMWR);
}

void fillRect(int16_t x,int16_t y,int16_t w,int16_t h,uint16_t c){
  if(w<=0||h<=0) return;
  if(x<0){w+=x;x=0;} if(y<0){h+=y;y=0;}
  if(x>=SCR_W||y>=SCR_H) return;
  if(x+w>SCR_W) w=SCR_W-x; if(y+h>SCR_H) h=SCR_H-y;
  if(w<=0||h<=0) return;
  setWindow(x,y,x+w-1,y+h-1);
  digitalWrite(PIN_DC,HIGH); digitalWrite(PIN_CS,LOW);
  uint32_t n=(uint32_t)w*h;
  uint8_t hi=c>>8,lo=c&0xFF;
  for(uint32_t i=0;i<n;i++){SPI.transfer(hi);SPI.transfer(lo);}
  digitalWrite(PIN_CS,HIGH);
}

void fillScreen(uint16_t c){ fillRect(0,0,SCR_W,SCR_H,c); }

void drawPixel(int16_t x,int16_t y,uint16_t c){
  if(x<0||x>=SCR_W||y<0||y>=SCR_H) return;
  setWindow(x,y,x,y); _dat(c>>8); _dat(c&0xFF);
}

// Vertical gradient fill
void fillGrad(int16_t x,int16_t y,int16_t w,int16_t h,uint16_t c1,uint16_t c2){
  uint8_t r1=c1>>11,g1=(c1>>5)&0x3F,b1=c1&0x1F;
  uint8_t r2=c2>>11,g2=(c2>>5)&0x3F,b2=c2&0x1F;
  for(int16_t i=0;i<h;i++){
    int r=r1+(int)(r2-r1)*i/h;
    int g=g1+(int)(g2-g1)*i/h;
    int b=b1+(int)(b2-b1)*i/h;
    fillRect(x,y+i,w,1,(r<<11)|(g<<5)|b);
  }
}


// ══════════════════════════════════════════════════════════════════════
//  SPLASH SCREEN — BITMAP RENDERER + ANIMATIONS
// ══════════════════════════════════════════════════════════════════════
void drawBitmap(int16_t x,int16_t y,const uint16_t* bmp,int16_t w,int16_t h){
  setWindow(x,y,x+w-1,y+h-1);
  digitalWrite(PIN_DC,HIGH); digitalWrite(PIN_CS,LOW);
  for(uint32_t i=0;i<(uint32_t)w*h;i++){
    uint16_t px=pgm_read_word(&bmp[i]);
    SPI.transfer(px>>8); SPI.transfer(px&0xFF);
  }
  digitalWrite(PIN_CS,HIGH);
}

static float _orbitAngle=0.0f;
void orbitalParticles(int16_t cx,int16_t cy,float radius,uint8_t count,uint16_t col){
  _orbitAngle+=0.09f;
  for(uint8_t i=0;i<count;i++){
    float a=_orbitAngle+i*(6.2832f/count);
    int16_t px=cx+(int16_t)(cosf(a)*radius);
    int16_t py=cy+(int16_t)(sinf(a)*radius*0.65f);
    if(px>=0&&px<SCR_W&&py>=0&&py<SCR_H){
      drawPixel(px,py,col);
      if((i&1)==0&&px+1<SCR_W) drawPixel(px+1,py,C_XCYAN);
    }
  }
}

void showStartupSplash(){
  fillScreen(C_BG);
  // Background grid
  for(int16_t gx=0;gx<SCR_W;gx+=20) drawV(gx,0,SCR_H,0x0421u);
  for(int16_t gy=0;gy<SCR_H;gy+=20) drawH(0,gy,SCR_W,0x0421u);

  // ── PHASE 1: Reinsma Innovations ─────────────────────────────────
  neonBox(30,75,SCR_W-60,185,C_CYAN,C_SURF);
  hexFrame(40,90,SCR_W-80,155,C_PURPLE,14);
  int16_t ri_x=(SCR_W-reinsma_logo_W)/2;
  int16_t ri_y=88;
  drawBitmap(ri_x,ri_y,reinsma_logo,reinsma_logo_W,reinsma_logo_H);
  drawStrC(SCR_W/2,222,"GENERATIVE  AI  SOLUTIONS",C_PURPLE,0xFFFF,1);
  drawStrC(SCR_W/2,238,"Proudly Powered by Arduino",C_MGRAY,0xFFFF,1);

  // Particle burst
  for(uint8_t i=0;i<65;i++){
    drawPixel(random(40,SCR_W-40),random(90,240),C_CYAN);
    if(i%6==0) delay(9);
  }
  delay(1800);

  // Smooth fade out
  for(uint8_t step=0;step<16;step++){
    fillRect(25,70,SCR_W-50,200,(uint16_t)(step*0x0842u));
    delay(35);
  }

  // ── PHASE 2: BuddyBot v1.0 ────────────────────────────────────────
  fillScreen(C_BG);
  for(int16_t gx=0;gx<SCR_W;gx+=20) drawV(gx,0,SCR_H,0x0421u);
  for(int16_t gy=0;gy<SCR_H;gy+=20) drawH(0,gy,SCR_W,0x0421u);

  int16_t bx=(SCR_W-buddybot_logo_W)/2;
  int16_t by=30;
  drawBitmap(bx,by,buddybot_logo,buddybot_logo_W,buddybot_logo_H);

  // Loading bar + orbital particles during init
  neonBox(24,296,SCR_W-48,44,C_PURPLE,C_SURF);
  drawStrC(SCR_W/2,270,"INITIALIZING  NEXUS  OS...",C_LGRAY,0xFFFF,1);
  drawStrC(SCR_W/2,256,"GUARDIAN SYSTEM",C_PURPLE,0xFFFF,1);

  int16_t oc_x=bx+buddybot_logo_W/2;
  int16_t oc_y=by+buddybot_logo_H/2;
  for(int p=0;p<=100;p+=2){
    // Erase old particles before drawing new (avoids trails)
    fillCircle(oc_x,oc_y,130,C_BG);
    // Redraw logo centre (particle orbit overlaps it)
    drawBitmap(bx,by,buddybot_logo,buddybot_logo_W,buddybot_logo_H);
    // Orbital rings
    orbitalParticles(oc_x,oc_y,100,14,C_CYAN);
    orbitalParticles(oc_x,oc_y,124,9,C_XCYAN);
    orbitalParticles(oc_x,oc_y,80,6,C_PURPLE);
    // Progress bar
    glowBar(28,300,SCR_W-56,36,p/100.0f,C_CYAN,C_SURF2);
    if(p%10==0) drawRR(24,296,SCR_W-48,44,6,C_WHITE);
    delay(80);
  }
  delay(600);

  // Fade to black
  for(uint8_t step=0;step<20;step++){
    fillRect(0,0,SCR_W,SCR_H,(uint16_t)((19-step)*0x0842u));
    delay(25);
  }
  fillScreen(C_BG);
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
  SPI.begin(); SPI.beginTransaction(_spi);
  _cmd(ST_SWRESET); delay(120);
  _cmd(ST_SLPOUT);  delay(120);
  _cmd(ST_COLMOD);  _dat(0x55);
  _cmd(ST_MADCTL);  _dat(0x48);   // MX|BGR — portrait, X-mirrored
  _cmd(ST_DISPON);  delay(50);
}

// ══════════════════════════════════════════════════════════════════════
//  BITMAP FONT  (6×8 glyphs, scale for large/small text)
// ══════════════════════════════════════════════════════════════════════
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
  while(*s){ drawChar(x,y,*s++,fg,bg,sz); x+=6*sz; }
}

// Centred
void drawStrC(int16_t cx,int16_t y,const char* s,uint16_t fg,uint16_t bg,uint8_t sz){
  drawStr(cx-(int16_t)(strlen(s)*6*sz/2),y,s,fg,bg,sz);
}

// Right-aligned
void drawStrR(int16_t rx,int16_t y,const char* s,uint16_t fg,uint16_t bg,uint8_t sz){
  drawStr(rx-(int16_t)(strlen(s)*6*sz),y,s,fg,bg,sz);
}

int16_t strW(const char* s,uint8_t sz=1){ return strlen(s)*6*sz; }

// ══════════════════════════════════════════════════════════════════════
//  SHAPE PRIMITIVES
// ══════════════════════════════════════════════════════════════════════
void drawH(int16_t x,int16_t y,int16_t w,uint16_t c){ fillRect(x,y,w,1,c); }
void drawV(int16_t x,int16_t y,int16_t h,uint16_t c){ fillRect(x,y,1,h,c); }
void drawRect(int16_t x,int16_t y,int16_t w,int16_t h,uint16_t c){
  drawH(x,y,w,c); drawH(x,y+h-1,w,c); drawV(x,y,h,c); drawV(x+w-1,y,h,c);
}
void fillRR(int16_t x,int16_t y,int16_t w,int16_t h,int16_t r,uint16_t c){
  fillRect(x+r,y,w-2*r,h,c);
  fillRect(x,y+r,r,h-2*r,c);
  fillRect(x+w-r,y+r,r,h-2*r,c);
}
void drawRR(int16_t x,int16_t y,int16_t w,int16_t h,int16_t r,uint16_t c){
  drawH(x+r,y,w-2*r,c); drawH(x+r,y+h-1,w-2*r,c);
  drawV(x,y+r,h-2*r,c); drawV(x+w-1,y+r,h-2*r,c);
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
void fillTri(int16_t x0,int16_t y0,int16_t x1,int16_t y1,int16_t x2,int16_t y2,uint16_t c){
  if(y0>y1){int16_t t=y0;y0=y1;y1=t;t=x0;x0=x1;x1=t;}
  if(y1>y2){int16_t t=y1;y1=y2;y2=t;t=x1;x1=x2;x2=t;}
  if(y0>y1){int16_t t=y0;y0=y1;y1=t;t=x0;x0=x1;x1=t;}
  for(int16_t y=y0;y<=y2;y++){
    int16_t a=(y<y1)?x0+(x1-x0)*(y-y0)/(y1-y0):x1+(x2-x1)*(y-y1)/(y2-y1);
    int16_t b=x0+(x2-x0)*(y-y0)/(y2-y0);
    if(a>b){int16_t t=a;a=b;b=t;}
    drawH(a,y,b-a+1,c);
  }
}

// ══════════════════════════════════════════════════════════════════════
//  CYBERPUNK UI COMPONENTS
// ══════════════════════════════════════════════════════════════════════

// Glowing neon box — double border with inner glow colour
void neonBox(int16_t x,int16_t y,int16_t w,int16_t h,uint16_t col,uint16_t bg=C_SURF){
  fillRR(x,y,w,h,6,bg);
  drawRR(x,y,w,h,6,col);
  // inner highlight
  drawRR(x+2,y+2,w-4,h-4,4,col);
  // top accent line
  fillRect(x+8,y,w-16,2,col);
}

// Hex corner brackets (sci-fi frame)
void hexFrame(int16_t x,int16_t y,int16_t w,int16_t h,uint16_t col,uint8_t sz=8){
  // TL
  drawH(x,y,sz,col);      drawV(x,y,sz,col);
  drawPixel(x+1,y+1,col);
  // TR
  drawH(x+w-sz,y,sz,col); drawV(x+w-1,y,sz,col);
  drawPixel(x+w-2,y+1,col);
  // BL
  drawH(x,y+h-1,sz,col);  drawV(x,y+h-sz,sz,col);
  drawPixel(x+1,y+h-2,col);
  // BR
  drawH(x+w-sz,y+h-1,sz,col); drawV(x+w-1,y+h-sz,sz,col);
  drawPixel(x+w-2,y+h-2,col);
}

// Progress bar with glow
void glowBar(int16_t x,int16_t y,int16_t w,int16_t h,float frac,uint16_t col,uint16_t bg=C_SURF2){
  fillRR(x,y,w,h,h/2,bg);
  int16_t filled=(int16_t)(frac*w); if(filled<2)filled=2; if(filled>w)filled=w;
  fillRR(x,y,filled,h,h/2,col);
  // highlight
  fillRect(x+2,y+1,filled-4,1,C_WHITE);
}

// Status pill badge
void drawPill(int16_t x,int16_t y,int16_t w,int16_t h,uint16_t col,bool on){
  uint16_t bg=on?col:C_SURF2;
  fillRR(x,y,w,h,h/2,bg);
  drawRR(x,y,w,h,h/2,col);
}

// Animated pulsing dot (call each frame with millis)
void pulseDot(int16_t cx,int16_t cy,int16_t r,uint16_t col,bool active){
  if(!active){ fillCircle(cx,cy,r,C_MGRAY); return; }
  float t=(millis()%1200)/1200.0f;
  uint8_t alpha=(uint8_t)(128+127*sinf(t*2*3.14159f));
  // Outer glow ring — fade in/out
  uint8_t ar=((col>>11)*(alpha>>3))>>5;
  uint8_t ag=(((col>>5)&0x3F)*(alpha>>2))>>6;
  uint8_t ab=((col&0x1F)*(alpha>>3))>>5;
  uint16_t glowCol=(ar<<11)|(ag<<5)|ab;
  drawCircle(cx,cy,r+3,glowCol);
  fillCircle(cx,cy,r,col);
}

// Scan line animation
void scanLine(int16_t x,int16_t y,int16_t w,int16_t h){
  static unsigned long lastScan=0;
  static int16_t scanPos=0;
  if(millis()-lastScan>18){ lastScan=millis(); scanPos=(scanPos+2)%h; }
  // Subtle horizontal glow bar scrolling down the panel
  if(scanPos>=0&&scanPos<h){
    uint16_t slc=0x0421u;  // very dark tint
    fillRect(x,y+scanPos,w,1,slc);
    if(scanPos>0) fillRect(x,y+scanPos-1,w,1,slc);
  }
}

// Value cell — label + large value — partial update
void valueCell(int16_t x,int16_t y,int16_t w,int16_t h,
               const char* label,const char* val,uint16_t vcol,uint16_t bg=C_SURF){
  fillRect(x,y,w,h,bg);
  drawRR(x,y,w,h,4,C_LINE);
  // Top accent
  fillRect(x+4,y,w-8,2,vcol);
  // Label
  int16_t lx=x+(w-strW(label,1))/2;
  drawStr(lx,y+5,label,vcol,bg,1);
  // Value large
  uint8_t vsz=3;
  int16_t vw=strW(val,vsz);
  while(vw>w-6&&vsz>1){ vsz--; vw=strW(val,vsz); }
  int16_t vx=x+(w-vw)/2;
  int16_t vy=y+h/2-4*vsz;
  drawStr(vx,vy,val,vcol,bg,vsz);
}

// Divider with label
void sectionDiv(int16_t y,const char* lbl,uint16_t col=C_CYAN){
  drawH(0,y,SCR_W,col);
  int16_t tw=strW(lbl,1);
  int16_t lx=(SCR_W-tw-8)/2;
  fillRect(lx-2,y-4,tw+12,9,C_BG);
  drawStr(lx+2,y-4,lbl,col,C_BG,1);
}

// Touch helper
bool hit(const Touch& t,int16_t x,int16_t y,int16_t w,int16_t h){
  return t.pressed&&t.x>=x&&t.x<x+w&&t.y>=y&&t.y<y+h;
}

// ══════════════════════════════════════════════════════════════════════
//  APP STATE
// ══════════════════════════════════════════════════════════════════════
struct Telemetry {
  int   gas=0; float temp=0,hum=0;
  bool  haz=false,pir=false,tilt=false,ir=false;
  float volt=8.4f; int pct=100; float amps=0;
  long  dFront=-1,dRear=-1,dLeft=-1,dRight=-1;
  bool  estop=false,autoM=false;
  String mode="NORMAL",fw="";
  bool  r3ok=false,espok=false,s9ok=false;
} T;

// Previous telemetry for partial-update comparison
struct Telemetry Tprev;

struct SensFlags { bool dht=true,gas=true,flame=true,pir=false,tilt=true,ir=true,us=true,cur=true; } SF;

enum Screen { SCR_BOOT,SCR_MAIN,SCR_RADAR,SCR_GAMES,
              SCR_GAME_COLOR,SCR_GAME_SHAPE,SCR_GAME_COUNT,
              SCR_INFO,SCR_SENSORS };
Screen curScr=SCR_BOOT;
bool   scrDirty=true;
bool   firstMainDraw=true;  // force full main draw on first visit

bool   alertOn=false;
String alertTitle="",alertMsg="";
uint16_t alertCol=C_CORAL;
unsigned long alertTs=0;

#define MEGA_SERIAL  Serial1
String        megaBuf="";
bool          megaLinked=false;
unsigned long lastMegaRx=0,lastPing=0;
uint16_t      pingSeq=0;

const uint16_t GCOLS[]={C_RED,C_GREEN,C_BLUE,C_YLLOW};
const char* GCNAMES[]={"RED","GREEN","BLUE","YELLOW"};
int   gTarget=0,gScore=0;
unsigned long gFeedbackUntil=0;
unsigned long lastTouchMs=0;

// ══════════════════════════════════════════════════════════════════════
//  TOUCH — FIX: mirror X for MADCTL 0x48 (MX bit set)
// ══════════════════════════════════════════════════════════════════════
#define CTP_ADDR 0x38
Touch readTouch(){
  Touch t={0,0,false};
  // FIX #3 applied here: Wire instead of Wire1 — matches I2C0 init in setup()
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
  t.x = constrain(319-rx, 0, SCR_W-1);   // mirror X for MADCTL 0x48
  t.y = constrain(ry,     0, SCR_H-1);
  t.pressed=true;
  return t;
}

// ══════════════════════════════════════════════════════════════════════
//  CHROME — Header & Footer (persistent chrome — redrawn only on change)
// ══════════════════════════════════════════════════════════════════════
void drawHeader(const char* title){
  fillGrad(0,0,SCR_W,HDR_H,0x0841u,C_BG);
  drawH(0,HDR_H-1,SCR_W,C_CYAN);
  drawH(0,HDR_H-2,SCR_W,C_XCYAN);

  // Mode badge
  uint16_t mc=C_CYAN;
  if(T.estop)              mc=C_CORAL;
  else if(T.mode=="BODYGUARD") mc=C_AMBER;
  else if(T.mode=="DOG")       mc=C_MINT;
  else if(T.mode=="UNHINGED")  mc=C_MAG;
  else if(T.mode=="PARTY")     mc=C_PURPLE;
  const char* ms=T.estop?"E-STOP":T.mode.c_str();
  int16_t bw=strW(ms,1)+14;
  fillRR(4,8,bw,34,5,mc);
  drawRR(4,8,bw,34,5,C_WHITE);
  drawStrC(4+bw/2,19,ms,C_VOID,mc,1);

  // Title centred
  drawStrC(SCR_W/2,(HDR_H-16)/2,title,C_WHITE,0xFFFF,2);

  // Battery + link
  uint16_t bc=T.pct>50?C_MINT:(T.pct>20?C_AMBER:C_CORAL);
  char bat[10]; snprintf(bat,sizeof(bat),"%d%%",T.pct);
  drawStrR(SCR_W-18,(HDR_H-8)/2,bat,bc,0xFFFF,1);
  pulseDot(SCR_W-10,HDR_H/2,5,megaLinked?C_MINT:C_CORAL,megaLinked);
}

void drawFooter(){
  int fy=SCR_H-FTR_H;
  fillGrad(0,fy,SCR_W,FTR_H,C_BG,0x0841u);
  drawH(0,fy,SCR_W,C_PURPLE);
  drawH(0,fy+1,SCR_W,C_DPURP);

  // E-STOP
  uint16_t ec=T.estop?C_AMBER:C_CORAL;
  fillRR(6,fy+8,86,32,6,T.estop?0x4200u:0x2800u);
  drawRR(6,fy+8,86,32,6,ec);
  drawRR(8,fy+10,82,28,4,ec);
  const char* el=T.estop?"RESUME":"E-STOP";
  drawStrC(49,fy+20,el,ec,0xFFFF,1);

  // Hazard pills
  int ix=100;
  if(T.tilt){ fillRR(ix,fy+12,40,24,6,0x2200u); drawRR(ix,fy+12,40,24,6,C_AMBER); drawStrC(ix+20,fy+20,"TILT",C_AMBER,0xFFFF,1); ix+=46; }
  if(T.ir)  { fillRR(ix,fy+12,28,24,6,0x0210u); drawRR(ix,fy+12,28,24,6,C_MINT);  drawStrC(ix+14,fy+20,"IR",  C_MINT, 0xFFFF,1); ix+=34; }

  // Mega link
  if(megaLinked){
    unsigned long age=(millis()-lastMegaRx)/1000;
    char buf[14];
    if(age<5) snprintf(buf,sizeof(buf),"LIVE");
    else      snprintf(buf,sizeof(buf),"%lus",age);
    uint16_t lc=age<5?C_MINT:C_AMBER;
    drawStrR(SCR_W-6,fy+13,"Mega",C_DGRAY,0xFFFF,1);
    drawStrR(SCR_W-6,fy+23,buf,lc,0xFFFF,1);
  } else {
    drawStrR(SCR_W-6,fy+18,"NO LINK",C_CORAL,0xFFFF,1);
  }
}

bool drawBackBtn(Touch& t){
  int fy=SCR_H-FTR_H;
  fillRR(SCR_W-74,fy+8,68,32,6,C_SURF2);
  drawRR(SCR_W-74,fy+8,68,32,6,C_PURPLE);
  drawStrC(SCR_W-40,fy+20,"< BACK",C_PURPLE,0xFFFF,1);
  return hit(t,SCR_W-74,fy+8,68,32);
}

// ══════════════════════════════════════════════════════════════════════
//  BOOT SCREEN — animated, no flicker
// ══════════════════════════════════════════════════════════════════════
void drawBoot(){
  static unsigned long lastAnim=0;
  static uint8_t di=0;
  static uint8_t phase=0;

  if(scrDirty){
    fillScreen(C_BG);
    // Background grid
    for(int16_t gx=0;gx<SCR_W;gx+=20) drawV(gx,0,SCR_H,0x0421u);
    for(int16_t gy=0;gy<SCR_H;gy+=20) drawH(0,gy,SCR_W,0x0421u);

    // Logo box
    fillGrad(20,60,SCR_W-40,110,0x0841u,C_BG);
    hexFrame(20,60,SCR_W-40,110,C_CYAN,10);
    drawStrC(SCR_W/2,80,"BUDDYBOT",C_CYAN,0xFFFF,4);
    drawStrC(SCR_W/2,116,"NEXUS  HMI  v4.0",C_DCYAN,0xFFFF,1);
    drawH(40,136,SCR_W-80,C_LINE);
    drawStrC(SCR_W/2,144,"For AJ   |   Guardian System",C_MGRAY,0xFFFF,1);

    // Hardware info
    neonBox(20,174,SCR_W-40,76,C_PURPLE,C_SURF);
    drawStrC(SCR_W/2,184,"HARDWARE",C_PURPLE,C_SURF,1);
    drawStrC(SCR_W/2,200,"ST7796S  320x480  Direct SPI",C_LGRAY,C_SURF,1);
    drawStrC(SCR_W/2,212,"FT6336U  Capacitive Touch",C_LGRAY,C_SURF,1);
    drawStrC(SCR_W/2,224,"RP2040   Raspberry Pi Pico",C_LGRAY,C_SURF,1);
    drawStrC(SCR_W/2,236,"NEXUS-OS  Serial1  115200",C_LGRAY,C_SURF,1);

    drawStrC(SCR_W/2,270,"Waiting for Mega...",C_AMBER,0xFFFF,2);
    scrDirty=false;
  }

  // Dot animation
  if(millis()-lastAnim>200){ lastAnim=millis(); di=(di+1)%8; }
  int16_t dx=SCR_W/2-56;
  for(int i=0;i<8;i++){
    uint16_t dc=(i<=di)?C_CYAN:C_DGRAY;
    int16_t r=(i==di)?6:3;
    fillRect(dx+i*16-r,300-r,r*2+1,r*2+1,C_BG);
    fillCircle(dx+i*16,300,r,dc);
    if(i==di){
      drawCircle(dx+i*16,300,r+2,C_XCYAN);
    }
  }
}

// ══════════════════════════════════════════════════════════════════════
//  MAIN DASHBOARD — smart partial updates (no flicker)
// ══════════════════════════════════════════════════════════════════════

// Cell positions for the telemetry grid
#define CELL_W   ((SCR_W-18)/2)
#define CELL_H   54
#define CELL_X1  6
#define CELL_X2  (CELL_X1+CELL_W+6)
#define CELL_Y1  (CNT_Y+6)
#define CELL_Y2  (CELL_Y1+CELL_H+4)
#define CELL_Y3  (CELL_Y2+CELL_H+4)

void updateTelCell(int16_t x,int16_t y,const char* lbl,const char* val,uint16_t col){
  valueCell(x,y,CELL_W,CELL_H,lbl,val,col,C_SURF);
}

void drawMainFull(){
  fillRect(0,CNT_Y,SCR_W,CNT_H,C_BG);
  // Background grid lines
  for(int16_t gx=0;gx<SCR_W;gx+=16) drawV(gx,CNT_Y,CNT_H,0x0421u);

  // Section: TELEMETRY
  sectionDiv(CNT_Y+4,"TELEMETRY",C_CYAN);

  // Mode selector row
  int mw=(SCR_W-24)/3;
  struct { const char* l; uint16_t c; const char* cmd; } modes[]={
    {"NORMAL",C_CYAN,"NORMAL"},{"BODYGUARD",C_AMBER,"BODYGUARD"},{"DOG",C_MINT,"DOG"}
  };
  for(int i=0;i<3;i++){
    int mx=6+i*(mw+6);
    int my=CELL_Y3+CELL_H+10;
    bool act=(T.mode==modes[i].cmd);
    uint16_t bg=act?modes[i].c:C_SURF2;
    uint16_t fg=act?C_VOID:modes[i].c;
    fillRR(mx,my,mw,34,6,bg);
    drawRR(mx,my,mw,34,6,modes[i].c);
    if(act){ fillRect(mx+4,my,mw-8,2,C_WHITE); }
    drawStrC(mx+mw/2,my+13,modes[i].l,fg,0xFFFF,1);
  }
  sectionDiv(CELL_Y3+CELL_H+54,"MODE",C_PURPLE);

  // Nav buttons
  int ny=CELL_Y3+CELL_H+64;
  int nbw=(SCR_W-18)/2, nbh=44;
  struct { const char* l; uint16_t c; Screen s; } nav[]={
    {"RADAR",C_CYAN,SCR_RADAR},{"GAMES",C_MINT,SCR_GAMES},
    {"SENSORS",C_AMBER,SCR_SENSORS},{"INFO",C_PURPLE,SCR_INFO}
  };
  for(int i=0;i<4;i++){
    int nx2=6+(i%2)*(nbw+6);
    int ny2=ny+(i/2)*(nbh+6);
    neonBox(nx2,ny2,nbw,nbh,nav[i].c,C_SURF);
    drawStrC(nx2+nbw/2,ny2+nbh/2-4,nav[i].l,nav[i].c,0xFFFF,2);
  }
}

void updateTelemetryOnly(){
  char buf[22];
  // Row 1
  snprintf(buf,sizeof(buf),"%.1fC",T.temp);
  updateTelCell(CELL_X1,CELL_Y1,"TEMP",buf,T.temp>38?C_CORAL:T.temp>32?C_AMBER:C_CYAN);
  snprintf(buf,sizeof(buf),"%.0f%%",T.hum);
  updateTelCell(CELL_X2,CELL_Y1,"HUMIDITY",buf,C_CYAN);
  // Row 2
  snprintf(buf,sizeof(buf),"%d%%",T.pct);
  updateTelCell(CELL_X1,CELL_Y2,"BATTERY",buf,T.pct>50?C_MINT:T.pct>20?C_AMBER:C_CORAL);
  snprintf(buf,sizeof(buf),"%.1fV",T.volt);
  updateTelCell(CELL_X2,CELL_Y2,"VOLTAGE",buf,T.volt>7.5f?C_MINT:T.volt>7.0f?C_AMBER:C_CORAL);
  // Row 3
  snprintf(buf,sizeof(buf),"%d",T.gas);
  updateTelCell(CELL_X1,CELL_Y3,"GAS",buf,T.gas>400?C_CORAL:T.gas>200?C_AMBER:C_MINT);
  // Sensor status flags
  fillRect(CELL_X2,CELL_Y3,CELL_W,CELL_H,C_SURF);
  drawRR(CELL_X2,CELL_Y3,CELL_W,CELL_H,4,C_LINE);
  fillRect(CELL_X2+4,CELL_Y3,CELL_W-8,2,C_PURPLE);
  drawStr(CELL_X2+4,CELL_Y3+5,"SENSORS",C_PURPLE,C_SURF,1);
  // Status dots row
  struct { const char* l; bool v; uint16_t c; } flags[]={
    {"R3",T.r3ok,C_MINT},{"ESP",T.espok,C_CYAN},{"S9",T.s9ok,C_PURPLE},{"AUTO",T.autoM,C_AMBER}
  };
  int fx=CELL_X2+4;
  for(int i=0;i<4;i++){
    pulseDot(fx+6,CELL_Y3+CELL_H/2+4,4,flags[i].c,flags[i].v);
    drawStr(fx+12,CELL_Y3+CELL_H/2,flags[i].l,flags[i].v?flags[i].c:C_MGRAY,C_SURF,1);
    fx+=(CELL_W-8)/4;
  }
}

void drawMain(Touch& t){
  // First visit — draw full layout
  if(scrDirty||firstMainDraw){
    drawMainFull();
    firstMainDraw=false;
    scrDirty=false;
  }

  drawHeader("NEXUS DASHBOARD");
  drawFooter();

  // Only update telemetry cells — no full screen wipe
  updateTelemetryOnly();
  scanLine(0,CNT_Y,SCR_W,CNT_H);

  // Touch — mode buttons
  int mw=(SCR_W-24)/3;
  struct { const char* l; uint16_t c; const char* cmd; } modes[]={
    {"NORMAL",C_CYAN,"NORMAL"},{"BODYGUARD",C_AMBER,"BODYGUARD"},{"DOG",C_MINT,"DOG"}
  };
  for(int i=0;i<3;i++){
    int mx=6+i*(mw+6);
    int my=CELL_Y3+CELL_H+10;
    if(hit(t,mx,my,mw,34)){
      MEGA_SERIAL.print("MODE:"); MEGA_SERIAL.println(modes[i].cmd);
      T.mode=modes[i].cmd; firstMainDraw=true; t.pressed=false; return;
    }
  }

  // Touch — nav buttons
  int ny=CELL_Y3+CELL_H+64;
  int nbw=(SCR_W-18)/2, nbh=44;
  struct { const char* l; uint16_t c; Screen s; } nav[]={
    {"RADAR",C_CYAN,SCR_RADAR},{"GAMES",C_MINT,SCR_GAMES},
    {"SENSORS",C_AMBER,SCR_SENSORS},{"INFO",C_PURPLE,SCR_INFO}
  };
  for(int i=0;i<4;i++){
    int nx2=6+(i%2)*(nbw+6);
    int ny2=ny+(i/2)*(nbh+6);
    if(hit(t,nx2,ny2,nbw,nbh)){
      curScr=nav[i].s; scrDirty=true; firstMainDraw=true; t.pressed=false; return;
    }
  }
}

// ══════════════════════════════════════════════════════════════════════
//  RADAR — animated neon sweep
// ══════════════════════════════════════════════════════════════════════
void drawRadar(Touch& t){
  if(scrDirty){ fillRect(0,CNT_Y,SCR_W,CNT_H,C_BG); scrDirty=false; }
  drawHeader("PROXIMITY RADAR"); drawFooter();
  if(drawBackBtn(t)){ curScr=SCR_MAIN; scrDirty=true; firstMainDraw=true; t.pressed=false; return; }

  const int CX=SCR_W/2, CY=CNT_Y+CNT_H/2+20, MR=106;
  // Animated sweep
  float sweep=((millis()%3000)/3000.0f)*2*3.14159f;

  // Clear radar area
  fillCircle(CX,CY,MR+8,C_BG);

  // Range rings with neon glow
  uint16_t rings[]={0x2800u,0x4900u,0x0210u,C_XCYAN};
  for(int r=1;r<=4;r++){
    drawCircle(CX,CY,(MR*r)/4,rings[r-1]);
    char rb[8]; snprintf(rb,sizeof(rb),"%d",r*25);
    drawStr(CX+(MR*r)/4+2,CY-5,rb,C_DGRAY,0xFFFF,1);
  }
  // Cross hairs
  drawH(CX-MR,CY,MR*2,C_SURF2);
  drawV(CX,CY-MR,MR*2,C_SURF2);

  // Sweep trail (fade)
  for(int step=0;step<12;step++){
    float a=sweep-step*0.15f;
    int sx=CX+(int)(cosf(a-1.5708f)*MR);
    int sy=CY+(int)(sinf(a-1.5708f)*MR);
    uint8_t alpha=12-step;
    uint16_t sc=((alpha>>1)<<5); // fading green
    fillRect((CX+sx)/2-1,(CY+sy)/2-1,2,2,sc);
  }
  // Sweep line
  int sx=CX+(int)(cosf(sweep-1.5708f)*MR);
  int sy=CY+(int)(sinf(sweep-1.5708f)*MR);
  // Draw sweep line pixel by pixel for glow
  for(int r=2;r<=MR;r+=2){
    int px=CX+(int)(cosf(sweep-1.5708f)*r);
    int py=CY+(int)(sinf(sweep-1.5708f)*r);
    fillRect(px-1,py-1,2,2,C_MINT);
  }

  // Centre
  fillCircle(CX,CY,6,C_BG);
  fillCircle(CX,CY,5,C_CYAN);
  drawCircle(CX,CY,7,C_DCYAN);

  // Plot sensors
  auto plot=[&](long d,float ang,const char* l){
    if(d<=0) return;
    float a=(ang-90.0f)*0.01745329f;
    float frac=1.0f-constrain(d,0,100)/100.0f;
    int r=(int)(frac*MR); if(r<4)return;
    int px=CX+(int)(cosf(a)*r);
    int py=CY+(int)(sinf(a)*r);
    uint16_t col=d<25?C_CORAL:d<60?C_AMBER:C_MINT;
    // Glow
    for(int gr=12;gr>=4;gr-=4){
      uint8_t gv=12-gr+4;
      drawCircle(px,py,gr,d<25?((gv>>1)<<11):(d<60?((gv>>1)<<11|(gv>>1)<<5):((gv>>1)<<5)));
    }
    fillCircle(px,py,5,col);
    char db[12]; snprintf(db,sizeof(db),"%s%ldcm",l,d);
    int lx=CX+(int)(cosf(a)*(MR+14))-strW(db)/2;
    int ly=CY+(int)(sinf(a)*(MR+14))-4;
    lx=constrain(lx,2,SCR_W-strW(db)-2);
    ly=constrain(ly,CNT_Y+4,SCR_H-FTR_H-14);
    drawStr(lx,ly,db,col,0xFFFF,1);
  };
  plot(T.dFront,0,"F:"); plot(T.dRear,180,"R:");
  plot(T.dLeft,270,"L:"); plot(T.dRight,90,"Ri:");
}

// ══════════════════════════════════════════════════════════════════════
//  GAMES MENU
// ══════════════════════════════════════════════════════════════════════
void drawGamesMenu(Touch& t){
  if(scrDirty){ fillRect(0,CNT_Y,SCR_W,CNT_H,C_BG); scrDirty=false; }
  drawHeader("GAMES FOR AJ"); drawFooter();
  if(drawBackBtn(t)){ curScr=SCR_MAIN; scrDirty=true; firstMainDraw=true; t.pressed=false; return; }

  drawStrC(SCR_W/2,CNT_Y+10,"Choose your game!",C_MGRAY,0xFFFF,1);

  struct { const char* n; Screen s; uint16_t c; const char* d; const char* ic; } g[]={
    {"COLOURS",  SCR_GAME_COLOR, C_RED,    "Match the colour",  "C"},
    {"SHAPES",   SCR_GAME_SHAPE, C_CYAN,   "Find the shape",    "S"},
    {"COUNTING", SCR_GAME_COUNT, C_YLLOW,  "Count the stars",   "*"},
  };
  int gh=(CNT_H-52)/3;
  for(int i=0;i<3;i++){
    int gy=CNT_Y+30+i*(gh+8);
    // Card
    fillGrad(6,gy,SCR_W-12,gh,C_SURF,C_BG);
    hexFrame(6,gy,SCR_W-12,gh,g[i].c,10);
    // Neon accent
    fillRect(6,gy,SCR_W-12,3,g[i].c);
    // Icon circle
    fillCircle(44,gy+gh/2,22,C_SURF2);
    drawCircle(44,gy+gh/2,22,g[i].c);
    drawCircle(44,gy+gh/2,20,g[i].c);
    drawStrC(44,gy+gh/2-8,g[i].ic,g[i].c,0xFFFF,3);
    // Text
    drawStr(76,gy+gh/2-12,g[i].n,g[i].c,0xFFFF,2);
    drawStr(76,gy+gh/2+6,g[i].d,C_MGRAY,0xFFFF,1);
    // Arrow
    drawStr(SCR_W-22,gy+gh/2-4,">",g[i].c,0xFFFF,2);
    if(hit(t,6,gy,SCR_W-12,gh)){
      gTarget=random(i==2?5:(i==0?4:3));
      if(i==2)gTarget++;
      gScore=0;gFeedbackUntil=0;
      curScr=g[i].s;scrDirty=true;t.pressed=false;return;
    }
  }
}

void gameCorrect(uint8_t s,int nm){
  gScore++;
  fillRect(0,CNT_Y,SCR_W,CNT_H,0x0240u);
  // Large check-like indicator
  for(int i=0;i<3;i++) drawCircle(SCR_W/2,SCR_H/2-30,30+i*4,C_MINT);
  fillCircle(SCR_W/2,SCR_H/2-30,28,C_MINT);
  drawStrC(SCR_W/2,SCR_H/2-38,"OK",C_VOID,0xFFFF,4);
  drawStrC(SCR_W/2,SCR_H/2+10,"CORRECT!",C_MINT,0xFFFF,3);
  char sb[20];snprintf(sb,sizeof(sb),"Score: %d",gScore);
  drawStrC(SCR_W/2,SCR_H/2+40,sb,C_WHITE,0xFFFF,2);
  gTarget=random(nm)+(s==SCR_GAME_COUNT?1:0);
  gFeedbackUntil=millis()+900;
}

void gameWrong(){
  fillRect(0,CNT_Y,SCR_W,CNT_H,0x2800u);
  for(int i=0;i<3;i++) drawCircle(SCR_W/2,SCR_H/2-30,30+i*4,C_CORAL);
  fillCircle(SCR_W/2,SCR_H/2-30,28,C_CORAL);
  drawStrC(SCR_W/2,SCR_H/2-38,"X",C_WHITE,0xFFFF,4);
  drawStrC(SCR_W/2,SCR_H/2+10,"Try Again!",C_CORAL,0xFFFF,2);
  gFeedbackUntil=millis()+700;
}

void drawGameColor(Touch& t){
  if(millis()<gFeedbackUntil)return;
  if(scrDirty){fillRect(0,CNT_Y,SCR_W,CNT_H,C_BG);scrDirty=false;}
  drawHeader("COLOUR MATCH");drawFooter();
  if(drawBackBtn(t)){curScr=SCR_GAMES;scrDirty=true;t.pressed=false;return;}
  char sb[16];snprintf(sb,sizeof(sb),"Score: %d",gScore);
  drawStrR(SCR_W-6,CNT_Y+8,sb,C_CYAN,0xFFFF,1);
  drawStrC(SCR_W/2,CNT_Y+26,"Touch the colour:",C_LGRAY,0xFFFF,1);
  drawStrC(SCR_W/2,CNT_Y+44,GCNAMES[gTarget],GCOLS[gTarget],0xFFFF,4);
  int bw=(SCR_W-22)/2,bh=56;
  for(int i=0;i<4;i++){
    int bx=6+(i%2)*(bw+10);
    int by=CNT_Y+108+(i/2)*(bh+10);
    fillRR(bx,by,bw,bh,10,GCOLS[i]);
    drawRR(bx,by,bw,bh,10,C_WHITE);
    drawCircle(bx+bw/2,by+bh/2,14,C_WHITE);
    if(hit(t,bx,by,bw,bh)){t.pressed=false;(i==gTarget)?gameCorrect(SCR_GAME_COLOR,4):gameWrong();scrDirty=true;return;}
  }
}

void drawGameShape(Touch& t){
  const char* SN[]={"CIRCLE","SQUARE","TRIANGLE"};
  if(millis()<gFeedbackUntil)return;
  if(scrDirty){fillRect(0,CNT_Y,SCR_W,CNT_H,C_BG);scrDirty=false;}
  drawHeader("SHAPE MATCH");drawFooter();
  if(drawBackBtn(t)){curScr=SCR_GAMES;scrDirty=true;t.pressed=false;return;}
  char sb[16];snprintf(sb,sizeof(sb),"Score: %d",gScore);
  drawStrR(SCR_W-6,CNT_Y+8,sb,C_CYAN,0xFFFF,1);
  drawStrC(SCR_W/2,CNT_Y+26,"Find the shape:",C_LGRAY,0xFFFF,1);
  drawStrC(SCR_W/2,CNT_Y+44,SN[gTarget],C_CYAN,0xFFFF,3);
  int bw=SCR_W-20,bh=58,by=CNT_Y+110;
  for(int i=0;i<3;i++){
    neonBox(10,by,bw,bh,C_CYAN,C_SURF);
    int cx2=54,cy2=by+bh/2,r=20;
    if(i==0)fillCircle(cx2,cy2,r,C_CYAN);
    else if(i==1)fillRect(cx2-r,cy2-r,r*2,r*2,C_CYAN);
    else fillTri(cx2,cy2-r,cx2-r,cy2+r,cx2+r,cy2+r,C_CYAN);
    drawStr(88,by+bh/2-8,SN[i],C_WHITE,0xFFFF,2);
    if(hit(t,10,by,bw,bh)){t.pressed=false;(i==gTarget)?gameCorrect(SCR_GAME_SHAPE,3):gameWrong();scrDirty=true;return;}
    by+=bh+8;
  }
}

void drawGameCount(Touch& t){
  if(millis()<gFeedbackUntil)return;
  if(scrDirty){fillRect(0,CNT_Y,SCR_W,CNT_H,C_BG);scrDirty=false;}
  drawHeader("COUNT THE STARS");drawFooter();
  if(drawBackBtn(t)){curScr=SCR_GAMES;scrDirty=true;t.pressed=false;return;}
  char sb[16];snprintf(sb,sizeof(sb),"Score: %d",gScore);
  drawStrR(SCR_W-6,CNT_Y+8,sb,C_CYAN,0xFFFF,1);
  drawStrC(SCR_W/2,CNT_Y+26,"How many stars?",C_LGRAY,0xFFFF,1);
  // Stars panel
  neonBox(10,CNT_Y+52,SCR_W-20,80,C_YLLOW,C_SURF);
  int sp=(gTarget>1)?((SCR_W-60)/(gTarget-1)):0;
  for(int i=0;i<gTarget;i++){
    int sx=(gTarget>1)?(26+i*sp):(SCR_W/2-9);
    drawStrC(sx,CNT_Y+76,"*",C_YLLOW,C_SURF,4);
  }
  // Number buttons
  int bw=(SCR_W-32)/5,bh=58,by=CNT_Y+148;
  for(int i=0;i<5;i++){
    int bx=6+i*(bw+4);
    char nb[3];snprintf(nb,sizeof(nb),"%d",i+1);
    bool sel=(i+1==gTarget);
    fillRR(bx,by,bw,bh,8,sel?0x0840u:C_SURF2);
    drawRR(bx,by,bw,bh,8,C_YLLOW);
    if(sel)drawRR(bx+2,by+2,bw-4,bh-4,6,C_YLLOW);
    drawStrC(bx+bw/2,by+bh/2-16,nb,C_YLLOW,0xFFFF,4);
    if(hit(t,bx,by,bw,bh)){t.pressed=false;(i+1==gTarget)?gameCorrect(SCR_GAME_COUNT,5):gameWrong();scrDirty=true;return;}
  }
}

// ══════════════════════════════════════════════════════════════════════
//  INFO SCREEN
// ══════════════════════════════════════════════════════════════════════
void drawInfo(Touch& t){
  if(scrDirty){fillRect(0,CNT_Y,SCR_W,CNT_H,C_BG);scrDirty=false;}
  drawHeader("SYSTEM INFO");drawFooter();
  if(drawBackBtn(t)){curScr=SCR_MAIN;scrDirty=true;firstMainDraw=true;t.pressed=false;return;}

  int y=CNT_Y+8,rh=22,cw=SCR_W-12;
  // Card
  fillRR(6,y,cw,rh*10+12,6,C_SURF);
  drawRR(6,y,cw,rh*10+12,6,C_LINE);
  fillRect(6,y,cw,3,C_PURPLE);

  auto ir=[&](const char* l,const char* v,uint16_t vc){
    drawStr(14,y+6,l,C_MGRAY,C_SURF,1);
    drawStrR(SCR_W-14,y+6,v,vc,C_SURF,1);
    drawH(10,y+rh-1,cw-8,C_LINE);
    y+=rh;
  };
  char buf[28];
  ir("Mega Firmware", T.fw.length()?T.fw.c_str():"--", C_CYAN);
  snprintf(buf,sizeof(buf),"%.2f V",T.volt);
  ir("Battery Voltage",buf,T.volt>7.5f?C_MINT:T.volt>7.0f?C_AMBER:C_CORAL);
  snprintf(buf,sizeof(buf),"%d%%",T.pct);
  ir("Battery Level",buf,T.pct>50?C_MINT:C_AMBER);
  snprintf(buf,sizeof(buf),"%.2f A",T.amps);
  ir("Current Draw",buf,C_LGRAY);
  ir("R3 Motors",T.r3ok?"LINKED":"OFFLINE",T.r3ok?C_MINT:C_CORAL);
  ir("ESP32 Bridge",T.espok?"LINKED":"WAITING",T.espok?C_MINT:C_AMBER);
  ir("S9 Android",T.s9ok?"LINKED":"WAITING",T.s9ok?C_MINT:C_AMBER);
  ir("Auto Mode",T.autoM?"ACTIVE":"OFF",T.autoM?C_MINT:C_DGRAY);
  snprintf(buf,sizeof(buf),"%lu s",millis()/1000);
  ir("Pico Uptime",buf,C_LGRAY);
  snprintf(buf,sizeof(buf),"%lu s ago",(millis()-lastMegaRx)/1000);
  ir("Last Mega RX",buf,megaLinked?C_MINT:C_CORAL);
}

// ══════════════════════════════════════════════════════════════════════
//  SENSORS SCREEN
// ══════════════════════════════════════════════════════════════════════
void drawSensors(Touch& t){
  if(scrDirty){fillRect(0,CNT_Y,SCR_W,CNT_H,C_BG);scrDirty=false;}
  drawHeader("SENSOR CONFIG");drawFooter();
  if(drawBackBtn(t)){curScr=SCR_MAIN;scrDirty=true;firstMainDraw=true;t.pressed=false;return;}

  struct SBtn { const char* id;bool* f;const char* l; };
  SBtn sb[]={
    {"DHT",&SF.dht,"DHT Temp"},{"GAS",&SF.gas,"Gas"},
    {"FLAME",&SF.flame,"Flame"},{"PIR",&SF.pir,"PIR"},
    {"TILT",&SF.tilt,"Tilt"},{"IR",&SF.ir,"IR Obst."},
    {"US",&SF.us,"Ultrasonic"},{"CURRENT",&SF.cur,"Current"},
  };
  int bw=(SCR_W-18)/2,bh=42;
  for(int i=0;i<8;i++){
    int bx=6+(i%2)*(bw+6),by=CNT_Y+6+(i/2)*(bh+6);
    bool on=*sb[i].f;
    uint16_t col=on?C_MINT:C_CORAL;
    fillRR(bx,by,bw,bh,8,on?0x0240u:0x2800u);
    drawRR(bx,by,bw,bh,8,col);
    if(on)fillRect(bx+4,by,bw-8,2,col);
    pulseDot(bx+14,by+bh/2,5,col,on);
    drawStr(bx+26,by+bh/2-4,sb[i].l,col,0xFFFF,1);
    drawStrR(bx+bw-6,by+bh/2-4,on?"ON":"OFF",col,0xFFFF,1);
    if(hit(t,bx,by,bw,bh)){
      *sb[i].f=!(*sb[i].f);
      MEGA_SERIAL.print("TOGGLE_SENSOR:");MEGA_SERIAL.print(sb[i].id);
      MEGA_SERIAL.println(*sb[i].f?":ON":":OFF");
      scrDirty=true;t.pressed=false;
    }
  }
}

// ══════════════════════════════════════════════════════════════════════
//  ALERT OVERLAY
// ══════════════════════════════════════════════════════════════════════
void drawAlert(Touch& t){
  const int AX=16,AY=90,AW=SCR_W-32,AH=260;
  fillGrad(AX,AY,AW,AH,alertCol,C_BG);
  hexFrame(AX,AY,AW,AH,alertCol,14);
  drawH(AX,AY+2,AW,alertCol);
  drawH(AX,AY+4,AW,C_WHITE);
  drawStrC(AX+AW/2,AY+24,alertTitle.c_str(),C_WHITE,0xFFFF,3);
  drawH(AX+20,AY+66,AW-40,C_WHITE);
  drawStrC(AX+AW/2,AY+80,alertMsg.c_str(),C_WHITE,0xFFFF,2);
  // Dismiss button
  int bx=AX+(AW-110)/2,by=AY+AH-54;
  fillRR(bx,by,110,40,8,C_WHITE);
  drawRR(bx,by,110,40,8,alertCol);
  drawStrC(bx+55,by+16,"DISMISS",alertCol,0xFFFF,2);
  if(hit(t,bx,by,110,40)||(alertTs&&millis()-alertTs>10000)){
    alertOn=false;alertTs=0;scrDirty=true;firstMainDraw=true;t.pressed=false;
  }
}

// ══════════════════════════════════════════════════════════════════════
//  MEGA PROTOCOL PARSER
// ══════════════════════════════════════════════════════════════════════
void raisAlert(const char* ti,const char* msg,uint16_t c){
  alertTitle=ti;alertMsg=msg;alertCol=c;alertOn=true;alertTs=millis();
}
void parseStat(const String& s){
  // V31: gas:temp:hum:haz:pir:tilt:ir:volt:pct:amps
  String f[11];int n=0,st=5;
  for(int i=5;i<=(int)s.length()&&n<11;i++){
    if(i==(int)s.length()||s[i]==':'){f[n++]=s.substring(st,i);st=i+1;}
  }
  if(n<10) return;
  T.gas=f[0].toInt();T.temp=f[1].toFloat();T.hum=f[2].toFloat();
  T.haz=f[3].toInt();T.pir=f[4].toInt();T.tilt=f[5].toInt();
  T.ir=f[6].toInt();
  T.volt=f[7].toFloat();T.pct=f[8].toInt();T.amps=f[9].toFloat();
}
void parseUS(const String& s){
  String tmp=s.substring(3);String f[4];int n=0,st=0;
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
  T.r3ok=(s.indexOf("R3:OK")>=0);
  T.espok=(s.indexOf("ESP:OK")>=0);
  T.s9ok=(s.indexOf("S9:OK")>=0);
  int fi=s.indexOf("FW:");
  if(fi>=0){int fe=s.indexOf('|',fi);T.fw=(fe>0)?s.substring(fi+3,fe):s.substring(fi+3);}
}
void parseSensStatus(const String& s){
  SF.dht=s.indexOf("DHT:1")>=0; SF.gas=s.indexOf("GAS:1")>=0;
  SF.flame=s.indexOf("FLAME:1")>=0; SF.pir=s.indexOf("PIR:1")>=0;
  SF.tilt=s.indexOf("TILT:1")>=0; SF.ir=s.indexOf("IR:1")>=0;
  SF.us=s.indexOf("US:1")>=0; SF.cur=s.indexOf("CUR:1")>=0;
}
void handleMegaLine(String& line){
  line.trim();if(!line.length())return;
  lastMegaRx=millis();megaLinked=true;
  int crcIdx=line.indexOf("|CRC:");
  if(crcIdx>0)line=line.substring(0,crcIdx);
  if(!line.length())return;
  if(line.startsWith("STAT:"))           parseStat(line);
  else if(line.startsWith("US:"))        parseUS(line);
  else if(line.startsWith("STATUS|")||line.startsWith("MEGA_READY|")||line.startsWith("SYSTEM|READY|")) parseStatus(line);
  else if(line.startsWith("CONN_STATUS|"))  parseStatus(line);
  else if(line.startsWith("SENS_ST|"))      parseSensStatus(line);
  else if(line.startsWith("MODE:"))      T.mode=line.substring(5);
  else if(line=="PING")                  MEGA_SERIAL.println("PONG");
  else if(line.startsWith("BAT:WARN"))    raisAlert("Battery Low","Charge soon",C_AMBER);
  else if(line.startsWith("BAT:LOW"))     raisAlert("Battery Critical","Plug in NOW",C_CORAL);
  else if(line=="CHARGE:MANUAL:CONNECTED")    raisAlert("Charging","Manual charger connected",C_MINT);
  else if(line=="CHARGE:MANUAL:DISCONNECTED") raisAlert("Ready","Charger disconnected",C_CYAN);
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
    if(c=='\n'){handleMegaLine(megaBuf);megaBuf="";}
    else if(c!='\r'){megaBuf+=c;if(megaBuf.length()>128)megaBuf="";}
  }
}

// ══════════════════════════════════════════════════════════════════════
//  SETUP & LOOP
// ══════════════════════════════════════════════════════════════════════
void setup(){
  Serial.begin(115200);  // USB debug — echoes Mega serial to PC for diagnostics
  delay(500);
  displayInit();
  showStartupSplash();  // Reinsma + BuddyBot logo splash with orbital particles

  // Touch
  pinMode(PIN_CTP_RST, OUTPUT);
  digitalWrite(PIN_CTP_RST, LOW);  delay(20);
  digitalWrite(PIN_CTP_RST, HIGH); delay(200);  // FT6336U needs 200ms after reset
  // FIX #2: INPUT_PULLUP — FT6336U INT is open-drain; floats without pullup
  // and the touch condition (!digitalRead) would never reliably trigger.
  pinMode(PIN_CTP_INT, INPUT_PULLUP);

  // FIX #3: Use Wire (I2C0) matching the DisplayTest — Wire1 default routing
  // on RP2040 does not map to GP26/GP27 unless explicitly configured.
  Wire.setSDA(PIN_CTP_SDA);
  Wire.setSCL(PIN_CTP_SCL);
  Wire.begin();
  // FIX #4: Set 400 kHz fast-mode (FT6336U supports up to 400 kHz)
  Wire.setClock(400000);
  delay(100);

  // Mega UART
  Serial1.setTX(0);Serial1.setRX(1);
  MEGA_SERIAL.begin(115200);
  delay(200);
  MEGA_SERIAL.println("PONG");
  MEGA_SERIAL.println("PING_PICO:0");

  randomSeed(analogRead(A0));
  scrDirty=true;
}

void loop(){
  handleMegaSerial();

  if(millis()-lastPing>5000){
    lastPing=millis();
    MEGA_SERIAL.print("PING_PICO:");MEGA_SERIAL.println(pingSeq++);
    if(pingSeq>9999)pingSeq=0;
  }
  if(megaLinked&&millis()-lastMegaRx>12000)megaLinked=false;
  if(curScr==SCR_BOOT&&megaLinked){curScr=SCR_MAIN;scrDirty=true;firstMainDraw=true;}

  // Touch — debounced 80ms
  Touch t={0,0,false};
  if(millis()-lastTouchMs>80&&!digitalRead(PIN_CTP_INT)){
    t=readTouch();if(t.pressed)lastTouchMs=millis();
  }

  // Global E-STOP
  if(hit(t,6,SCR_H-FTR_H+8,86,32)){
    if(T.estop){MEGA_SERIAL.println("ESTOP_CLEAR");T.estop=false;}
    else        {MEGA_SERIAL.println("EMERGENCY_STOP");T.estop=true;}
    scrDirty=true;firstMainDraw=true;t.pressed=false;
  }

  if(alertOn){drawHeader("ALERT");drawFooter();drawAlert(t);return;}

  // NO timed scrDirty=true — partial updates handle telemetry
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
