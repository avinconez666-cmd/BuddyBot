/*
 * ════════════════════════════════════════════════════════════════════
 *  BUDDYBOT Remote Control — XC3812 SAMD21 — V4.0
 *  4 buttons: UP DOWN LEFT RIGHT
 *  Based on working V1 display init (hardware SPI, RST=8, rotation=0)
 * ════════════════════════════════════════════════════════════════════
 *
 *  WIRING (identical to V1 that worked):
 *    OLED CS  → 10    DC  → 9    RST → 8
 *    OLED SDA → MOSI pad (underside)
 *    OLED SCL → SCK  pad (underside)
 *    OLED VCC → 3V   GND → GND
 *
 *    BTN_UP    → A2   (INPUT_PULLUP to GND)
 *    BTN_DOWN  → A3
 *    BTN_LEFT  → A4
 *    BTN_RIGHT → A5
 *
 *    RF DATA   → D6
 *    BAT ADC   → A0 (via 2x100k divider)
 *
 *  CONTROLS:
 *    UP press         toggle Forward ON/OFF
 *    DOWN press       toggle Reverse ON/OFF
 *    LEFT hold        spin left
 *    RIGHT hold       spin right
 *    LEFT+RIGHT 600ms E-STOP toggle
 *    DOWN hold 800ms  open Menu
 *    In menu: UP/DOWN scroll, LEFT confirm, RIGHT back
 * ════════════════════════════════════════════════════════════════════
 */

#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1351.h>
#include <RCSwitch.h>

// ── Pins — identical to working V1 ───────────────────────────────
#define OLED_CS   10
#define OLED_DC    9
#define OLED_RST   8    // same as V1 that worked

#define BTN_UP    A2
#define BTN_DOWN  A3
#define BTN_LEFT  A4
#define BTN_RIGHT A5

#define RF_PIN     6    // same as V1 that worked
#define BAT_PIN   A0

#define OLED_W   128
#define OLED_H   128

// ── RF codes ─────────────────────────────────────────────────────
#define RF_FWD   0xA10001UL
#define RF_REV   0xA10002UL
#define RF_LEFT  0xA10003UL
#define RF_RIGHT 0xA10004UL
#define RF_STOP  0xA10005UL
#define RF_SLOW  0xA10030UL
#define RF_NORM  0xA10031UL
#define RF_FAST  0xA10032UL
#define RF_NRM   0xA10010UL
#define RF_BOD   0xA10011UL
#define RF_DOG   0xA10012UL
#define RF_UNH   0xA10013UL
#define RF_ESTP  0xA100FFUL
#define RF_DNC   0xA10020UL
#define RF_DEF   0xA10021UL

// ── Colours ───────────────────────────────────────────────────────
#define BK    0x0000
#define SURF  0x0841
#define CYN   0x07FF
#define MNT   0x07E4
#define AMB   0xFD20
#define CRL   0xF944
#define PUR   0x781F
#define WHT   0xFFFF
#define DGR   0x2104
#define MGR   0x4228
#define LGR   0x8C71
#define DCY   0x03EF

// ── Objects — identical init to V1 ───────────────────────────────
Adafruit_SSD1351 oled(OLED_W, OLED_H, &SPI, OLED_CS, OLED_DC, OLED_RST);
RCSwitch rf;

// ── State ─────────────────────────────────────────────────────────
enum Scr { DRIVE, MMODE, MSPEC, MSPD };
Scr scr = DRIVE;
bool redraw = true;
uint8_t modeIdx=0, specIdx=0, spdIdx=1;
bool eStopped=false, goFwd=false, goRev=false, menuUsed=false;
uint32_t lastCode=RF_STOP;
uint8_t batPct=100;
unsigned long lastBat=0;

const char* mN[]={"NORMAL","BODYGUARD","DOG GUARD","UNHINGED"};
const uint16_t mC[]={CYN,AMB,MNT,CRL};
const uint32_t mRF[]={RF_NRM,RF_BOD,RF_DOG,RF_UNH};
const char* sN[]={"DANCE","DEFENSE","E-STOP","< BACK"};
const uint16_t sC[]={PUR,AMB,CRL,LGR};
const uint32_t sRF[]={RF_DNC,RF_DEF,RF_ESTP,0};
const char* spN[]={"SLOW","NORMAL","FAST"};
const uint16_t spC[]={MNT,CYN,CRL};
const uint32_t spRF[]={RF_SLOW,RF_NORM,RF_FAST};

// ── Buttons ───────────────────────────────────────────────────────
struct Btn { uint8_t pin; bool st,pr,rl,hd; unsigned long dn; };
Btn B[4]; // 0=UP 1=DN 2=LT 3=RT

void initB() {
  uint8_t p[]={BTN_UP,BTN_DOWN,BTN_LEFT,BTN_RIGHT};
  for(int i=0;i<4;i++){B[i]={p[i],HIGH,false,false,false,0};pinMode(p[i],INPUT_PULLUP);}
}

void readB() {
  for(int i=0;i<4;i++){
    B[i].pr=B[i].rl=false;
    bool r=digitalRead(B[i].pin);
    if(r!=B[i].st){
      B[i].st=r;
      if(r==LOW){B[i].pr=true;B[i].hd=true;B[i].dn=millis();}
      else{B[i].rl=true;B[i].hd=false;}
    }
  }
}

bool lh(int i,unsigned long ms=600){return B[i].hd&&(millis()-B[i].dn>=ms);}

void readBat(){
  if(millis()-lastBat<15000)return; lastBat=millis();
  float v=(analogRead(BAT_PIN)/1023.0f)*3.3f*2.0f;
  batPct=(uint8_t)constrain((v-3.0f)/1.2f*100.0f,0.0f,100.0f);
}

// ── Draw helpers ──────────────────────────────────────────────────
void ctr(int y,const char*s,uint16_t c,uint8_t sz=1){
  oled.setTextSize(sz);oled.setTextColor(c);
  oled.setCursor((128-(int)strlen(s)*6*sz)/2,y);oled.print(s);
}

void hdr(const char*t,uint16_t c){
  oled.fillRect(0,0,128,16,BK);
  oled.drawFastHLine(0,15,128,c);
  oled.setTextSize(1);oled.setTextColor(c);
  oled.setCursor((128-(int)strlen(t)*6)/2,4);oled.print(t);
  // battery
  uint16_t bc=batPct>50?MNT:batPct>20?AMB:CRL;
  oled.drawRect(100,3,18,9,bc);oled.fillRect(118,5,2,4,bc);
  int f=(int)(batPct/100.0f*16);if(f>0)oled.fillRect(101,4,f,7,bc);
}

void box(int x,int y,int w,int h,uint16_t c,bool active){
  oled.fillRect(x,y,w,h,active?c:BK);
  oled.drawRect(x,y,w,h,c);
  if(active)oled.drawFastHLine(x+2,y,w-4,WHT);
}

void menu(int sel,int n,const char**nm,const uint16_t*cl){
  for(int i=0;i<n;i++){
    bool a=(i==sel);int y=20+i*24;
    box(4,y,120,20,cl[i],a);
    oled.setTextSize(1);oled.setTextColor(a?BK:cl[i]);
    oled.setCursor((128-(int)strlen(nm[i])*6)/2,y+6);oled.print(nm[i]);
  }
  ctr(118,"UP/DN  LEFT=ok  RIGHT=bk",DGR);
}

// ── Drive screen ──────────────────────────────────────────────────
void drvBtn(int cx,int cy,int w,int h,const char*l,bool a,uint16_t c){
  box(cx-w/2,cy-h/2,w,h,c,a);
  oled.setTextSize(1);oled.setTextColor(a?BK:c);
  oled.setCursor(cx-(int)strlen(l)*3,cy-4);oled.print(l);
}

void drawDrive(){
  bool u=goFwd,d=goRev,l=B[2].hd,r=B[3].hd;
  drvBtn(64,46,36,20,"FWD",u,CYN);
  drvBtn(64,98,36,20,"REV",d,CYN);
  drvBtn(36,72,36,20,"LFT",l,AMB);
  drvBtn(92,72,36,20,"RGT",r,AMB);
  // centre dot
  uint16_t cc=eStopped?CRL:(u||d||l||r)?MNT:MGR;
  oled.fillCircle(64,72,8,cc);oled.drawCircle(64,72,8,WHT);
  if(eStopped){oled.setTextSize(1);oled.setTextColor(WHT);oled.setCursor(57,68);oled.print("E!");}
  if(eStopped){oled.fillRect(0,17,128,11,CRL);ctr(19,"! E-STOP !",WHT);}
  // status
  oled.fillRect(0,108,128,20,BK);
  oled.drawRect(0,108,62,20,spC[spdIdx]);
  oled.setTextSize(1);oled.setTextColor(spC[spdIdx]);
  oled.setCursor(3,114);oled.print("SPD:");oled.print(spN[spdIdx]);
  oled.drawRect(64,108,64,20,mC[modeIdx]);
  oled.setTextSize(1);oled.setTextColor(mC[modeIdx]);
  oled.setCursor(67,114);oled.print(mN[modeIdx]);
  ctr(124,"HLD-DN=menu",DGR);
}

// ── Logic ─────────────────────────────────────────────────────────
void doDrive(){
  static unsigned long ls=0; unsigned long n=millis();
  // E-STOP
  if(B[2].hd&&B[3].hd&&lh(2)&&lh(3)){
    eStopped=!eStopped;rf.send(eStopped?RF_ESTP:RF_STOP,24);
    if(eStopped)goFwd=goRev=false;
    B[2].hd=B[3].hd=false;redraw=true;delay(300);return;
  }
  if(eStopped)return;
  // Menu
  if(lh(1,800)&&!menuUsed){
    menuUsed=true;goFwd=goRev=false;rf.send(RF_STOP,24);
    scr=MMODE;redraw=true;oled.fillRect(0,16,128,112,BK);return;
  }
  if(!B[1].hd)menuUsed=false;
  // Toggle fwd/rev
  if(B[0].pr){goRev=false;goFwd=!goFwd;redraw=true;}
  if(B[1].pr&&!menuUsed){goFwd=false;goRev=!goRev;redraw=true;}
  if(B[2].pr||B[3].pr){goFwd=goRev=false;redraw=true;}
  // Send
  uint32_t cmd=RF_STOP;
  if(goFwd)cmd=RF_FWD;
  else if(goRev)cmd=RF_REV;
  else if(B[2].hd)cmd=RF_LEFT;
  else if(B[3].hd)cmd=RF_RIGHT;
  if(n-ls>=90){rf.send(cmd,24);ls=n;if(cmd!=lastCode){lastCode=cmd;redraw=true;}}
}

bool nav(uint8_t&idx,uint8_t n){
  if(B[0].pr){idx=(idx+n-1)%n;redraw=true;}
  if(B[1].pr){idx=(idx+1)%n;redraw=true;}
  if(B[2].pr)return true;
  if(B[3].pr){scr=DRIVE;goFwd=goRev=false;redraw=true;oled.fillRect(0,16,128,112,BK);}
  return false;
}

// ── Setup/Loop ────────────────────────────────────────────────────
void setup(){
  initB();
  rf.enableTransmit(RF_PIN);
  rf.setProtocol(1);rf.setPulseLength(189);rf.setRepeatTransmit(5);
  analogReadResolution(10);

  // Exactly as V1 that worked
  oled.begin();
  oled.setRotation(0);
  oled.fillScreen(BK);

  // Simple splash — no animation, just text to confirm display alive
  oled.drawRect(8,20,112,88,CYN);oled.drawRect(9,21,110,86,DCY);
  ctr(32,"BUDDYBOT",CYN,2);ctr(50,"REMOTE",CYN,2);
  ctr(68,"v4.0",PUR,1);
  oled.drawFastHLine(20,80,88,DCY);
  ctr(84,"Reinsma Innovations",LGR,1);
  ctr(98,"GENERATIVE AI",DGR,1);
  delay(2000);
  oled.fillScreen(BK);

  lastBat=0;readBat();redraw=true;
}

void loop(){
  readB();readBat();

  // Refresh header battery every 15s
  static unsigned long lh2=0;
  if(millis()-lh2>15000){lh2=millis();
    switch(scr){
      case DRIVE: hdr("BUDDYBOT RC",CYN);break;
      case MMODE: hdr("MODE",PUR);break;
      case MSPEC: hdr("SPECIAL",AMB);break;
      case MSPD:  hdr("SPEED",AMB);break;
    }
  }

  switch(scr){
    case DRIVE:{
      doDrive();
      static bool pU=false,pD=false,pL=false,pR=false,pE=false;
      bool u=goFwd,d=goRev,l=B[2].hd,r=B[3].hd;
      if(redraw||u!=pU||d!=pD||l!=pL||r!=pR||eStopped!=pE){
        oled.fillRect(0,16,128,92,BK);
        hdr("BUDDYBOT RC",CYN);
        drawDrive();
        pU=u;pD=d;pL=l;pR=r;pE=eStopped;redraw=false;
      }
      break;
    }
    case MMODE:
      if(nav(modeIdx,4)){rf.send(mRF[modeIdx],24);scr=MSPD;spdIdx=1;redraw=true;oled.fillRect(0,16,128,112,BK);}
      if(redraw){hdr("MODE",PUR);oled.fillRect(0,16,128,102,BK);menu(modeIdx,4,mN,mC);redraw=false;}
      break;
    case MSPEC:
      if(nav(specIdx,4)){
        if(specIdx==3){scr=DRIVE;redraw=true;oled.fillRect(0,16,128,112,BK);}
        else{rf.send(sRF[specIdx],24);if(specIdx==2){eStopped=true;goFwd=goRev=false;}delay(150);redraw=true;}
      }
      if(redraw){hdr("SPECIAL",AMB);oled.fillRect(0,16,128,102,BK);menu(specIdx,4,sN,sC);redraw=false;}
      break;
    case MSPD:
      if(nav(spdIdx,3)){rf.send(spRF[spdIdx],24);scr=DRIVE;redraw=true;oled.fillRect(0,16,128,112,BK);}
      if(redraw){
        hdr("SPEED",AMB);oled.fillRect(0,16,128,102,BK);
        for(int i=0;i<3;i++){
          bool a=(i==spdIdx);int y=22+i*30;
          box(8,y,112,26,spC[i],a);
          oled.setTextSize(2);oled.setTextColor(a?BK:spC[i]);
          oled.setCursor((128-(int)strlen(spN[i])*12)/2,y+9);oled.print(spN[i]);
        }
        ctr(118,"UP/DN  LEFT=ok",DGR);redraw=false;
      }
      break;
  }
  delay(8);
}
