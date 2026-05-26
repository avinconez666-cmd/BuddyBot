/*
 * ════════════════════════════════════════════════════════════════════
 *  BUDDYBOT  —  Remote Control  ·  XC3812 SAMD21  ·  V3.0
 *  4-Button  |  Toggle-to-drive  |  Neon Cyberpunk UI
 * ════════════════════════════════════════════════════════════════════
 *
 *  WIRING
 *  ─────────────────────────────────────────────────────────────────
 *  OLED XC3726 (SPI):
 *    CS   → 10        DC   → 9        RES  → 5
 *    SDA  → MOSI pad  SCL  → SCK pad  (underside of XC3812)
 *    VCC  → 3V        GND  → GND
 *
 *  Buttons (INPUT_PULLUP — other leg to GND):
 *    UP    → D11      DOWN  → D12
 *    LEFT  → D13      RIGHT → A2
 *
 *  RF Transmitter 433MHz:
 *    DATA  → D6    VCC → 3.3V    GND → GND
 *
 *  Battery ADC: LiPo+ → 2×100kΩ divider → A0
 *
 *  CONTROLS
 *  ─────────────────────────────────────────────────────────────────
 *  UP     press        → toggle Forward ON/OFF
 *  DOWN   press        → toggle Reverse ON/OFF
 *  LEFT   hold         → spin Left
 *  RIGHT  hold         → spin Right
 *  LEFT+RIGHT 600ms    → E-STOP toggle
 *  DOWN   hold 800ms   → open Menu
 *
 *  In Menu:
 *    UP/DOWN=scroll   LEFT=confirm   RIGHT=back
 *
 * ════════════════════════════════════════════════════════════════════
 */

#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1351.h>
#include <RCSwitch.h>

// ── Pins ──────────────────────────────────────────────────────────
#define OLED_CS   10
#define OLED_DC    9
#define OLED_RST   5
#define BTN_UP    A3   // moved off D11 (MOSI)
#define BTN_DOWN  A4   // moved off D12 (was ok but keeping layout tidy)
#define BTN_LEFT  A5   // moved off D13 (SCK)
#define BTN_RIGHT A2
#define RF_PIN     6
#define BAT_PIN   A0

#define OLED_W 128
#define OLED_H 128

// ── RF codes ─────────────────────────────────────────────────────
#define RF_FORWARD  0xA10001UL
#define RF_BACKWARD 0xA10002UL
#define RF_LEFT     0xA10003UL
#define RF_RIGHT    0xA10004UL
#define RF_STOP     0xA10005UL
#define RF_SPD_SLOW 0xA10030UL
#define RF_SPD_NORM 0xA10031UL
#define RF_SPD_FAST 0xA10032UL
#define RF_MODE_NRM 0xA10010UL
#define RF_MODE_BOD 0xA10011UL
#define RF_MODE_DOG 0xA10012UL
#define RF_MODE_UNH 0xA10013UL
#define RF_ESTOP    0xA100FFUL
#define RF_DANCE    0xA10020UL
#define RF_DEFENSE  0xA10021UL

// ── Neon palette (RGB565) ─────────────────────────────────────────
#define C_BK     0x0000u
#define C_BG     0x0020u
#define C_SURF   0x0841u
#define C_CYAN   0x07FFu
#define C_DCYAN  0x03EFu
#define C_XCYAN  0x001Fu
#define C_MINT   0x07E4u
#define C_AMBER  0xFD20u
#define C_CORAL  0xF944u
#define C_PURPLE 0x781Fu
#define C_WHITE  0xFFFFu
#define C_LGRAY  0x8C71u
#define C_MGRAY  0x4228u
#define C_DGRAY  0x2104u

// ── Objects ───────────────────────────────────────────────────────
// Hardware SPI — uses board SPI peripheral (MOSI/SCK = underside pads of XC3812)
Adafruit_SSD1351 oled(OLED_W,OLED_H,&SPI,OLED_CS,OLED_DC,OLED_RST);
RCSwitch rf;

// ── Screens ───────────────────────────────────────────────────────
enum Screen { SCR_DRIVE, SCR_MODE, SCR_SPECIAL, SCR_SPEED };
Screen curScr = SCR_DRIVE;
bool   redraw = true;

// ── Selections ───────────────────────────────────────────────────
uint8_t modeIdx = 0, specIdx = 0, spdIdx = 1;

const char*    modeNames[] = { "NORMAL","BODYGUARD","DOG GUARD","UNHINGED" };
const uint16_t modeCols[]  = { C_CYAN,C_AMBER,C_MINT,C_CORAL };
const uint32_t modeCodes[] = { RF_MODE_NRM,RF_MODE_BOD,RF_MODE_DOG,RF_MODE_UNH };

const char*    specNames[] = { "DANCE","DEFENSE","E-STOP","< BACK" };
const uint16_t specCols[]  = { C_PURPLE,C_AMBER,C_CORAL,C_LGRAY };
const uint32_t specCodes[] = { RF_DANCE,RF_DEFENSE,RF_ESTOP,0 };

const char*    spdNames[]  = { "SLOW","NORMAL","FAST" };
const uint16_t spdCols[]   = { C_MINT,C_CYAN,C_CORAL };
const uint32_t spdCodes[]  = { RF_SPD_SLOW,RF_SPD_NORM,RF_SPD_FAST };

// ── Drive state ───────────────────────────────────────────────────
bool     eStop    = false;
bool     driveFwd = false;
bool     driveRev = false;
uint32_t lastRFCode = RF_STOP;
bool     menuConsumed = false;

// ── Buttons ───────────────────────────────────────────────────────
#define NUM_BTNS 4
#define DEB_MS   22
#define LONG_MS  600
#define MENU_MS  800
#define RF_MS     90

struct Btn {
  uint8_t pin;
  bool state, rawPrev, pressed, released, held;
  unsigned long downAt;
};

Btn btns[NUM_BTNS];
#define B_UP    0
#define B_DOWN  1
#define B_LEFT  2
#define B_RIGHT 3

void initBtns() {
  const uint8_t pins[NUM_BTNS] = { BTN_UP,BTN_DOWN,BTN_LEFT,BTN_RIGHT };
  for (int i=0;i<NUM_BTNS;i++) {
    btns[i]={pins[i],HIGH,HIGH,false,false,false,0};
    pinMode(pins[i],INPUT_PULLUP);
  }
}

void readBtns() {
  for (int i=0;i<NUM_BTNS;i++) {
    btns[i].pressed=btns[i].released=false;
    bool raw=digitalRead(btns[i].pin);
    if (raw!=btns[i].state) {
      btns[i].state=raw;
      if (raw==LOW) { btns[i].pressed=true; btns[i].held=true; btns[i].downAt=millis(); }
      else          { btns[i].released=true; btns[i].held=false; }
    }
  }
}

bool longHeld(int i,unsigned long ms=LONG_MS) {
  return btns[i].held&&(millis()-btns[i].downAt>=ms);
}

// ── Battery ───────────────────────────────────────────────────────
uint8_t batPct=100;
unsigned long lastBat=0;

void readBat() {
  if (millis()-lastBat<12000) return;
  lastBat=millis();
  float v=(analogRead(BAT_PIN)/1023.0f)*3.3f*2.0f;
  batPct=(uint8_t)constrain((v-3.0f)/1.2f*100.0f,0.0f,100.0f);
}

// ════════════════════════════════════════════════════════════════════
//  DRAW HELPERS
// ════════════════════════════════════════════════════════════════════

void drawGrid() {
  for (int x=0;x<OLED_W;x+=16) oled.drawFastVLine(x,0,OLED_H,0x0421u);
  for (int y=0;y<OLED_H;y+=16) oled.drawFastHLine(0,y,OLED_W,0x0421u);
}

void hexCorners(int x,int y,int w,int h,uint16_t col,int sz=8) {
  oled.drawFastHLine(x,y,sz,col);    oled.drawFastVLine(x,y,sz,col);
  oled.drawFastHLine(x+w-sz,y,sz,col); oled.drawFastVLine(x+w-1,y,sz,col);
  oled.drawFastHLine(x,y+h-1,sz,col); oled.drawFastVLine(x,y+h-sz,sz,col);
  oled.drawFastHLine(x+w-sz,y+h-1,sz,col); oled.drawFastVLine(x+w-1,y+h-sz,sz,col);
}

void drawC(int y,const char* s,uint16_t c,uint8_t sz=1) {
  oled.setTextSize(sz); oled.setTextColor(c);
  oled.setCursor((OLED_W-(int)strlen(s)*6*sz)/2,y);
  oled.print(s);
}

void glowBar(int x,int y,int w,int h,float frac,uint16_t col) {
  oled.fillRoundRect(x,y,w,h,2,C_SURF);
  oled.drawRoundRect(x,y,w,h,2,col);
  int fill=(int)(frac*(w-2));
  if (fill>0) {
    oled.fillRect(x+1,y+1,fill,h-2,col);
    oled.drawFastHLine(x+1,y+1,fill,C_WHITE);
  }
}

void drawBat(int x,int y) {
  uint16_t c=batPct>50?C_MINT:batPct>20?C_AMBER:C_CORAL;
  oled.drawRoundRect(x,y,20,10,2,c);
  oled.fillRect(x+20,y+3,2,4,c);
  int f=(int)(batPct/100.0f*17.0f);
  if(f>0) oled.fillRect(x+2,y+2,f,6,c);
  oled.setTextSize(1); oled.setTextColor(c);
  char b[5]; snprintf(b,5,"%d%%",batPct);
  oled.setCursor(x+24,y+1); oled.print(b);
}

void drawHdr(const char* title,uint16_t col) {
  oled.fillRect(0,0,OLED_W,16,C_BK);
  oled.drawFastHLine(0,15,OLED_W,col);
  oled.drawFastHLine(0,14,OLED_W,C_SURF);
  oled.drawFastHLine(0,13,OLED_W,C_XCYAN);
  oled.setTextSize(1); oled.setTextColor(col);
  oled.setCursor((OLED_W-(int)strlen(title)*6)/2,4);
  oled.print(title);
  drawBat(OLED_W-52,3);
}

void drawMenu(int sel,int count,const char** names,const uint16_t* cols) {
  for (int i=0;i<count;i++) {
    bool act=(i==sel);
    int y=20+i*24;
    uint16_t bg=act?cols[i]:C_BK;
    uint16_t fg=act?C_BK:cols[i];
    oled.fillRect(4,y,120,20,bg);
    oled.drawRect(4,y,120,20,cols[i]);
    oled.drawRect(5,y+1,118,18,cols[i]);
    if (act) {
      oled.drawFastHLine(8,y,112,C_WHITE);
      oled.drawFastHLine(8,y+19,112,C_WHITE);
    }
    oled.setTextSize(1); oled.setTextColor(fg);
    oled.setCursor((OLED_W-(int)strlen(names[i])*6)/2,y+6);
    oled.print(names[i]);
  }
  drawC(118,"UP/DN=scroll  LEFT=ok",C_DGRAY);
}

// ── Drive direction block ─────────────────────────────────────────
void driveBlock(int cx,int cy,int w,int h,
                const char* lbl,bool active,uint16_t col) {
  uint16_t bg=active?col:C_BK;
  uint16_t fg=active?C_BK:col;
  oled.fillRect(cx-w/2,cy-h/2,w,h,bg);
  oled.drawRect(cx-w/2,cy-h/2,w,h,col);
  oled.drawRect(cx-w/2+1,cy-h/2+1,w-2,h-2,col);
  if (active) {
    oled.drawFastHLine(cx-w/2+2,cy-h/2,w-4,C_WHITE);
    oled.drawFastHLine(cx-w/2+2,cy+h/2-1,w-4,C_WHITE);
  }
  oled.setTextSize(1); oled.setTextColor(fg);
  oled.setCursor(cx-(int)strlen(lbl)*3,cy-4);
  oled.print(lbl);
}

void drawDriveScreen() {
  bool up=driveFwd, dn=driveRev;
  bool l=btns[B_LEFT].held, r=btns[B_RIGHT].held;
  const int CX=64,CY=72,BW=34,BH=22,GAP=28;

  driveBlock(CX,CY-GAP,BW,BH,"FWD",up,C_CYAN);
  driveBlock(CX,CY+GAP,BW,BH,"REV",dn,C_CYAN);
  driveBlock(CX-GAP,CY,BW,BH,"LEFT",l,C_AMBER);
  driveBlock(CX+GAP,CY,BW,BH,"RGHT",r,C_AMBER);

  uint16_t cc=eStop?C_CORAL:(up||dn||l||r)?C_MINT:C_MGRAY;
  oled.fillCircle(CX,CY,9,cc);
  oled.drawCircle(CX,CY,9,C_WHITE);
  oled.drawCircle(CX,CY,10,cc);
  if (eStop) {
    oled.setTextSize(1); oled.setTextColor(C_WHITE);
    oled.setCursor(CX-6,CY-4); oled.print("E!");
  }
  if (eStop) {
    oled.fillRect(0,17,OLED_W,11,C_CORAL);
    drawC(19,"!! E-STOP ACTIVE !!",C_WHITE);
  }

  // Status row
  char spb[8]; snprintf(spb,sizeof(spb),"%s",spdNames[spdIdx]);
  oled.fillRoundRect(2,108,56,14,3,C_BK);
  oled.drawRoundRect(2,108,56,14,3,spdCols[spdIdx]);
  oled.setTextSize(1); oled.setTextColor(spdCols[spdIdx]);
  oled.setCursor(5,111); oled.print("SPD:"); oled.print(spb);

  oled.fillRoundRect(62,108,64,14,3,C_BK);
  oled.drawRoundRect(62,108,64,14,3,modeCols[modeIdx]);
  oled.setTextSize(1); oled.setTextColor(modeCols[modeIdx]);
  oled.setCursor(65,111); oled.print(modeNames[modeIdx]);

  drawC(124,"HOLD-DN=menu",C_DGRAY);
}

// ── Boot splash ───────────────────────────────────────────────────
void bootSplash() {
  oled.fillScreen(C_BK);
  randomSeed(42);
  for (int i=0;i<40;i++) {
    uint16_t sc=random(3)==0?C_DGRAY:random(2)?C_MGRAY:C_LGRAY;
    oled.drawPixel(random(OLED_W),random(OLED_H),sc);
  }
  // Animated frame
  for (int i=0;i<=OLED_W;i+=4) {
    oled.drawPixel(i,0,C_CYAN);
    oled.drawPixel(OLED_W-1-i,OLED_H-1,C_CYAN);
    delay(3);
  }
  for (int i=0;i<=OLED_H;i+=4) {
    oled.drawPixel(0,i,C_PURPLE);
    oled.drawPixel(OLED_W-1,OLED_H-1-i,C_PURPLE);
    delay(3);
  }
  hexCorners(4,4,OLED_W-8,OLED_H-8,C_CYAN,10);
  hexCorners(6,6,OLED_W-12,OLED_H-12,C_DCYAN,6);

  drawC(22,"BUDDYBOT",C_CYAN,2);
  drawC(40,"REMOTE",C_CYAN,2);
  oled.drawFastHLine(20,58,OLED_W-40,C_PURPLE);
  drawC(62,"v3.0",C_PURPLE,1);
  drawC(72,"XC3812  SAMD21",C_DGRAY,1);
  oled.drawFastHLine(20,82,OLED_W-40,C_DCYAN);
  drawC(88,"Reinsma Innovations",C_DCYAN,1);
  drawC(98,"GENERATIVE AI",C_MGRAY,1);
  drawC(110,"STARTING...",C_MGRAY,1);
  for (int p=0;p<=100;p+=5) {
    glowBar(16,120,OLED_W-32,6,p/100.0f,C_CYAN);
    delay(28);
  }
  delay(300);
  for (int b=0;b<3;b++) {
    oled.fillScreen(C_CYAN); delay(35);
    oled.fillScreen(C_BK);   delay(55);
  }
}

// ── Drive logic ───────────────────────────────────────────────────
void processDrive() {
  static unsigned long lastRFSend=0;
  unsigned long now=millis();

  // E-STOP: both LEFT+RIGHT held
  if (btns[B_LEFT].held&&btns[B_RIGHT].held&&
      longHeld(B_LEFT,LONG_MS)&&longHeld(B_RIGHT,LONG_MS)) {
    eStop=!eStop;
    rf.send(eStop?RF_ESTOP:RF_STOP,24);
    if(eStop) driveFwd=driveRev=false;
    btns[B_LEFT].held=btns[B_RIGHT].held=false;
    redraw=true; delay(300); return;
  }

  if(eStop) return;

  // Menu: hold DOWN
  if(longHeld(B_DOWN,MENU_MS)&&!menuConsumed) {
    menuConsumed=true;
    driveFwd=driveRev=false;
    rf.send(RF_STOP,24);
    curScr=SCR_MODE; redraw=true;
    oled.fillRect(0,16,OLED_W,OLED_H-16,C_BK);
    drawGrid(); return;
  }
  if(!btns[B_DOWN].held) menuConsumed=false;

  // UP: toggle forward
  if(btns[B_UP].pressed) {
    driveRev=false; driveFwd=!driveFwd; redraw=true;
  }
  // DOWN: toggle reverse
  if(btns[B_DOWN].pressed&&!menuConsumed) {
    driveFwd=false; driveRev=!driveRev; redraw=true;
  }
  // LEFT/RIGHT: cancel toggles
  if(btns[B_LEFT].pressed||btns[B_RIGHT].pressed) {
    driveFwd=driveRev=false; redraw=true;
  }

  uint32_t cmd=RF_STOP;
  if     (driveFwd)           cmd=RF_FORWARD;
  else if(driveRev)           cmd=RF_BACKWARD;
  else if(btns[B_LEFT].held)  cmd=RF_LEFT;
  else if(btns[B_RIGHT].held) cmd=RF_RIGHT;

  if(now-lastRFSend>=RF_MS) {
    rf.send(cmd,24);
    lastRFSend=now;
    if(cmd!=lastRFCode){lastRFCode=cmd;redraw=true;}
  }
}

// ── Menu nav ─────────────────────────────────────────────────────
bool menuNav(uint8_t& idx,uint8_t count) {
  if(btns[B_UP].pressed)   {idx=(idx+count-1)%count;redraw=true;}
  if(btns[B_DOWN].pressed) {idx=(idx+1)%count;redraw=true;}
  if(btns[B_LEFT].pressed)  return true;
  if(btns[B_RIGHT].pressed) {
    curScr=SCR_DRIVE; driveFwd=driveRev=false; redraw=true;
    oled.fillRect(0,16,OLED_W,OLED_H-16,C_BK);
    drawGrid(); drawHdr("BUDDYBOT RC",C_CYAN);
  }
  return false;
}

// ── Setup ─────────────────────────────────────────────────────────
void setup() {
  initBtns();
  rf.enableTransmit(RF_PIN);
  rf.setProtocol(1);
  rf.setPulseLength(189);
  rf.setRepeatTransmit(5);
  analogReadResolution(10);
  analogReference(AR_DEFAULT);
  oled.begin();
  oled.setRotation(0);   // rotation 0 — upside-down fixed via flipped Y coords in draw functions
  bootSplash();
  oled.fillScreen(C_BK);
  drawGrid();
  drawHdr("BUDDYBOT RC",C_CYAN);
  lastBat=0; readBat(); redraw=true;
}

// ── Loop ─────────────────────────────────────────────────────────
void loop() {
  readBtns();
  readBat();

  static unsigned long lastHdr=0;
  if(millis()-lastHdr>15000) {
    lastHdr=millis();
    switch(curScr) {
      case SCR_DRIVE:   drawHdr("BUDDYBOT RC",C_CYAN);   break;
      case SCR_MODE:    drawHdr("SELECT MODE",C_PURPLE);  break;
      case SCR_SPECIAL: drawHdr("SPECIAL CMDS",C_AMBER);  break;
      case SCR_SPEED:   drawHdr("SELECT SPEED",C_AMBER);  break;
    }
  }

  switch(curScr) {

    case SCR_DRIVE: {
      processDrive();
      static bool pF=false,pR=false,pL=false,pRi=false,pE=false;
      bool f=driveFwd,r=driveRev,l=btns[B_LEFT].held,ri=btns[B_RIGHT].held;
      if(redraw||f!=pF||r!=pR||l!=pL||ri!=pRi||eStop!=pE) {
        oled.fillRect(0,16,OLED_W,OLED_H-16,C_BK);
        drawGrid(); drawDriveScreen();
        pF=f;pR=r;pL=l;pRi=ri;pE=eStop; redraw=false;
      }
      break;
    }

    case SCR_MODE:
      drawHdr("SELECT MODE",C_PURPLE);
      if(menuNav(modeIdx,4)) {
        rf.send(modeCodes[modeIdx],24);
        curScr=SCR_SPEED; spdIdx=1; redraw=true;
        oled.fillRect(0,16,OLED_W,OLED_H-16,C_BK);
        drawGrid(); drawHdr("SELECT SPEED",C_AMBER);
      }
      if(redraw){oled.fillRect(0,16,OLED_W,OLED_H-16,C_BK);drawGrid();drawMenu(modeIdx,4,modeNames,modeCols);redraw=false;}
      break;

    case SCR_SPECIAL:
      drawHdr("SPECIAL CMDS",C_AMBER);
      if(menuNav(specIdx,4)) {
        if(specIdx==3){curScr=SCR_DRIVE;redraw=true;oled.fillRect(0,16,OLED_W,OLED_H-16,C_BK);drawGrid();drawHdr("BUDDYBOT RC",C_CYAN);}
        else{rf.send(specCodes[specIdx],24);if(specIdx==2){eStop=true;driveFwd=driveRev=false;}delay(150);redraw=true;}
      }
      if(redraw){oled.fillRect(0,16,OLED_W,OLED_H-16,C_BK);drawGrid();drawMenu(specIdx,4,specNames,specCols);redraw=false;}
      break;

    case SCR_SPEED:
      drawHdr("SELECT SPEED",C_AMBER);
      if(menuNav(spdIdx,3)) {
        rf.send(spdCodes[spdIdx],24);
        curScr=SCR_DRIVE; redraw=true;
        oled.fillRect(0,16,OLED_W,OLED_H-16,C_BK);
        drawGrid(); drawHdr("BUDDYBOT RC",C_CYAN);
      }
      if(redraw) {
        oled.fillRect(0,16,OLED_W,OLED_H-16,C_BK); drawGrid();
        for(int i=0;i<3;i++){
          bool sel=(i==spdIdx); int y=22+i*30;
          oled.fillRect(8,y,112,26,sel?spdCols[i]:C_BK);
          oled.drawRect(8,y,112,26,spdCols[i]);
          oled.drawRect(9,y+1,110,24,spdCols[i]);
          if(sel){oled.drawFastHLine(12,y,104,C_WHITE);oled.drawFastHLine(12,y+25,104,C_WHITE);}
          oled.setTextSize(2);
          oled.setTextColor(sel?C_BK:spdCols[i]);
          oled.setCursor((OLED_W-(int)strlen(spdNames[i])*12)/2,y+9);
          oled.print(spdNames[i]);
        }
        drawC(118,"UP/DN=scroll  LEFT=ok",C_DGRAY);
        redraw=false;
      }
      break;
  }
  delay(8);
}
