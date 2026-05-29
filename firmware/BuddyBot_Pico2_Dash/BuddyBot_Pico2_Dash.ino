/*
 * ════════════════════════════════════════════════════════════════════
 *  BuddyBot  ·  Pico 2 Dashboard  V6.1  — PORTRAIT 320x480
 *  Board : Raspberry Pi Pico 2 (RP2350)  ·  TFT_eSPI  ·  FT6336U touch
 *  Audio : Keyestudio SC8002B Power Amplifier (GP14 → IN)
 *  Serial: GP0(TX)/GP1(RX)  ←→  Mega Serial1 @ 115200
 * ════════════════════════════════════════════════════════════════════
 *
 *  SC8002B WIRING (3-pin header on module):
 *    Pin "VCC"  →  Pico pin 40 (VBUS = 5V from USB)   ** must be 5V **
 *    Pin "GND"  →  Pico GND (any GND pin)
 *    Pin "IN"   →  100Ω resistor → GP14
 *                  (100nF cap from GP14 side of resistor to GND)
 *
 *  RC LOW-PASS FILTER on GP14 (filters PWM carrier, leaves audio):
 *
 *    GP14 ──[100Ω]──┬── SC8002B IN pin
 *                   │
 *                [100nF]
 *                   │
 *                  GND
 *
 *  VOLUME: Turn potentiometer fully anti-clockwise before first
 *  power-on, then raise slowly to avoid blowing the speaker.
 *
 * ════════════════════════════════════════════════════════════════════
 */

#include <TFT_eSPI.h>
#include <Wire.h>
#include <SPI.h>
#include <math.h>

TFT_eSPI tft = TFT_eSPI();

// ── Display ──────────────────────────────────────────────────────────
#define SCR_W    320
#define SCR_H    480
#define ROTATION   0     // portrait

// ── Audio — SC8002B via tone() on GP14 ───────────────────────────────
#define AUDIO_PIN  14    // GP14 → 100Ω → SC8002B IN  (100nF to GND)
#define SND_QUEUE  12    // max queued notes

// ── Touch (FT6336U via I2C1 — GP26=SDA, GP27=SCL) ──────────────────
#define PIN_CTP_SDA  26
#define PIN_CTP_SCL  27
#define PIN_CTP_INT  28
#define PIN_CTP_RST  15
#define CTP_ADDR     0x38
#define TOUCH_FLIP_X true
#define TOUCH_FLIP_Y false

// ── Screen states (must be defined early because Arduino auto-generates
//     function prototypes at the top of the file, before most user code)
enum Screen : uint8_t {
  SCR_MAIN, 
  SCR_GAMES, 
  SCR_SENSORS,
  SCR_SENS_EYES, 
  SCR_SENS_NOSE, 
  SCR_SENS_BRAIN, 
  SCR_SENS_TUMMY,
  SCR_COMMS, 
  SCR_SETTINGS,
  GAME_MARIO, 
  GAME_PACMAN, 
  GAME_STARSHIP,
  GAME_MEMORY,
  GAME_COLORMATCH, 
  GAME_MATH
};
Screen curScreen = SCR_MAIN, prevScreen = SCR_MAIN;
bool   screenDirty = true;

struct Touch { int16_t x,y; bool pressed; };
unsigned long lastTouchMs = 0;

Touch readTouch() {
  Touch t = {0, 0, false};
  Wire1.beginTransmission(CTP_ADDR);
  Wire1.write(0x02);
  if (Wire1.endTransmission(false) != 0) return t;
  if (Wire1.requestFrom(CTP_ADDR, (uint8_t)6) < 6) return t;
  uint8_t n  = Wire1.read() & 0x0F;
  uint8_t xh = Wire1.read();
  uint8_t xl = Wire1.read();
  uint8_t yh = Wire1.read();
  uint8_t yl = Wire1.read();
  Wire1.read();
  if (n == 0 || n > 2) return t;
  int16_t rx = ((xh & 0x0F) << 8) | xl;
  int16_t ry = ((yh & 0x0F) << 8) | yl;
  t.x = TOUCH_FLIP_X ? constrain(319 - rx, 0, SCR_W-1) : constrain(rx, 0, SCR_W-1);
  t.y = TOUCH_FLIP_Y ? constrain(479 - ry, 0, SCR_H-1) : constrain(ry, 0, SCR_H-1);
  t.pressed = true;
  return t;
}

bool touchReady() { return (millis() - lastTouchMs > 100); }


// ── Neon colour palette (RGB565) ─────────────────────────────────────
#define C_BG     0x0208
#define C_SURF   0x0841
#define C_SURF2  0x10C3
#define C_BORDER 0x2945
#define C_CYAN   0x07FF
#define C_GREEN  0x07E4
#define C_PURPLE 0x781F
#define C_ORANGE 0xFD20
#define C_PINK   0xF81F
#define C_YELLOW 0xFFE0
#define C_RED    0xF800
#define C_BLUE   0x001F
#define C_WHITE  0xFFFF
#define C_LGRAY  0x8C71
#define C_DGRAY  0x4208
#define C_BLACK  0x0000
#define C_MRED   0xF800
#define C_MBLUE  0x001F
#define C_MSKIN  0xFD8C
#define C_MBROWN 0x8200
#define C_MPIPE  0x0320
#define C_MYELL  0xFFE0
#define C_MSKY   0x065F
#define C_MGRND  0xC240

// ── Telemetry ─────────────────────────────────────────────────────────
struct Telem {
  int   gas=0; float temp=0,hum=0,volt=0,amps=0; int pct=0;
  long  dFront=-1,dRear=-1,dLeft=-1,dRight=-1;
  bool  r3ok=false,espok=false,s9ok=false,estop=false,autoM=false;
  bool  irFront=false,irRear=false,pir=false,flame=false;
  char  fw[16]=""; char mode[16]="NORMAL";
} T;
bool brainToggle[6]={true,true,true,true,true,true};
const char* brainLabels[6]={"TEMP","GAS","VOLTAGE","ULTRASONICS","STATUS","MOTOR"};

// ── Serial state ──────────────────────────────────────────────────────
#define MEGA_SERIAL Serial1
char megaBuf[256]; uint16_t megaBufLen=0;
unsigned long lastMegaRx=0;
bool megaLinked=false;

// ── Debug log ────────────────────────────────────────────────────────────────
#define DBG_LINES 10
#define DBG_LEN   48
char   dbgLog[DBG_LINES][DBG_LEN];
int    dbgHead = 0;

void dbgPush(const char* msg) {
  strncpy(dbgLog[dbgHead], msg, DBG_LEN-1);
  dbgLog[dbgHead][DBG_LEN-1] = 0;
  dbgHead = (dbgHead+1) % DBG_LINES;
  Serial.println(msg);  // Pico USB → PC Serial Monitor
}

// ── Audio queue ───────────────────────────────────────────────────────
struct Note { uint16_t freq; uint16_t dur; };
Note          sndQ[SND_QUEUE];
int           sndHead=0, sndTail=0, sndLen=0;
unsigned long sndEndMs=0;

void sndUpdate(){
  if(sndLen==0||millis()<sndEndMs)return;
  // RP2040: use analogWriteFreq + analogWrite for reliable tone output
  if(sndQ[sndHead].freq == 0){
    analogWrite(AUDIO_PIN, 0);
  } else {
    analogWriteFreq(sndQ[sndHead].freq);
    analogWrite(AUDIO_PIN, 127); // 50% duty cycle = loudest square wave
  }
  sndEndMs=millis()+sndQ[sndHead].dur+8;
  sndHead=(sndHead+1)%SND_QUEUE; sndLen--;
}
void sndQ1(uint16_t f,uint16_t d){
  if(sndLen>=SND_QUEUE)return;
  sndQ[sndTail]={f,d}; sndTail=(sndTail+1)%SND_QUEUE; sndLen++;
  if(sndLen==1)sndUpdate();
}
void sndClear(){ sndLen=0; sndHead=sndTail=0; analogWrite(AUDIO_PIN,0); sndEndMs=0; }
void sndClick()   { sndClear(); sndQ1(800,28); }
void sndCoin()    { sndClear(); sndQ1(1047,45); sndQ1(1319,65); }
void sndJump()    { sndClear(); sndQ1(523,22);  sndQ1(784,45); }
void sndStomp()   { sndClear(); sndQ1(350,20);  sndQ1(175,35); }
void sndDot()     { sndQ1(1200,12); }
void sndPower()   { sndClear(); sndQ1(523,30); sndQ1(659,30); sndQ1(784,50); }
void sndHit()     { sndClear(); sndQ1(220,35); sndQ1(110,55); }
void sndBuzz()    { sndClear(); sndQ1(150,90); }
void sndCorrect() { sndClear(); sndQ1(1047,55); sndQ1(1568,90); }
void sndWrong()   { sndClear(); sndQ1(220,35);  sndQ1(165,60); }
void sndMatch()   { sndClear(); sndQ1(1047,60); sndQ1(1319,90); }
void sndDeath()   { sndClear(); sndQ1(392,60);  sndQ1(349,60); sndQ1(294,60); sndQ1(247,120); }
void sndWin()     { sndClear(); sndQ1(523,80);  sndQ1(659,80); sndQ1(784,80); sndQ1(1047,160); }
void sndGameOver(){ sndClear(); sndQ1(392,100); sndQ1(349,100); sndQ1(330,100); sndQ1(262,200); }
void sndAlert()   { sndClear(); sndQ1(880,100); sndQ1(660,100); }
void sndBoot()    { sndQ1(262,80); sndQ1(330,80); sndQ1(392,80); sndQ1(523,130); }

// ── Parsers ───────────────────────────────────────────────────────────
// Only redraw when displayed values actually change — prevents constant flicker
static void markDirty(){ if(curScreen<GAME_MARIO) screenDirty=true; }

void parseStat(const char* s){
  static char b[160]; static char* f[11]; int n=0;
  strncpy(b,s+5,sizeof(b)-1); b[sizeof(b)-1]=0;
  char* p=strtok(b,":"); while(p&&n<11){f[n++]=p;p=strtok(NULL,":");}
  if(n<10)return;
  int ng=atoi(f[0]); float nt=atof(f[1]),nh=atof(f[2]);
  float nv=atof(f[7]); int np=atoi(f[8]); float na=atof(f[9]);
  bool changed=(ng!=T.gas||np!=T.pct||(int)(nv*10)!=(int)(T.volt*10));
  T.gas=ng;T.temp=nt;T.hum=nh;T.volt=nv;T.pct=np;T.amps=na;
  if(changed)markDirty();
}
void parseUS(const char* s){
  static char b[64]; static char* f[4]; int n=0;
  strncpy(b,s+3,sizeof(b)-1); b[sizeof(b)-1]=0;
  char* p=strtok(b,","); while(p&&n<4){f[n++]=p;p=strtok(NULL,",");}
  if(n<4)return;
  long nf=atol(f[0]),nr=atol(f[1]),nl2=atol(f[2]),nrr=atol(f[3]);
  if(nf!=T.dFront||nr!=T.dRear||nl2!=T.dLeft||nrr!=T.dRight){
    T.dFront=nf;T.dRear=nr;T.dLeft=nl2;T.dRight=nrr;
    markDirty();
  }
}
void parseStatus(const char* s){
  const char* tags[]={"R3:","ESP:","S9:","ESTOP:","AUTO:"};
  bool* vals[]={&T.r3ok,&T.espok,&T.s9ok,&T.estop,&T.autoM};
  bool changed=false;
  const char* trueChars[]={'O','O','O','Y','O'};  // R3:Ok, ESP:Ok, S9:Ok, ESTOP:Yes, AUTO:On
  for(int i=0;i<5;i++){const char* pp=strstr(s,tags[i]);if(pp){bool nv=(*(pp+strlen(tags[i]))==trueChars[i]);if(nv!=*(vals[i])){*(vals[i])=nv;changed=true;}}}
  const char* mp=strstr(s,"MODE:");
  if(mp){char nm[16];strncpy(nm,mp+5,15);nm[15]=0;char*nl2=strchr(nm,',');if(nl2)*nl2=0;if(strncmp(nm,T.mode,15)){strncpy(T.mode,nm,16);changed=true;}}
  const char* fp=strstr(s,"FW:");
  if(fp){char nfw[16];strncpy(nfw,fp+3,15);nfw[15]=0;char*nl2=strchr(nfw,',');if(nl2)*nl2=0;if(strncmp(nfw,T.fw,15)){strncpy(T.fw,nfw,16);changed=true;}}
  if(changed)markDirty();
}
void handleMegaLine(const char* line){
  if(!megaLinked){megaLinked=true;markDirty(); dbgPush("[PICO] Mega link established");}
  lastMegaRx=millis();
  // Log non-telemetry messages to debug buffer + Pico USB Serial
  if(strncmp(line,"STAT:",5)!=0 && strncmp(line,"US:",3)!=0){
    else dbgPush(line);  // all other non-telem lines
    if(strncmp(line,"DBG:",4)==0){ dbgPush(line+4); }  // Mega debug forward
  }
  if(strncmp(line,"STAT:",5)==0)parseStat(line);
  else if(strncmp(line,"US:",3)==0)parseUS(line);
  else if(strncmp(line,"STATUS:",7)==0)parseStatus(line);
}
void handleMegaSerial(){
  while(MEGA_SERIAL.available()){
    char c=(char)MEGA_SERIAL.read();
    if(c=='\n'){megaBuf[megaBufLen]=0;if(megaBufLen>0)handleMegaLine(megaBuf);megaBufLen=0;}
    else if(c!='\r'&&megaBufLen<255)megaBuf[megaBufLen++]=c;
  }
}

// ════════════════════════════════════════════════════════════════════
//  AUDIO ENGINE  — non-blocking note queue → SC8002B via GP14
//  How it works:
//    tone(AUDIO_PIN, freq, dur) uses a hardware timer — returns immediately.
//    sndUpdate() called every loop() checks if the note's time has expired
//    and fires the next queued note. Zero blocking in the game loop.
// ════════════════════════════════════════════════════════════════════

// ════════════════════════════════════════════════════════════════════
//  UI PRIMITIVES  — High-End 3D / Glassmorphic Aesthetic
// ════════════════════════════════════════════════════════════════════

// Colour math helpers
uint16_t dimCol(uint16_t c,uint8_t shift=1){
  return (uint16_t)(((c>>11)>>shift)<<11)
        |(uint16_t)((((c>>5)&0x3F)>>shift)<<5)
        |(uint16_t)(((c&0x1F)>>shift));
}
uint16_t blendCol(uint16_t a,uint16_t b,uint8_t t){
  uint8_t r=(((a>>11)*(255-t)+(b>>11)*t)>>8)&0x1F;
  uint8_t g=(((((a>>5)&0x3F)*(255-t))+(((b>>5)&0x3F)*t))>>8)&0x3F;
  uint8_t bl=((((a&0x1F)*(255-t))+((b&0x1F)*t))>>8)&0x1F;
  return (r<<11)|(g<<5)|bl;
}

// Vertical gradient fill (top col1 → bottom col2)
void gradientRect(int x,int y,int w,int h,uint16_t c1,uint16_t c2){
  for(int i=0;i<h;i++){
    tft.drawFastHLine(x,y+i,w,blendCol(c1,c2,(uint8_t)((i*255)/max(h-1,1))));
  }
}

// Glass card: dark gradient bg + coloured top edge + inner highlight
void glassCard(int x,int y,int w,int h,uint16_t accent){
  // Background gradient (dark top to slightly lighter bottom)
  gradientRect(x+1,y+1,w-2,h-2,0x0C62,0x0841);
  // Outer border — accent colour, slightly dim
  tft.drawRoundRect(x,y,w,h,8,dimCol(accent,1));
  // Inner highlight — bright accent top
  tft.drawFastHLine(x+4,y+1,w-8,dimCol(accent,0));
  // Top accent line — full brightness
  tft.drawFastHLine(x+2,y,w-4,accent);
  // Drop shadow (2px right+down, very dark)
  tft.drawFastHLine(x+4,y+h,w-4,0x0208);
  tft.drawFastVLine(x+w,y+4,h-4,0x0208);
}

// Neon box with glow, gradient fill and drop shadow
void neonBox(int x,int y,int w,int h,uint16_t col,uint16_t bg=C_SURF){
  // Drop shadow
  tft.fillRoundRect(x+3,y+3,w,h,6,0x0208);
  // Gradient fill
  gradientRect(x+1,y+1,w-2,h-2,blendCol(bg,col,40),bg);
  // Outer glow — dim
  tft.drawRoundRect(x+1,y+1,w-2,h-2,5,dimCol(col,2));
  // Main border
  tft.drawRoundRect(x,y,w,h,6,col);
  // Top gloss line
  tft.drawFastHLine(x+4,y+1,w-8,dimCol(col,1));
}

// Glow text: 3-layer (shadow + dim + bright)
void glowText(int x,int y,const char* txt,uint16_t col,uint8_t sz=1){
  uint16_t d1=dimCol(col,2), d2=dimCol(col,1);
  tft.setTextSize(sz); tft.setTextDatum(TL_DATUM);
  tft.setTextColor(d1,C_BG); tft.setCursor(x+2,y+2); tft.print(txt);
  tft.setTextColor(d2,C_BG); tft.setCursor(x+1,y+1); tft.print(txt);
  tft.setTextColor(col,C_BG); tft.setCursor(x,y);     tft.print(txt);
}

// Centred text
void centreText(int cx,int y,const char* txt,uint16_t col,uint8_t sz,uint16_t bg=C_BG){
  tft.setTextSize(sz); tft.setTextColor(col,bg);
  int16_t tw=strlen(txt)*6*sz;
  tft.setCursor(cx-tw/2,y); tft.print(txt);
}

// High-end main menu button with 3D bevel + gradient + icon
void neonBtn(int x,int y,int w,int h,const char* icon,const char* label,uint16_t col,bool pressed=false){
  uint16_t bg   = pressed ? dimCol(col,1) : 0x0A41;
  uint16_t tc   = pressed ? C_BLACK : col;
  // Drop shadow
  tft.fillRoundRect(x+4,y+4,w,h,10,0x0104);
  // Gradient body
  gradientRect(x,y,w,h,blendCol(bg,col,30),bg);
  // Rounded clip (re-blank corners)
  tft.drawRoundRect(x,y,w,h,10,bg);
  // Outer border — bright accent
  tft.drawRoundRect(x,y,w,h,10,col);
  // Inner bevel highlight (top-left)
  tft.drawFastHLine(x+4,y+1,w-8,dimCol(col,1));
  tft.drawFastVLine(x+1,y+4,h/3,dimCol(col,2));
  // Bottom-right shadow bevel
  tft.drawFastHLine(x+4,y+h-1,w-8,dimCol(col,3));
  // Icon — large centre top
  tft.setTextSize(3); tft.setTextColor(tc,0x0A41);
  int16_t iw=strlen(icon)*18;
  tft.setCursor(x+(w-iw)/2, y+h/2-26); tft.print(icon);
  // Label — centred bottom
  tft.setTextSize(2); tft.setTextColor(col,0x0A41);
  int16_t lw=strlen(label)*12;
  tft.setCursor(x+(w-lw)/2, y+h/2+8); tft.print(label);
}

// Stat pill with gradient and side accent bar
void statPill(int x,int y,int w,const char* lbl,const char* val,uint16_t col){
  // Gradient bg
  gradientRect(x,y,w,26,blendCol(C_SURF,col,20),C_SURF);
  // Left accent bar
  tft.fillRect(x,y,3,26,col);
  // Border
  tft.drawRect(x,y,w,26,dimCol(col,1));
  // Top gloss
  tft.drawFastHLine(x+3,y+1,w-4,dimCol(col,2));
  tft.setTextSize(1); tft.setTextColor(C_LGRAY,C_SURF);
  tft.setCursor(x+8,y+5); tft.print(lbl);
  if(val&&strlen(val)>0){
    tft.setTextColor(col,C_SURF);
    tft.setCursor(x+w-strlen(val)*6-4,y+9); tft.print(val);
  }
}

// US distance bar — 3D segmented style
void usBar(int x,int y,int w,long dist,const char* lbl){
  gradientRect(x,y,w,34,0x0C62,C_SURF);
  tft.drawRect(x,y,w,34,C_BORDER);
  tft.drawFastHLine(x+1,y+1,w-2,0x18C6);  // inner gloss
  tft.setTextSize(1); tft.setTextColor(C_LGRAY,C_SURF);
  tft.setCursor(x+4,y+4); tft.print(lbl);
  if(dist<0){ tft.setTextColor(C_DGRAY,C_SURF); tft.setCursor(x+4,y+20); tft.print("NO SIGNAL"); return; }
  long capped=min(dist,200L);
  int bw=(int)((capped*(w-8))/200);
  uint16_t col=dist<20?C_RED:dist<50?C_ORANGE:dist<100?C_YELLOW:C_GREEN;
  // Segmented bar
  for(int i=0;i<bw;i+=5){
    tft.fillRect(x+4+i,y+18,4,10,blendCol(C_RED,C_GREEN,(uint8_t)((i*255)/max(bw,1))));
  }
  tft.drawRect(x+4,y+18,w-8,10,dimCol(col,1));
  char buf[12]; snprintf(buf,12,"%ldcm",dist);
  tft.setTextColor(C_WHITE,C_SURF); tft.setCursor(x+w-42,y+20); tft.print(buf);
}

// Back button — right-aligned chevron, tap anywhere in top 72px
void drawBack(uint16_t col=C_CYAN){
  // Glass pill button top-right corner
  gradientRect(SCR_W-68,4,64,28,blendCol(C_SURF,col,30),C_SURF);
  tft.drawRoundRect(SCR_W-68,4,64,28,6,col);
  tft.drawFastHLine(SCR_W-64,5,56,dimCol(col,1));
  tft.setTextSize(2); tft.setTextColor(col,C_SURF);
  tft.setCursor(SCR_W-60,9); tft.print("< BCK");
}

// Horizontal rule — double line with glow
void hRule(int y,uint16_t col=C_CYAN){
  tft.drawFastHLine(0,y+2,SCR_W,dimCol(col,3));
  tft.drawFastHLine(0,y+1,SCR_W,dimCol(col,1));
  tft.drawFastHLine(0,y,  SCR_W,col);
}
// ════════════════════════════════════════════════════════════════════
//  HEADER  (shared across all non-game screens)
// ════════════════════════════════════════════════════════════════════
void drawHeader(){
  // Premium gradient header bar
  gradientRect(0,0,SCR_W,56,0x1082,0x0208);
  // Cyan top edge glow
  tft.drawFastHLine(0,0,SCR_W,C_CYAN);
  tft.drawFastHLine(0,1,SCR_W,dimCol(C_CYAN,1));
  // Bottom separator
  tft.drawFastHLine(0,55,SCR_W,dimCol(C_CYAN,2));
  tft.drawFastHLine(0,56,SCR_W,dimCol(C_CYAN,3));

  // Title with 3-layer glow
  tft.setTextSize(2);
  uint16_t s1=dimCol(C_CYAN,3),s2=dimCol(C_CYAN,2),s3=dimCol(C_CYAN,1);
  int16_t tw=13*12; int tx=SCR_W/2-tw/2;
  tft.setTextColor(s1,0x0000); tft.setCursor(tx+2,8+2); tft.print("AJ2BUDDYCOMMS");
  tft.setTextColor(s2,0x0000); tft.setCursor(tx+1,8+1); tft.print("AJ2BUDDYCOMMS");
  tft.setTextColor(C_CYAN,0x0000); tft.setCursor(tx,8);  tft.print("AJ2BUDDYCOMMS");

  // Status row
  // Link indicator — pulsing dot
  uint16_t lk=megaLinked?C_GREEN:C_RED;
  tft.fillCircle(10,44,5,dimCol(lk,2));
  tft.fillCircle(10,44,3,lk);
  tft.setTextSize(1); tft.setTextColor(megaLinked?C_GREEN:C_RED,0x0000);
  tft.setCursor(18,40); tft.print(megaLinked?"LIVE":"WAIT");

  // Battery bar
  char buf[10];
  uint16_t bc=T.pct>50?C_GREEN:T.pct>20?C_ORANGE:C_RED;
  snprintf(buf,10,"%d%%",T.pct);
  // Mini battery bar (30px wide)
  tft.drawRect(54,38,32,12,C_LGRAY);
  tft.fillRect(55,39,(int)(T.pct*30/100),10,bc);
  tft.fillRect(86,41,3,6,C_LGRAY);  // terminal nub
  tft.setTextColor(bc,0x0000); tft.setCursor(92,40); tft.print(buf);

  // Voltage
  snprintf(buf,10,"%.1fV",T.volt);
  tft.setTextColor(C_CYAN,0x0000); tft.setCursor(128,40); tft.print(buf);

  // Mode badge — right side
  uint16_t mc=C_PURPLE;
  int16_t mw=strlen(T.mode)*6+8;
  gradientRect(SCR_W-mw-2,34,mw,18,blendCol(C_SURF,mc,40),C_SURF);
  tft.drawRect(SCR_W-mw-2,34,mw,18,mc);
  tft.setTextColor(mc,0x0000); tft.setCursor(SCR_W-mw+2,39); tft.print(T.mode);
}

// ════════════════════════════════════════════════════════════════════
//  MAIN SCREEN  — 2x2 hub buttons
// ════════════════════════════════════════════════════════════════════
void drawMain() {
  tft.fillScreen(C_BG);
  drawHeader();

  // 2x2 buttons — each 148x148 with 8px padding
  //  (8) [GAMES 148x148] (8) [SENSORS 148x148] (8)
  //       y=60                                       y = 60+148+8 = 216
  const int BW=148, BH=148, PAD=8;
  const int ROW1=60, ROW2=ROW1+BH+PAD;

  neonBtn(PAD,         ROW1, BW, BH, ">",  "GAMES",   C_GREEN);
  neonBtn(PAD+BW+PAD,  ROW1, BW, BH, "o",  "SENSORS", C_CYAN);
  neonBtn(PAD,         ROW2, BW, BH, "~",  "COMMS",   C_PURPLE);
  neonBtn(PAD+BW+PAD,  ROW2, BW, BH, "*",  "SETTINGS",C_ORANGE);

  // Bottom sensor strip
  int sy=ROW2+BH+PAD;
  tft.fillRect(0,sy,SCR_W,SCR_H-sy,C_SURF);
  hRule(sy,C_CYAN);

  char buf[16];
  snprintf(buf,16,"F:%ldcm",T.dFront<0?0:T.dFront);
  tft.setTextSize(1); tft.setTextColor(T.dFront<30?C_RED:C_CYAN,C_SURF);
  tft.setCursor(6,sy+8); tft.print(buf);

  snprintf(buf,16,"R:%ldcm",T.dRear<0?0:T.dRear);
  tft.setTextColor(T.dRear<30?C_RED:C_CYAN,C_SURF);
  tft.setCursor(86,sy+8); tft.print(buf);

  snprintf(buf,16,"L:%ldcm",T.dLeft<0?0:T.dLeft);
  tft.setTextColor(T.dLeft<30?C_RED:C_CYAN,C_SURF);
  tft.setCursor(166,sy+8); tft.print(buf);

  snprintf(buf,16,"Bk:%ldcm",T.dRear<0?0:T.dRear);
  tft.setTextColor(C_LGRAY,C_SURF);
  tft.setCursor(246,sy+8); tft.print(buf);
}

void handleMainTouch(Touch& t) {
  const int BW=148,BH=148,PAD=8,ROW1=60,ROW2=ROW1+BH+PAD;
  sndClick(); if(t.x>=PAD && t.x<PAD+BW) {
    if(t.y>=ROW1 && t.y<ROW1+BH) { curScreen=SCR_SENSORS; screenDirty=true; }
    if(t.y>=ROW2 && t.y<ROW2+BH) { curScreen=SCR_SETTINGS;screenDirty=true; }
  }
  if(t.x>=PAD+BW+PAD && t.x<PAD+BW+PAD+BW) {
    if(t.y>=ROW1 && t.y<ROW1+BH) { curScreen=SCR_GAMES;   screenDirty=true; }
    if(t.y>=ROW2 && t.y<ROW2+BH) { curScreen=SCR_COMMS;   screenDirty=true; }
  }
}

// ════════════════════════════════════════════════════════════════════
//  GAMES MENU
// ════════════════════════════════════════════════════════════════════
void drawGames() {
  tft.fillScreen(C_BG);
  drawHeader();
  drawBack(C_GREEN);
  glowText(SCR_W/2-48,55,"GAMES MENU",C_GREEN,2);
  hRule(72,C_GREEN);

  const char* names[]={"SUPER MARIO","PACMAN","STARSHIP","MEMORY","COLOR MATCH","MATH"};
  uint16_t    cols[] ={C_RED,C_YELLOW,C_CYAN,C_PURPLE,C_ORANGE,C_GREEN};
  for(int i=0;i<6;i++){
    int bx=8, by=80+i*62;
    neonBox(bx,by,SCR_W-16,54,cols[i],C_SURF);
    tft.setTextSize(2); tft.setTextColor(cols[i],C_SURF);
    int16_t tw=strlen(names[i])*12;
    tft.setCursor(bx+(SCR_W-16-tw)/2, by+16); tft.print(names[i]);
  }
}

void handleGamesTouch(Touch& t) {
  if(t.y<72){ curScreen=SCR_MAIN; screenDirty=true; return; }
  Screen games[]={GAME_MARIO,GAME_PACMAN,GAME_STARSHIP,GAME_MEMORY,GAME_COLORMATCH,GAME_MATH};
  for(int i=0;i<6;i++){
    int by=80+i*62;
    if(t.y>=by && t.y<by+54 && t.x>=8 && t.x<SCR_W-8){
      sndClick(); prevScreen=SCR_GAMES; curScreen=games[i]; screenDirty=true; return;
    }
  }
}

// ════════════════════════════════════════════════════════════════════
//  SENSORS MENU  (4 submenus)
// ════════════════════════════════════════════════════════════════════
void drawSensors() {
  tft.fillScreen(C_BG);
  drawHeader();
  drawBack(C_CYAN);
  glowText(SCR_W/2-56,55,"SENSOR MENU",C_CYAN,2);
  hRule(72,C_CYAN);

  const char* labels[]={"EYES","NOSE","BRAIN","TUMMY"};
  const char* subs[]  ={"US+IR+Camera","Gas Sensors","All Telemetry","Power Data"};
  uint16_t    cols[]  ={C_CYAN,C_GREEN,C_PURPLE,C_ORANGE};

  for(int i=0;i<4;i++){
    int by=80+i*96;
    neonBox(8,by,SCR_W-16,86,cols[i],C_SURF);
    tft.setTextSize(3); tft.setTextColor(cols[i],C_SURF);
    int16_t tw=strlen(labels[i])*18;
    tft.setCursor(8+(SCR_W-16-tw)/2,by+10); tft.print(labels[i]);
    tft.setTextSize(1); tft.setTextColor(C_LGRAY,C_SURF);
    tw=strlen(subs[i])*6;
    tft.setCursor(8+(SCR_W-16-tw)/2,by+50); tft.print(subs[i]);
  }
}

void handleSensorsTouch(Touch& t) {
  if(t.y<72){ curScreen=SCR_MAIN; screenDirty=true; return; }
  Screen subs[]={SCR_SENS_EYES,SCR_SENS_NOSE,SCR_SENS_BRAIN,SCR_SENS_TUMMY};
  for(int i=0;i<4;i++){
    int by=80+i*96;
    if(t.y>=by && t.y<by+86 && t.x>=8 && t.x<SCR_W-8){
      sndClick(); curScreen=subs[i]; screenDirty=true; return;
    }
  }
}

// ════════════════════════════════════════════════════════════════════
//  SENSOR — EYES  (US distances + IR sensors visual layout)
// ════════════════════════════════════════════════════════════════════
void drawSensEyes() {
  tft.fillScreen(C_BG);
  drawHeader();
  drawBack(C_CYAN);
  glowText(SCR_W/2-24,55,"EYES",C_CYAN,2);
  hRule(72,C_CYAN);

  // Robot outline in center
  int rx=SCR_W/2, ry=230;
  tft.drawRoundRect(rx-30,ry-40,60,80,8,C_LGRAY);  // body
  tft.drawRect(rx-20,ry-60,40,22,C_LGRAY);           // head
  // eyes
  tft.fillCircle(rx-8,ry-50,4,C_CYAN);
  tft.fillCircle(rx+8,ry-50,4,C_CYAN);

  // US sensor readouts — radial around robot
  char buf[16];
  uint16_t fc=T.dFront<0?C_DGRAY:T.dFront<30?C_RED:T.dFront<80?C_ORANGE:C_GREEN;
  uint16_t rc=T.dRear <0?C_DGRAY:T.dRear <30?C_RED:T.dRear <80?C_ORANGE:C_GREEN;
  uint16_t lc=T.dLeft <0?C_DGRAY:T.dLeft <30?C_RED:T.dLeft <80?C_ORANGE:C_GREEN;
  uint16_t rrc=T.dRight<0?C_DGRAY:T.dRight<30?C_RED:T.dRight<80?C_ORANGE:C_GREEN;

  // FRONT (top)
  snprintf(buf,16,T.dFront<0?"--":"%ldcm",T.dFront);
  neonBox(rx-35,80,70,36,fc,C_SURF);
  tft.setTextSize(1); tft.setTextColor(C_LGRAY,C_SURF); tft.setCursor(rx-32,83); tft.print("FRONT");
  tft.setTextSize(2); tft.setTextColor(fc,C_SURF);
  tft.setCursor(rx-strlen(buf)*6,95); tft.print(buf);
  tft.drawFastVLine(rx,117,50,fc);

  // REAR (bottom)
  snprintf(buf,16,T.dRear<0?"--":"%ldcm",T.dRear);
  neonBox(rx-35,334,70,36,rc,C_SURF);
  tft.setTextSize(1); tft.setTextColor(C_LGRAY,C_SURF); tft.setCursor(rx-30,337); tft.print("REAR");
  tft.setTextSize(2); tft.setTextColor(rc,C_SURF);
  tft.setCursor(rx-strlen(buf)*6,349); tft.print(buf);
  tft.drawFastVLine(rx,310,24,rc);

  // LEFT
  snprintf(buf,16,T.dLeft<0?"--":"%ldcm",T.dLeft);
  neonBox(8,ry-18,74,36,lc,C_SURF);
  tft.setTextSize(1); tft.setTextColor(C_LGRAY,C_SURF); tft.setCursor(12,ry-15); tft.print("LEFT");
  tft.setTextSize(2); tft.setTextColor(lc,C_SURF); tft.setCursor(12,ry-3); tft.print(buf);
  tft.drawFastHLine(82,ry,rx-112,lc);

  // RIGHT
  snprintf(buf,16,T.dRight<0?"--":"%ldcm",T.dRight);
  neonBox(SCR_W-82,ry-18,74,36,rrc,C_SURF);
  tft.setTextSize(1); tft.setTextColor(C_LGRAY,C_SURF); tft.setCursor(SCR_W-78,ry-15); tft.print("RIGHT");
  tft.setTextSize(2); tft.setTextColor(rrc,C_SURF); tft.setCursor(SCR_W-78,ry-3); tft.print(buf);
  tft.drawFastHLine(rx+31,ry,SCR_W-82-rx-31,rrc);

  // IR sensors
  hRule(380,C_CYAN);
  tft.setTextSize(1); tft.setTextColor(C_LGRAY,C_SURF); tft.setCursor(8,388); tft.print("IR SENSORS (FRONT / REAR)");
  uint16_t ilCol=T.irFront?C_RED:C_DGRAY;
  uint16_t irCol=T.irRear?C_RED:C_DGRAY;
  tft.fillRoundRect(8,400,148,36,6,C_SURF);
  tft.drawRoundRect(8,400,148,36,6,ilCol);
  tft.setTextColor(ilCol,C_SURF); tft.setTextSize(2); tft.setCursor(16,412);
  tft.print(T.irFront?"IR-F DETECT":"IR-F clear ");
  tft.fillRoundRect(164,400,148,36,6,C_SURF);
  tft.drawRoundRect(164,400,148,36,6,irCol);
  tft.setTextColor(irCol,C_SURF); tft.setCursor(172,412);
  tft.print(T.irRear?"IR-Rear DETECT":"IR-Rear clear ");

  // Camera placeholder
  hRule(440,C_PURPLE);
  tft.setTextSize(1); tft.setTextColor(C_PURPLE,C_SURF); tft.setCursor(8,448); tft.print("CAMERA: S9 USB feed via Android app");
}

// ════════════════════════════════════════════════════════════════════
//  SENSOR — NOSE  (gas)
// ════════════════════════════════════════════════════════════════════
void drawSensNose() {
  tft.fillScreen(C_BG);
  drawHeader();
  drawBack(C_GREEN);
  glowText(SCR_W/2-24,55,"NOSE",C_GREEN,2);
  hRule(72,C_GREEN);

  int gasVal=T.gas;
  uint16_t gc= gasVal>700?C_RED:gasVal>400?C_ORANGE:gasVal>200?C_YELLOW:C_GREEN;

  // Big gas reading
  tft.setTextSize(1); tft.setTextColor(C_LGRAY,C_BG); tft.setCursor(SCR_W/2-30,90); tft.print("GAS LEVEL");
  char buf[12]; snprintf(buf,12,"%d",gasVal);
  tft.setTextSize(6); tft.setTextColor(gc,C_BG);
  int16_t tw=strlen(buf)*36;
  tft.setCursor(SCR_W/2-tw/2,110); tft.print(buf);
  tft.setTextSize(2); tft.setTextColor(C_LGRAY,C_BG); tft.setCursor(SCR_W/2-12,172); tft.print("ppm");

  // Status label
  const char* status = gasVal>700?"DANGER!":gasVal>400?"WARNING":gasVal>200?"ELEVATED":"CLEAR";
  neonBox(SCR_W/2-60,200,120,40,gc,C_SURF);
  tft.setTextSize(2); tft.setTextColor(gc,C_SURF);
  int16_t sl=strlen(status)*12;
  tft.setCursor(SCR_W/2-sl/2,212); tft.print(status);

  // Horizontal gauge bar
  hRule(258,gc);
  int bw=SCR_W-20; int filled=(int)((min(gasVal,1023)*bw)/1023);
  tft.fillRect(10,265,bw,24,C_SURF);
  tft.fillRect(10,265,filled,24,gc);
  tft.drawRect(10,265,bw,24,C_BORDER);

  // Thresholds
  tft.setTextSize(1);
  tft.setTextColor(C_GREEN,C_BG);  tft.setCursor(10,296);  tft.print("SAFE<200");
  tft.setTextColor(C_YELLOW,C_BG); tft.setCursor(90,296);  tft.print("ELEV<400");
  tft.setTextColor(C_ORANGE,C_BG); tft.setCursor(178,296); tft.print("WARN<700");
  tft.setTextColor(C_RED,C_BG);    tft.setCursor(258,296); tft.print("DANGER");

  // Flame + PIR
  hRule(310,C_ORANGE);
  tft.setTextSize(1); tft.setTextColor(C_LGRAY,C_BG); tft.setCursor(8,318); tft.print("ENVIRONMENT");
  uint16_t flCol=T.flame?C_RED:C_DGRAY;
  tft.fillRoundRect(8,328,148,44,6,C_SURF);
  tft.drawRoundRect(8,328,148,44,6,flCol);
  tft.setTextSize(2); tft.setTextColor(flCol,C_SURF);
  tft.setCursor(14,338); tft.print(T.flame?"FLAME!":"No Flame");
  uint16_t pirCol=T.pir?C_CYAN:C_DGRAY;
  tft.fillRoundRect(164,328,148,44,6,C_SURF);
  tft.drawRoundRect(164,328,148,44,6,pirCol);
  tft.setTextColor(pirCol,C_SURF);
  tft.setCursor(170,338); tft.print(T.pir?"MOTION!":"No Motion");
}

// ════════════════════════════════════════════════════════════════════
// ════════════════════════════════════════════════════════════════════
//  DEBUG LOG VIEW  (toggled from BRAIN screen header tap)
// ════════════════════════════════════════════════════════════════════
bool showBrainLog = false;

void drawBrainLog() {
  tft.fillScreen(C_BG);
  drawBack(C_PURPLE);
  centreText(SCR_W/2, 8, "MEGA EVENT LOG", C_PURPLE, 2, C_BG);
  hRule(34, C_PURPLE);
  tft.setTextSize(1);
  tft.setTextColor(C_DGRAY, C_BG);
  tft.setCursor(4, SCR_H-12); tft.print("tap header to toggle telemetry view");
  for (int i = 0; i < DBG_LINES; i++) {
    int idx = (dbgHead + i) % DBG_LINES;
    if (dbgLog[idx][0] == 0) continue;
    int y = 40 + i * 43;
    gradientRect(2, y, SCR_W-4, 40, blendCol(C_SURF,C_PURPLE,12), C_SURF);
    tft.drawRect(2, y, SCR_W-4, 40, dimCol(C_PURPLE, 2));
    // First 24 chars
    char buf[32]; strncpy(buf, dbgLog[idx], 26); buf[26]=0;
    tft.setTextColor(C_CYAN, C_SURF); tft.setCursor(4, y+4); tft.print(buf);
    // Second 24 chars
    if(strlen(dbgLog[idx])>26){
      strncpy(buf, dbgLog[idx]+26, 22); buf[22]=0;
      tft.setTextColor(C_LGRAY, C_SURF); tft.setCursor(4, y+20); tft.print(buf);
    }
  }
}

//  SENSOR — BRAIN  (all telemetry with toggles)
// ════════════════════════════════════════════════════════════════════
void drawSensBrain() {
  tft.fillScreen(C_BG);
  drawHeader();
  drawBack(C_PURPLE);
  glowText(SCR_W/2-30,55,"BRAIN",C_PURPLE,2);
  hRule(72,C_PURPLE);

  char buf[32]; int y=80;
  // TEMP
  if(brainToggle[0]) {
    snprintf(buf,32,"TEMP   %.1f C  HUM %.0f%%",T.temp,T.hum);
    uint16_t c=T.temp>38?C_RED:T.temp>32?C_ORANGE:C_CYAN;
    statPill(8,y,SCR_W-16,buf,"",c); y+=28;
  }
  // GAS
  if(brainToggle[1]) {
    snprintf(buf,32,"GAS    %d ppm",T.gas);
    uint16_t c=T.gas>700?C_RED:T.gas>400?C_ORANGE:C_GREEN;
    statPill(8,y,SCR_W-16,buf,"",c); y+=28;
  }
  // VOLTAGE
  if(brainToggle[2]) {
    snprintf(buf,32,"VOLT   %.2fV   %.2fA   %d%%",T.volt,T.amps,T.pct);
    uint16_t c=T.volt>7.5?C_GREEN:T.volt>7.0?C_ORANGE:C_RED;
    statPill(8,y,SCR_W-16,buf,"",c); y+=28;
  }
  // ULTRASONICS
  if(brainToggle[3]) {
    snprintf(buf,32,"US  F%ld R%ld L%ld Rg%ld",
      T.dFront<0?-1:T.dFront, T.dRear<0?-1:T.dRear,
      T.dLeft<0?-1:T.dLeft,   T.dRight<0?-1:T.dRight);
    statPill(8,y,SCR_W-16,buf,"",C_CYAN); y+=28;
  }
  // STATUS
  if(brainToggle[4]) {
    uint16_t sc=T.estop?C_RED:C_GREEN;
    snprintf(buf,32,"R3:%s ESP:%s S9:%s AUTO:%s",
      T.r3ok?"Y":"N", T.espok?"Y":"N", T.s9ok?"Y":"N", T.autoM?"ON":"OFF");
    statPill(8,y,SCR_W-16,buf,"",sc); y+=28;
  }
  // MODE
  if(brainToggle[5]) {
    snprintf(buf,32,"MODE   %s   FW:%s",T.mode,T.fw);
    statPill(8,y,SCR_W-16,buf,"",C_PURPLE); y+=28;
  }

  // Toggle buttons
  hRule(y+4,C_PURPLE); y+=12;
  tft.setTextSize(1); tft.setTextColor(C_LGRAY,C_BG);
  tft.setCursor(8,y); tft.print("FEED TOGGLES:"); y+=12;
  for(int i=0;i<6;i++) {
    int bx=8+(i%3)*(SCR_W/3), by=y+(i/3)*28;
    uint16_t tc=brainToggle[i]?C_PURPLE:C_DGRAY;
    tft.fillRoundRect(bx,by,(SCR_W/3)-4,22,4,C_SURF);
    tft.drawRoundRect(bx,by,(SCR_W/3)-4,22,4,tc);
    tft.setTextColor(tc,C_SURF); tft.setTextSize(1);
    tft.setCursor(bx+3,by+7); tft.print(brainLabels[i]);
  }
}

void handleBrainTouch(Touch& t) {
void handleBrainTouch(Touch& t) {
  if(t.y<72 && t.x<(SCR_W-80)){ curScreen=SCR_SENSORS; screenDirty=true; return; } // left = back
  if(t.y<72){ showBrainLog=!showBrainLog; screenDirty=true; return; }  // right = toggle log
  int firstToggleY=0;
  int y=80; int used=0;
  for(int i=0;i<6;i++) if(brainToggle[i]) used++;
  firstToggleY = 80 + used*28 + 24;
  for(int i=0;i<6;i++){
    int bx=8+(i%3)*(SCR_W/3), by=firstToggleY+(i/3)*28;
    if(t.x>=bx && t.x<bx+(SCR_W/3)-4 && t.y>=by && t.y<by+22){
      brainToggle[i]=!brainToggle[i]; screenDirty=true; return;
    }
  }
}

// ════════════════════════════════════════════════════════════════════
//  SENSOR — TUMMY  (power data)
// ════════════════════════════════════════════════════════════════════
void drawSensTummy() {
  tft.fillScreen(C_BG);
  drawHeader();
  drawBack(C_ORANGE);
  glowText(SCR_W/2-30,55,"TUMMY",C_ORANGE,2);
  hRule(72,C_ORANGE);

  char buf[20];
  uint16_t vc=T.volt>7.5?C_GREEN:T.volt>7.0?C_ORANGE:C_RED;
  uint16_t ac=T.amps>3.0?C_RED:T.amps>2.0?C_ORANGE:C_GREEN;
  uint16_t pc=T.pct>50?C_GREEN:T.pct>20?C_ORANGE:C_RED;

  // Big voltage
  snprintf(buf,20,"%.2f V",T.volt);
  tft.setTextSize(1); tft.setTextColor(C_LGRAY,C_BG); tft.setCursor(SCR_W/2-24,88); tft.print("VOLTAGE");
  tft.setTextSize(5); tft.setTextColor(vc,C_BG);
  int16_t tw=strlen(buf)*30; tft.setCursor(SCR_W/2-tw/2,102); tft.print(buf);

  // Big current
  snprintf(buf,20,"%.2f A",T.amps);
  tft.setTextSize(1); tft.setTextColor(C_LGRAY,C_BG); tft.setCursor(SCR_W/2-22,174); tft.print("CURRENT");
  tft.setTextSize(5); tft.setTextColor(ac,C_BG);
  tw=strlen(buf)*30; tft.setCursor(SCR_W/2-tw/2,188); tft.print(buf);

  // Battery % with big bar
  snprintf(buf,20,"%d%%",T.pct);
  tft.setTextSize(1); tft.setTextColor(C_LGRAY,C_BG); tft.setCursor(SCR_W/2-24,260); tft.print("BATTERY");
  tft.setTextSize(5); tft.setTextColor(pc,C_BG);
  tw=strlen(buf)*30; tft.setCursor(SCR_W/2-tw/2,274); tft.print(buf);

  // Percentage bar
  int bw=SCR_W-40;
  tft.fillRect(20,330,bw,28,C_SURF);
  int filled=(int)(T.pct*bw/100);
  for(int x=0;x<filled;x+=4) {
    uint16_t col=x<filled/3?C_RED:x<2*filled/3?C_ORANGE:C_GREEN;
    tft.fillRect(20+x,330,3,28,col);
  }
  tft.drawRect(20,330,bw,28,C_BORDER);

  // Power = V*A
  float watts=T.volt*T.amps;
  snprintf(buf,20,"%.1f W",watts);
  hRule(374,C_ORANGE);
  tft.setTextSize(1); tft.setTextColor(C_LGRAY,C_BG); tft.setCursor(8,382); tft.print("POWER DRAW:");
  tft.setTextSize(3); tft.setTextColor(C_ORANGE,C_BG);
  tw=strlen(buf)*18; tft.setCursor(SCR_W-tw-8,378); tft.print(buf);

  // Est runtime
  float mAh=2000.0f; // assume 2Ah pack
  float runtimeH = (T.amps>0.1) ? (mAh/1000.0f)*(T.pct/100.0f)/T.amps : 0;
  int rMin=(int)(runtimeH*60);
  snprintf(buf,20,"%dm left",rMin);
  tft.setTextSize(1); tft.setTextColor(C_LGRAY,C_BG); tft.setCursor(8,420); tft.print("EST RUNTIME:");
  tft.setTextSize(2); tft.setTextColor(pc,C_BG);
  tw=strlen(buf)*12; tft.setCursor(SCR_W-tw-8,416); tft.print(buf);
}

// ════════════════════════════════════════════════════════════════════
//  COMMS SCREEN
// ════════════════════════════════════════════════════════════════════
void drawComms() {
  tft.fillScreen(C_BG);
  drawHeader();
  drawBack(C_PURPLE);
  glowText(SCR_W/2-30,55,"COMMS",C_PURPLE,2);
  hRule(72,C_PURPLE);

  char buf[40]; int y=84;
  const char* labels[]={"MEGA LINK","R3 MOTOR","ESP32 WIFI","S9 ANDROID"};
  bool* states[]={&megaLinked,&T.r3ok,&T.espok,&T.s9ok};
  uint16_t cols[]={C_CYAN,C_GREEN,C_ORANGE,C_PURPLE};

  for(int i=0;i<4;i++){
    bool ok=*(states[i]);
    uint16_t c=ok?cols[i]:C_DGRAY;
    neonBox(8,y,SCR_W-16,76,c,C_SURF);
    tft.fillCircle(24,y+22,8,ok?c:C_DGRAY);
    tft.setTextSize(2); tft.setTextColor(c,C_SURF);
    tft.setCursor(40,y+14); tft.print(labels[i]);
    tft.setTextSize(1); tft.setTextColor(ok?C_WHITE:C_DGRAY,C_SURF);
    tft.setCursor(40,y+38); tft.print(ok?"CONNECTED - ONLINE":"NOT DETECTED");
    y+=84;
  }

  // Mega RX timer
  hRule(y+4,C_PURPLE);
  unsigned long since=(millis()-lastMegaRx)/1000;
  snprintf(buf,40,"Last Mega RX: %lus ago",since);
  tft.setTextSize(1); tft.setTextColor(since>10?C_RED:C_GREEN,C_BG);
  tft.setCursor(8,y+12); tft.print(buf);
}

// ════════════════════════════════════════════════════════════════════
//  SETTINGS SCREEN
// ════════════════════════════════════════════════════════════════════
void drawSettings() {
  tft.fillScreen(C_BG);
  drawHeader();
  drawBack(C_ORANGE);
  glowText(SCR_W/2-36,55,"SETTINGS",C_ORANGE,2);
  hRule(72,C_ORANGE);

  tft.setTextSize(1); tft.setTextColor(C_LGRAY,C_BG);
  tft.setCursor(8,84); tft.print("BuddyBot Pico2 Dash v6.0");
  tft.setCursor(8,98); tft.print("Mega UART: GP0/GP1 @ 115200");
  tft.setCursor(8,112); tft.print("Touch: FT6336U I2C @ 400kHz");
  tft.setCursor(8,126); tft.print("Display: ST7796S SPI 320x480");

  // Send test commands to Mega
  const char* cmds[]={"PING MEGA","REQ STATUS","REQ SENSOR","ESTOP"};
  uint16_t cc[]={C_CYAN,C_GREEN,C_ORANGE,C_RED};
  for(int i=0;i<4;i++){
    neonBox(8,150+i*72,SCR_W-16,60,cc[i],C_SURF);
    tft.setTextSize(2); tft.setTextColor(cc[i],C_SURF);
    int16_t tw=strlen(cmds[i])*12;
    tft.setCursor(8+(SCR_W-16-tw)/2,168+i*72); tft.print(cmds[i]);
  }
}

void handleSettingsTouch(Touch& t) {
  if(t.y<72){ curScreen=SCR_MAIN; screenDirty=true; return; }
  const char* megaCmds[]={"PING","STATUS","SENSOR_STATUS","ESTOP"};
  for(int i=0;i<4;i++){
    if(t.y>=150+i*72 && t.y<210+i*72 && t.x>=8 && t.x<SCR_W-8){
      MEGA_SERIAL.println(megaCmds[i]); sndAlert();
      // Flash feedback
      neonBox(8,150+i*72,SCR_W-16,60,C_WHITE,C_WHITE);
      delay(80);
      screenDirty=true;
    }
  }
}

// ════════════════════════════════════════════════════════════════════
//  GAME: SUPER MARIO BROS
// ════════════════════════════════════════════════════════════════════
#define MARIO_GRAV  0.5f
#define MARIO_JUMP -9.0f
#define MARIO_SPD   3.0f
#define GROUND_Y   420
#define NUM_PLAT    5
#define NUM_COIN    6
#define NUM_ENEMY   3

struct Platform { int x,y,w; };
struct Coin     { int x,y; bool alive; };
struct Enemy    { float x,y,vx; bool alive; };

struct MarioGame {
  float  px=60,py=GROUND_Y-24,pvx=0,pvy=0;
  bool   onGround=false, jumping=false;
  int    score=0, lives=3;
  float  camX=0;
  bool   running=false, gameOver=false;
  unsigned long lastFrame=0;
  Platform plats[NUM_PLAT]={{0,GROUND_Y,3200},{200,300,120},{400,240,100},{650,280,140},{900,200,160}};
  Coin     coins[NUM_COIN]={{220,268,true},{260,268,true},{420,208,true},{670,248,true},{920,168,true},{1000,168,true}};
  Enemy    enemies[NUM_ENEMY]={{400,GROUND_Y-16,1.0f,true},{700,GROUND_Y-16,-1.0f,true},{960,220-16,1.0f,true}};
} M;

void marioReset() {
  M.px=60; M.py=GROUND_Y-24; M.pvx=0; M.pvy=0;
  M.onGround=false; M.score=0; M.lives=3; M.camX=0; M.gameOver=false;
  for(auto& c:M.coins) c.alive=true;
  for(auto& e:M.enemies){ e.alive=true; }
  M.enemies[0].x=400; M.enemies[1].x=700; M.enemies[2].x=960;
}

void drawMario(int sx,int sy,bool flip) {
  // Hat
  tft.fillRect(sx+2,sy,12,4,C_MRED);
  // Face
  tft.fillRect(sx,sy+4,16,5,C_MSKIN);
  // Eyes
  tft.fillRect(sx+(flip?3:9),sy+5,3,3,C_BLACK);
  // Body
  tft.fillRect(sx+2,sy+9,12,7,C_MRED);
  // Overalls
  tft.fillRect(sx,sy+9,4,5,C_MBLUE);
  tft.fillRect(sx+12,sy+9,4,5,C_MBLUE);
  tft.fillRect(sx+2,sy+14,12,5,C_MBLUE);
  // Feet (alternate for walk animation)
  int wa=(millis()/120)%2;
  tft.fillRect(sx+(wa?0:6),sy+19,6,4,C_MBROWN);
  tft.fillRect(sx+(wa?10:4),sy+19,6,4,C_MBROWN);
}

void drawMarioGame() {
  tft.fillScreen(C_MSKY);
  if(M.gameOver) {
    glowText(SCR_W/2-48,200,"GAME OVER",C_RED,3);
    char buf[20]; snprintf(buf,20,"Score: %d",M.score);
    centreText(SCR_W/2,260,buf,C_WHITE,2,C_MSKY);
    centreText(SCR_W/2,300,"Tap to restart",C_YELLOW,2,C_MSKY);
    return;
  }

  int camX=(int)M.camX;
  // Ground platform
  tft.fillRect(0,GROUND_Y,SCR_W,SCR_H-GROUND_Y,C_MGRND);
  tft.fillRect(0,GROUND_Y,SCR_W,8,0x0300);  // dark top edge of ground

  // Platforms
  for(auto& p:M.plats){
    int sx=p.x-camX, sw=p.w;
    if(sx+sw<0||sx>SCR_W) continue;
    tft.fillRect(sx,p.y,sw,14,C_MPIPE);
    tft.fillRect(sx,p.y,sw,4,0x0700);
  }
  // Coins
  for(auto& c:M.coins){
    if(!c.alive) continue;
    int sx=c.x-camX;
    if(sx<0||sx>SCR_W) continue;
    tft.fillCircle(sx,c.y,6,C_MYELL);
    tft.fillCircle(sx,c.y,3,C_ORANGE);
  }
  // Enemies (Goombas)
  for(auto& e:M.enemies){
    if(!e.alive) continue;
    int sx=(int)e.x-camX;
    if(sx<-20||sx>SCR_W+20) continue;
    tft.fillRect(sx,  (int)e.y,16,6,C_MBROWN);
    tft.fillRect(sx+2,(int)e.y+6,12,8,C_MBROWN);
    tft.fillRect(sx+1,(int)e.y+2,4,4,C_BLACK);
    tft.fillRect(sx+11,(int)e.y+2,4,4,C_BLACK);
  }
  // Mario
  int msx=(int)(M.px-camX);
  drawMario(msx,(int)M.py, M.pvx<0);

  // HUD
  tft.fillRect(0,0,SCR_W,24,0x0000A0>>1);
  tft.setTextSize(1); tft.setTextColor(C_WHITE,C_BLACK);
  char buf[32]; snprintf(buf,32,"SCORE:%05d  LIVES:%d",M.score,M.lives);
  tft.setCursor(4,8); tft.print(buf);
  tft.setCursor(SCR_W-72,8); tft.print("[TAP=JUMP]");
}

void updateMario() {
  unsigned long now=millis();
  if(now-M.lastFrame<33) return; // ~30fps
  M.lastFrame=now;

  Touch t; bool tpressed=false;
  if(touchReady()){ t=readTouch(); tpressed=t.pressed; if(tpressed) lastTouchMs=millis(); }
  if (tpressed && handleGameBack(t)) return;

  // Controls
  if(tpressed) {
    if(t.y>24) {  // not HUD area
      if(t.x<SCR_W/3) M.pvx=-MARIO_SPD;
      else if(t.x>2*SCR_W/3) M.pvx=MARIO_SPD;
      else if(M.onGround) { M.pvy=MARIO_JUMP; M.onGround=false; sndJump(); }
    }
  } else {
    M.pvx*=0.8f; // friction
  }

  // Gravity
  M.pvy+=MARIO_GRAV;
  M.py+=M.pvy; M.px+=M.pvx;
  if(M.px<0) M.px=0;

  // Platform collision
  M.onGround=false;
  for(auto& p:M.plats){
    if(M.px+14>p.x && M.px<p.x+p.w && M.py+24>=p.y && M.py+24<=p.y+16 && M.pvy>=0){
      M.py=p.y-24; M.pvy=0; M.onGround=true;
    }
  }

  // Coin collection
  for(auto& c:M.coins){
    if(!c.alive) continue;
    if(abs((int)M.px+8-c.x)<12 && abs((int)M.py+12-c.y)<12){ c.alive=false; M.score+=100; sndCoin(); }
  }

  // Enemy update & collision
  for(auto& e:M.enemies){
    if(!e.alive) continue;
    e.x+=e.vx;
    // Bounce at platform edges (simplified)
    if(e.x<0||e.x>1200) e.vx*=-1;
    // Stomp
    if(abs((int)M.px+8-(int)e.x+8)<14 && abs((int)M.py+24-(int)e.y)<14){
      if(M.pvy>0){ e.alive=false; M.score+=200; M.pvy=-6; sndStomp(); }
      else { M.lives--; M.px=60; M.py=GROUND_Y-24; M.pvy=0; M.camX=0; sndDeath(); if(M.lives<=0){ M.gameOver=true; sndGameOver(); } }
    }
  }

  // Camera follows Mario
  M.camX=M.px-SCR_W/3;
  if(M.camX<0) M.camX=0;

  // Fall off world
  if(M.py>SCR_H+40){ M.lives--; M.px=60; M.py=GROUND_Y-24; M.pvy=0; M.camX=0; sndDeath(); if(M.lives<=0){ M.gameOver=true; sndGameOver(); } }

  drawMarioGame();
}

// ════════════════════════════════════════════════════════════════════
//  GAME: PACMAN
// ════════════════════════════════════════════════════════════════════
#define PM_COLS  16
#define PM_ROWS  20
#define PM_CELL  14   // 16*14=224, 20*14=280 (fits in 320x480 portrait)
#define PM_OX    ((SCR_W-PM_COLS*PM_CELL)/2)
#define PM_OY    50

// 1=wall 0=dot 2=empty 3=power
const uint8_t pmMaze[PM_ROWS][PM_COLS] PROGMEM = {
  {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
  {1,0,0,0,0,0,0,1,1,0,0,0,0,0,0,1},
  {1,3,1,1,0,1,0,1,1,0,1,0,1,1,3,1},
  {1,0,1,1,0,1,0,0,0,0,1,0,1,1,0,1},
  {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
  {1,0,1,1,0,1,1,1,1,1,1,0,1,1,0,1},
  {1,0,0,0,0,0,0,1,1,0,0,0,0,0,0,1},
  {1,1,1,1,0,1,0,0,0,0,1,0,1,1,1,1},
  {2,2,2,1,0,1,0,2,2,0,1,0,1,2,2,2},
  {1,1,1,1,0,1,0,2,2,0,1,0,1,1,1,1},
  {2,2,2,2,0,0,0,2,2,0,0,0,2,2,2,2},
  {1,1,1,1,0,1,0,2,2,0,1,0,1,1,1,1},
  {2,2,2,1,0,1,0,2,2,0,1,0,1,2,2,2},
  {1,1,1,1,0,1,1,1,1,1,1,0,1,1,1,1},
  {1,0,0,0,0,0,0,1,1,0,0,0,0,0,0,1},
  {1,3,1,0,0,0,0,0,0,0,0,0,0,1,3,1},
  {1,0,1,0,1,1,0,1,1,0,1,1,0,1,0,1},
  {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
  {1,0,1,1,1,1,0,1,1,0,1,1,1,1,0,1},
  {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1}
};

struct PacGame {
  uint8_t maze[PM_ROWS][PM_COLS];
  float px,py,pvx,pvy;  // pacman pos (cell coords)
  int   dx,dy;           // desired direction
  int   score; int lives; int dots;
  bool  gameOver, win;
  // Ghost
  float gx[2],gy[2]; int gdx[2],gdy[2];
  uint16_t gcol[2];
  unsigned long lastFrame;
  bool powered; unsigned long powerEnd;
} PM;

void pacReset(){
  for(int r=0;r<PM_ROWS;r++) for(int c=0;c<PM_COLS;c++) PM.maze[r][c]=pgm_read_byte(&pmMaze[r][c]);
  PM.px=1; PM.py=1; PM.pvx=0; PM.pvy=0; PM.dx=1; PM.dy=0;
  PM.score=0; PM.lives=3; PM.gameOver=false; PM.win=false; PM.powered=false;
  PM.gx[0]=7; PM.gy[0]=8; PM.gdx[0]=1; PM.gdy[0]=0;
  PM.gx[1]=8; PM.gy[1]=8; PM.gdx[1]=-1;PM.gdy[1]=0;
  PM.gcol[0]=C_RED; PM.gcol[1]=C_PINK;
  PM.dots=0;
  for(int r=0;r<PM_ROWS;r++) for(int c=0;c<PM_COLS;c++) if(PM.maze[r][c]==0||PM.maze[r][c]==3) PM.dots++;
}

void drawPacGame(){
  tft.fillScreen(C_BLACK);
  // Maze
  for(int r=0;r<PM_ROWS;r++){
    for(int c=0;c<PM_COLS;c++){
      int x=PM_OX+c*PM_CELL, y=PM_OY+r*PM_CELL;
      uint8_t cell=PM.maze[r][c];
      if(cell==1){ tft.fillRect(x,y,PM_CELL,PM_CELL,0x000D); tft.drawRect(x,y,PM_CELL,PM_CELL,C_BLUE); }
      else if(cell==0) tft.fillCircle(x+PM_CELL/2,y+PM_CELL/2,2,C_YELLOW);
      else if(cell==3) tft.fillCircle(x+PM_CELL/2,y+PM_CELL/2,5,C_WHITE);
    }
  }
  // Pacman
  int pac_px=PM_OX+(int)(PM.px*PM_CELL)+PM_CELL/2;
  int pac_py=PM_OY+(int)(PM.py*PM_CELL)+PM_CELL/2;
  int mouth=(millis()/100)%2?30:5;
  tft.fillCircle(pac_px,pac_py,6,C_YELLOW);
  if(mouth>10) tft.fillTriangle(pac_px,pac_py,pac_px+8*(PM.dx?PM.dx:1),pac_py-4,pac_px+8*(PM.dx?PM.dx:1),pac_py+4,C_BLACK);

  // Ghosts
  for(int i=0;i<2;i++){
    int gx=PM_OX+(int)(PM.gx[i]*PM_CELL)+PM_CELL/2;
    int gy=PM_OY+(int)(PM.gy[i]*PM_CELL)+PM_CELL/2;
    uint16_t gc=PM.powered?C_BLUE:PM.gcol[i];
    tft.fillCircle(gx,gy,6,gc);
    tft.fillRect(gx-6,gy,12,6,gc);
  }
  // HUD
  tft.setTextSize(1); tft.setTextColor(C_YELLOW,C_BLACK);
  char buf[24]; snprintf(buf,24,"SC:%d LV:%d",PM.score,PM.lives);
  tft.setCursor(4,34); tft.print(buf);
  if(PM.gameOver){ centreText(SCR_W/2,200,"GAME OVER!",C_RED,3,C_BLACK); centreText(SCR_W/2,240,"Tap restart",C_WHITE,2,C_BLACK); }
  if(PM.win)     { centreText(SCR_W/2,200,"YOU WIN!",C_GREEN,3,C_BLACK); }
}

void updatePacman(){
  unsigned long now=millis();
  if(now-PM.lastFrame<80) return; PM.lastFrame=now;
  if(PM.gameOver||PM.win) { if(touchReady()){readTouch();lastTouchMs=millis();pacReset();screenDirty=true;} return; }

  if(touchReady()){ Touch et=readTouch(); if (et.pressed && handleGameBack(et)) return; }
  // Touch to set direction
  if(touchReady()){
    Touch t=readTouch(); lastTouchMs=millis();
    int dx=t.x-( PM_OX+(int)(PM.px*PM_CELL)+PM_CELL/2 );
    int dy=t.y-( PM_OY+(int)(PM.py*PM_CELL)+PM_CELL/2 );
    if(abs(dx)>abs(dy)){ PM.dx=dx>0?1:-1; PM.dy=0; } else { PM.dx=0; PM.dy=dy>0?1:-1; }
  }
  // Try desired direction, else continue
  int nr=(int)PM.py+PM.dy, nc=(int)PM.px+PM.dx;
  if(nr>=0&&nr<PM_ROWS&&nc>=0&&nc<PM_COLS&&PM.maze[nr][nc]!=1){
    PM.pvx=PM.dx*0.5f; PM.pvy=PM.dy*0.5f;
  }
  float nx=PM.px+PM.pvx, ny=PM.py+PM.pvy;
  int ci=(int)(nx+0.5f), ri=(int)(ny+0.5f);
  if(ci>=0&&ci<PM_COLS&&ri>=0&&ri<PM_ROWS&&PM.maze[ri][ci]!=1){ PM.px=nx; PM.py=ny; }
  // Eat dot
  int cr=(int)(PM.py+0.5f),cc=(int)(PM.px+0.5f);
  if(cr>=0&&cr<PM_ROWS&&cc>=0&&cc<PM_COLS){
    if(PM.maze[cr][cc]==0){ PM.maze[cr][cc]=2; PM.score+=10; PM.dots--; sndDot(); if(PM.dots<=0) PM.win=true; }
    if(PM.maze[cr][cc]==3){ PM.maze[cr][cc]=2; PM.score+=50; PM.powered=true; sndPower(); PM.powerEnd=now+6000; }
  }
  if(PM.powered&&now>PM.powerEnd) PM.powered=false;
  // Ghost AI (random walk)
  for(int i=0;i<2;i++){
    int gr=(int)(PM.gy[i]+0.5f), gc2=(int)(PM.gx[i]+0.5f);
    int nr2=gr+PM.gdy[i], nc2=gc2+PM.gdx[i];
    if(nr2<0||nr2>=PM_ROWS||nc2<0||nc2>=PM_COLS||PM.maze[nr2][nc2]==1||random(8)==0){
      int dirs[4][2]={{1,0},{-1,0},{0,1},{0,-1}};
      int tries=0;
      do{ int d=random(4); PM.gdx[i]=dirs[d][0]; PM.gdy[i]=dirs[d][1]; tries++; }
      while(tries<8&&(PM.maze[gr+PM.gdy[i]][gc2+PM.gdx[i]]==1));
    }
    PM.gx[i]+=PM.gdx[i]*0.4f; PM.gy[i]+=PM.gdy[i]*0.4f;
    // Collision with Pacman
    if(abs(PM.gx[i]-PM.px)<1.2f&&abs(PM.gy[i]-PM.py)<1.2f){
      if(PM.powered){ PM.gx[i]=7; PM.gy[i]=8; PM.score+=200; sndHit(); }
      else{ PM.lives--; PM.px=1; PM.py=1; PM.pvx=0; PM.pvy=0; sndDeath(); if(PM.lives<=0){ PM.gameOver=true; sndGameOver(); } }
    }
  }
  drawPacGame();
}

// ════════════════════════════════════════════════════════════════════
//  GAME: STARSHIP TROOPERS (Space Shooter)
// ════════════════════════════════════════════════════════════════════
#define SS_MAX_BULLETS 10
#define SS_MAX_BUGS    15

struct SSBullet { float x,y; bool alive; };
struct SSBug    { float x,y,vx,vy; bool alive; uint16_t col; };

struct StarShip {
  float sx;
  SSBullet bullets[SS_MAX_BULLETS];
  SSBug    bugs[SS_MAX_BUGS];
  int  score, lives, wave;
  bool gameOver;
  unsigned long lastFrame, lastShot;
  int  bugsAlive;
} ship;

void ssReset(){
  ship.sx=SCR_W/2; ship.score=0; ship.lives=3; ship.wave=1; ship.gameOver=false; ship.bugsAlive=0;
  for(auto& b:ship.bullets) b.alive=false;
  for(int i=0;i<SS_MAX_BUGS;i++){
    ship.bugs[i]={(float)(20+i*18),(float)(40+(i/8)*32),(float)(random(3)-1)*0.8f,0.3f,true,(uint16_t)(i%2?C_RED:C_GREEN)};
    ship.bugsAlive++;
  }
}

void drawStarship(){
  tft.fillScreen(C_BG);
  if(ship.gameOver){
    glowText(SCR_W/2-48,180,"GAME OVER",C_RED,3);
    char buf[24]; snprintf(buf,24,"SCORE: %d",ship.score);
    centreText(SCR_W/2,230,buf,C_WHITE,2,C_BG);
    centreText(SCR_W/2,270,"Tap to play again",C_CYAN,1,C_BG); return;
  }
  int shipY=SCR_H-40;
  tft.fillTriangle((int)ship.sx,shipY-20,(int)ship.sx-14,shipY+10,(int)ship.sx+14,shipY+10,C_CYAN);
  tft.fillRect((int)ship.sx-3,shipY+8,6,8,C_ORANGE);
  for(auto& b:ship.bullets){ if(!b.alive)continue; tft.fillRect((int)b.x-1,(int)b.y-6,3,10,C_YELLOW); }
  for(auto& b:ship.bugs){
    if(!b.alive)continue;
    tft.fillCircle((int)b.x,(int)b.y,8,b.col);
    tft.drawLine((int)b.x-8,(int)b.y-4,(int)b.x-14,(int)b.y-8,b.col);
    tft.drawLine((int)b.x+8,(int)b.y-4,(int)b.x+14,(int)b.y-8,b.col);
  }
  tft.fillRect(0,0,SCR_W,24,C_BLACK);
  tft.setTextSize(1); tft.setTextColor(C_CYAN,C_BLACK);
  char buf[32]; snprintf(buf,32,"SC:%05d LV:%d W:%d",ship.score,ship.lives,ship.wave);
  tft.setCursor(4,8); tft.print(buf);
}

void updateStarship(){
  unsigned long now=millis();
  if(now-ship.lastFrame<40)return; ship.lastFrame=now;
  if(ship.gameOver){if(touchReady()){readTouch();lastTouchMs=millis();ssReset();drawStarship();}return;}
  if(touchReady()){Touch t=readTouch();lastTouchMs=millis(); if (t.pressed && handleGameBack(t)) return; if(t.y>24)ship.sx=t.x;}
  ship.sx=constrain(ship.sx,16,SCR_W-16);
  if(now-ship.lastShot>250){
    ship.lastShot=now;
    for(auto& b:ship.bullets){if(!b.alive){b={ship.sx,SCR_H-50,true};break;}}
  }
  for(auto& b:ship.bullets){if(b.alive){b.y-=8;if(b.y<24)b.alive=false;}}
  for(auto& b:ship.bugs){
    if(!b.alive)continue;
    b.x+=b.vx; b.y+=b.vy;
    if(b.x<8||b.x>SCR_W-8)b.vx*=-1;
    if(b.y>SCR_H-50){ship.lives--;b.alive=false;ship.bugsAlive--;sndDeath();if(ship.lives<=0){ship.gameOver=true;sndGameOver();}}
    for(auto& blt:ship.bullets){if(!blt.alive)continue;if(abs(blt.x-b.x)<12&&abs(blt.y-b.y)<12){blt.alive=false;b.alive=false;ship.bugsAlive--;ship.score+=100; sndHit();}}
  }
  if(ship.bugsAlive<=0){
    ship.wave++; ship.bugsAlive=0;
    for(int i=0;i<min(SS_MAX_BUGS,8+ship.wave*2);i++){
      ship.bugs[i]={(float)(16+i*20),(float)(30+(i/8)*28),(float)(random(3)-1)*(0.8f+ship.wave*0.2f),0.25f+ship.wave*0.05f,true,(uint16_t)(i%3==0?C_RED:i%3==1?C_PURPLE:C_ORANGE)};
      ship.bugsAlive++;
    }
  }
  drawStarship();
}
// ════════════════════════════════════════════════════════════════════
//  GAME: MEMORY  (4x4 card flip)
// ════════════════════════════════════════════════════════════════════
#define MEM_COLS 4
#define MEM_ROWS 4
#define MEM_CW   70
#define MEM_CH   58
#define MEM_PAD  4
#define MEM_OX   ((SCR_W-(MEM_COLS*(MEM_CW+MEM_PAD)-MEM_PAD))/2)
#define MEM_OY   55

uint16_t memColors[8]={C_RED,C_GREEN,C_BLUE,C_YELLOW,C_PURPLE,C_ORANGE,C_PINK,C_CYAN};

struct MemGame {
  uint8_t cards[MEM_ROWS][MEM_COLS];
  bool    flipped[MEM_ROWS][MEM_COLS];
  bool    matched[MEM_ROWS][MEM_COLS];
  int     firstR,firstC,secondR,secondC,state,score,pairs;
  bool    win;
  unsigned long checkStart;
} MEM;

void memShuffle(){
  uint8_t vals[16]; for(int i=0;i<16;i++) vals[i]=i/2;
  for(int i=15;i>0;i--){int j=random(i+1);uint8_t t=vals[i];vals[i]=vals[j];vals[j]=t;}
  for(int r=0;r<MEM_ROWS;r++) for(int c=0;c<MEM_COLS;c++){
    MEM.cards[r][c]=vals[r*MEM_COLS+c]; MEM.flipped[r][c]=MEM.matched[r][c]=false;
  }
  MEM.state=0; MEM.score=0; MEM.pairs=0; MEM.win=false;
}

void drawMemCard(int r,int c){
  int x=MEM_OX+c*(MEM_CW+MEM_PAD), y=MEM_OY+r*(MEM_CH+MEM_PAD);
  uint16_t col=MEM.matched[r][c]?C_DGRAY:MEM.flipped[r][c]?memColors[MEM.cards[r][c]]:C_SURF2;
  neonBox(x,y,MEM_CW,MEM_CH,MEM.flipped[r][c]||MEM.matched[r][c]?col:C_PURPLE,col);
  if(MEM.flipped[r][c]&&!MEM.matched[r][c]){
    char v[4]; snprintf(v,4,"%d",MEM.cards[r][c]+1);
    tft.setTextSize(3); tft.setTextColor(C_WHITE,col);
    int16_t tw=strlen(v)*18; tft.setCursor(x+(MEM_CW-tw)/2,y+MEM_CH/2-10); tft.print(v);
  }
}

void drawMemGame(){
  tft.fillScreen(C_BG);
  char buf[24]; snprintf(buf,24,"MEMORY  Pairs:%d/8",MEM.pairs);
  centreText(SCR_W/2,8,buf,C_PURPLE,2,C_BG);
  hRule(34,C_PURPLE);
  for(int r=0;r<MEM_ROWS;r++) for(int c=0;c<MEM_COLS;c++) drawMemCard(r,c);
  snprintf(buf,24,"Score: %d",MEM.score);
  tft.setTextSize(1); tft.setTextColor(C_LGRAY,C_BG); tft.setCursor(8,460); tft.print(buf);
  tft.setTextColor(C_DGRAY,C_BG); tft.setCursor(SCR_W-88,460); tft.print("[top-left=back]");
  if(MEM.win){glowText(SCR_W/2-48,420,"YOU WIN!",C_GREEN,3);}
}

void updateMemory(){
  if(MEM.win){if(touchReady()){readTouch();lastTouchMs=millis();memShuffle();drawMemGame();}return;}
  if(MEM.state==2){
    if(millis()-MEM.checkStart>900){
      bool ok=(MEM.cards[MEM.firstR][MEM.firstC]==MEM.cards[MEM.secondR][MEM.secondC]);
      if(ok){MEM.matched[MEM.firstR][MEM.firstC]=MEM.matched[MEM.secondR][MEM.secondC]=true;MEM.score+=100;MEM.pairs++; sndMatch(); if(MEM.pairs==8){MEM.win=true;sndWin();}}
      else  {MEM.flipped[MEM.firstR][MEM.firstC]=MEM.flipped[MEM.secondR][MEM.secondC]=false; sndBuzz();}
      MEM.state=0; drawMemGame();
    }
    return;
  }
  if(!touchReady())return;
  Touch t=readTouch(); lastTouchMs=millis();
  if (handleGameBack(t)) return;
  int c=(t.x-MEM_OX)/(MEM_CW+MEM_PAD), r=(t.y-MEM_OY)/(MEM_CH+MEM_PAD);
  if(r<0||r>=MEM_ROWS||c<0||c>=MEM_COLS)return;
  if(MEM.flipped[r][c]||MEM.matched[r][c])return;
  MEM.flipped[r][c]=true; sndClick();
  if(MEM.state==0){MEM.firstR=r;MEM.firstC=c;MEM.state=1;}
  else{MEM.secondR=r;MEM.secondC=c;MEM.state=2;MEM.checkStart=millis();}
  drawMemCard(r,c);
}

// ════════════════════════════════════════════════════════════════════
//  GAME: COLOR MATCH
// ════════════════════════════════════════════════════════════════════
uint16_t cmPalette[]={C_RED,C_GREEN,C_BLUE,C_YELLOW,C_PURPLE,C_ORANGE,C_PINK,C_CYAN};
const char* cmNames[]={"RED","GREEN","BLUE","YELLOW","PURPLE","ORANGE","PINK","CYAN"};

struct ColMatch {
  uint16_t targetCol; const char* targetName;
  uint16_t options[4]; const char* names[4];
  int correct, score, streak;
  unsigned long roundStart;
} CM;

void cmNewRound(){
  int ti=random(8); CM.targetCol=cmPalette[ti]; CM.targetName=cmNames[ti];
  CM.correct=random(4);
  bool used[8]={0}; used[ti]=true;
  CM.options[CM.correct]=CM.targetCol; CM.names[CM.correct]=CM.targetName;
  for(int i=0;i<4;i++){
    if(i==CM.correct)continue;
    int ci; do{ci=random(8);}while(used[ci]);
    used[ci]=true; CM.options[i]=cmPalette[ci]; CM.names[i]=cmNames[ci];
  }
  CM.roundStart=millis();
}

void drawColMatch(){
  tft.fillScreen(C_BG);
  centreText(SCR_W/2,8,"COLOR MATCH",C_ORANGE,2,C_BG);
  hRule(34,C_ORANGE);
  unsigned long el=millis()-CM.roundStart;
  int bw=(int)((max(0UL,5000-el)*(SCR_W-20))/5000);
  tft.fillRect(10,42,SCR_W-20,8,C_SURF);
  tft.fillRect(10,42,bw,8,el<3500?C_GREEN:C_RED);
  tft.fillRoundRect(50,56,SCR_W-100,108,10,CM.targetCol);
  tft.setTextSize(3); tft.setTextColor(C_WHITE,CM.targetCol);
  int16_t tw=strlen(CM.targetName)*18;
  tft.setCursor(50+(SCR_W-100-tw)/2,98); tft.print(CM.targetName);
  tft.setTextSize(1); tft.setTextColor(C_LGRAY,C_BG); centreText(SCR_W/2,170,"TAP THE MATCHING COLOR",C_LGRAY,1,C_BG);
  const int BW=(SCR_W-28)/2,BH=96;
  for(int i=0;i<4;i++){
    int bx=8+(i%2)*(BW+12),by=180+(i/2)*(BH+10);
    tft.fillRoundRect(bx,by,BW,BH,8,CM.options[i]);
    tft.drawRoundRect(bx,by,BW,BH,8,C_WHITE);
    tft.setTextSize(2); tft.setTextColor(C_WHITE,CM.options[i]);
    tw=strlen(CM.names[i])*12; tft.setCursor(bx+(BW-tw)/2,by+BH/2-8); tft.print(CM.names[i]);
  }
  char buf[32]; snprintf(buf,32,"Score:%d  Streak:%d",CM.score,CM.streak);
  tft.setTextSize(1); tft.setTextColor(C_LGRAY,C_BG); tft.setCursor(8,392); tft.print(buf);
  if(el>5000){CM.streak=0;cmNewRound();drawColMatch();}
}

void updateColMatch(){
  if(!touchReady())return;
  Touch t=readTouch(); lastTouchMs=millis();
  if (handleGameBack(t)) return;
  const int BW=(SCR_W-28)/2,BH=96;
  for(int i=0;i<4;i++){
    int bx=8+(i%2)*(BW+12),by=180+(i/2)*(BH+10);
    if(t.x>=bx&&t.x<bx+BW&&t.y>=by&&t.y<by+BH){
      if(i==CM.correct){CM.score+=10+CM.streak*5;CM.streak++; sndCorrect();}else{CM.streak=0; sndBuzz();}
      cmNewRound(); drawColMatch(); return;
    }
  }
}

// ════════════════════════════════════════════════════════════════════
//  GAME: MATH QUIZ
// ════════════════════════════════════════════════════════════════════
struct MathGame {
  int a,b,op,answer,choices[4],correct,score,streak,lives;
  bool gameOver;
  unsigned long roundStart;
} MQ;

void mqNewRound(){
  MQ.op=random(3);
  if(MQ.op==0){MQ.a=random(1,21);MQ.b=random(1,21);MQ.answer=MQ.a+MQ.b;}
  else if(MQ.op==1){MQ.a=random(2,21);MQ.b=random(1,MQ.a);MQ.answer=MQ.a-MQ.b;}
  else{MQ.a=random(2,13);MQ.b=random(2,11);MQ.answer=MQ.a*MQ.b;}
  MQ.correct=random(4); MQ.choices[MQ.correct]=MQ.answer;
  bool used[4]={0}; used[MQ.correct]=true;
  for(int i=0;i<4;i++){
    if(i==MQ.correct)continue;
    int v; int tries=0;
    do{v=MQ.answer+(random(11)-5);tries++;} while((v==MQ.answer||v<0)&&tries<30);
    MQ.choices[i]=v;
  }
  MQ.roundStart=millis();
}

void drawMathGame(){
  tft.fillScreen(C_BG);
  if(MQ.gameOver){
    glowText(SCR_W/2-48,180,"GAME OVER!",C_RED,3);
    char b[24]; snprintf(b,24,"Score: %d",MQ.score);
    centreText(SCR_W/2,240,b,C_WHITE,2,C_BG);
    centreText(SCR_W/2,280,"Tap to play again",C_CYAN,1,C_BG); return;
  }
  centreText(SCR_W/2,8,"MATH QUIZ",C_GREEN,2,C_BG);
  hRule(34,C_GREEN);
  char buf[32]; const char* ops[]={"+","-","x"};
  snprintf(buf,32,"%d %s %d = ?",MQ.a,ops[MQ.op],MQ.b);
  tft.setTextSize(4); tft.setTextColor(C_YELLOW,C_BG);
  int16_t tw=strlen(buf)*24; tft.setCursor(SCR_W/2-tw/2,88); tft.print(buf);
  unsigned long el=millis()-MQ.roundStart;
  int bw2=(int)((max(0UL,8000-el)*(SCR_W-20))/8000);
  tft.fillRect(10,152,SCR_W-20,8,C_SURF); tft.fillRect(10,152,bw2,8,el<5000?C_GREEN:C_RED);
  const int BW=(SCR_W-28)/2,BH=92;
  uint16_t cc[]={C_CYAN,C_PURPLE,C_ORANGE,C_GREEN};
  for(int i=0;i<4;i++){
    int bx=8+(i%2)*(BW+12),by=168+(i/2)*(BH+10);
    neonBox(bx,by,BW,BH,cc[i],C_SURF);
    snprintf(buf,8,"%d",MQ.choices[i]);
    tft.setTextSize(4); tft.setTextColor(cc[i],C_SURF);
    tw=strlen(buf)*24; tft.setCursor(bx+(BW-tw)/2,by+BH/2-16); tft.print(buf);
  }
  snprintf(buf,32,"Sc:%d Str:%d Lv:%d",MQ.score,MQ.streak,MQ.lives);
  tft.setTextSize(1); tft.setTextColor(C_LGRAY,C_BG); tft.setCursor(8,390); tft.print(buf);
  if(el>8000){MQ.lives--;MQ.streak=0;if(MQ.lives<=0){MQ.gameOver=true;drawMathGame();}else{mqNewRound();drawMathGame();}}
}

void updateMath(){
  if(MQ.gameOver){if(touchReady()){readTouch();lastTouchMs=millis();MQ.score=0;MQ.streak=0;MQ.lives=3;MQ.gameOver=false;mqNewRound();drawMathGame();}return;}
  if(!touchReady())return;
  Touch t=readTouch(); lastTouchMs=millis();
  if (handleGameBack(t)) return;
  const int BW=(SCR_W-28)/2,BH=92;
  for(int i=0;i<4;i++){
    int bx=8+(i%2)*(BW+12),by=168+(i/2)*(BH+10);
    if(t.x>=bx&&t.x<bx+BW&&t.y>=by&&t.y<by+BH){
      if(i==MQ.correct){MQ.score+=10+MQ.streak*5;MQ.streak++; sndCorrect();}
      else{MQ.lives--;MQ.streak=0; sndWrong(); if(MQ.lives<=0){MQ.gameOver=true;sndGameOver();drawMathGame();return;}}
      mqNewRound(); drawMathGame(); return;
    }
  }
}
// ════════════════════════════════════════════════════════════════════
//  SCREEN ROUTER  — initialise + dispatch touch
// ════════════════════════════════════════════════════════════════════
void initScreen(Screen s) {
  switch(s) {
    case SCR_MAIN:        drawMain();         break;
    case SCR_GAMES:       drawGames();        break;
    case SCR_SENSORS:     drawSensors();      break;
    case SCR_SENS_EYES:   drawSensEyes();     break;
    case SCR_SENS_NOSE:   drawSensNose();     break;
    case SCR_SENS_BRAIN:  if(showBrainLog) drawBrainLog(); else drawSensBrain(); break;
    case SCR_SENS_TUMMY:  drawSensTummy();    break;
    case SCR_COMMS:       drawComms();        break;
    case SCR_SETTINGS:    drawSettings();     break;
    case GAME_MARIO:      marioReset();       drawMarioGame();  break;
    case GAME_PACMAN:     pacReset();         drawPacGame();    break;
    case GAME_STARSHIP:   ssReset();          drawStarship();   break;
    case GAME_MEMORY:     memShuffle();       drawMemGame();    break;
    case GAME_COLORMATCH: cmNewRound();       drawColMatch();   break;
    case GAME_MATH:       MQ.score=0;MQ.streak=0;MQ.lives=3;MQ.gameOver=false;mqNewRound(); drawMathGame(); break;
    default: break;
  }
}

void handleTouch(Touch& t) {
  switch(curScreen) {
    case SCR_MAIN:       handleMainTouch(t);     break;
    case SCR_GAMES:      handleGamesTouch(t);    break;
    case SCR_SENSORS:    handleSensorsTouch(t);  break;
    case SCR_SENS_BRAIN: handleBrainTouch(t);    break;
    case SCR_SETTINGS:   handleSettingsTouch(t); break;
    // Sensor submenus — BACK = top-left corner
    case SCR_SENS_EYES:
    case SCR_SENS_NOSE:
    case SCR_SENS_TUMMY:
      if(t.y<72){curScreen=SCR_SENSORS;screenDirty=true;}
      break;
    case SCR_COMMS:
      if(t.y<72){curScreen=SCR_MAIN;screenDirty=true;} // full header = back
      break;
    // Game back buttons (top-left tap)
    case GAME_MARIO:
      if(t.y<26&&t.x<SCR_W/2&&!M.gameOver){curScreen=SCR_GAMES;screenDirty=true;}
      else if(M.gameOver){marioReset();drawMarioGame();}
      break;
    case GAME_PACMAN:
      if(t.y<40&&t.x<70){curScreen=SCR_GAMES;screenDirty=true;}
      break;
    case GAME_STARSHIP:
      if(t.y<26&&t.x<80&&!ship.gameOver){curScreen=SCR_GAMES;screenDirty=true;}
      break;
    default: break;
  }
}

// ════════════════════════════════════════════════════════════════════

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("[PICO] BuddyBot Dash v6.1 booting...");

  // ── USB Serial debug (connect Pico USB to PC while S9 uses Mega USB) ──
  Serial.begin(115200);
  delay(100);
  Serial.println("[PICO] BuddyBot Pico2 Dash v6.1 — USB debug active");

  // ── TFT ──────────────────────────────────────────────────────────
  tft.init();
  tft.setRotation(ROTATION);
  tft.invertDisplay(false);
  tft.fillScreen(C_BG);
  pinMode(22, OUTPUT); digitalWrite(22, HIGH);   // backlight ON

  // ── Boot splash ───────────────────────────────────────────────────
  centreText(SCR_W/2, SCR_H/2-24, "AJ2BUDDYCOMMS", C_CYAN, 2, C_BG);
  centreText(SCR_W/2, SCR_H/2+8,  "BuddyBot v6.1",  C_LGRAY,1, C_BG);
  delay(1200);
  sndBoot();

  // ── Audio (SC8002B on GP14) ───────────────────────────────────────
  pinMode(AUDIO_PIN, OUTPUT);
  digitalWrite(AUDIO_PIN, LOW);

  // ── Touch (FT6336U on Wire1 / I2C1 / GP26+GP27) ──────────────────
  // Hard-reset the FT6336U
  pinMode(PIN_CTP_RST, OUTPUT);
  digitalWrite(PIN_CTP_RST, LOW);  delay(50);
  digitalWrite(PIN_CTP_RST, HIGH); delay(300);

  // Wire1 = I2C1 — GP26(SDA) / GP27(SCL) on RP2040
  Wire1.setSDA(PIN_CTP_SDA);
  Wire1.setSCL(PIN_CTP_SCL);
  Wire1.begin();
  Wire1.setClock(400000);
  delay(50);

  // Interrupt pin (open-drain — needs pullup)
  pinMode(PIN_CTP_INT, INPUT_PULLUP);

  // Quick I2C probe to confirm FT6336U is responding
  Wire1.beginTransmission(CTP_ADDR);
  int err = Wire1.endTransmission();
  Serial.print("[TOUCH] FT6336U probe at 0x38: ");
  Serial.println(err == 0 ? "OK" : "FAIL (check wiring)");

  // FT6336U init — MUST set polling mode or registers stay zero when touched.
  // Default from factory is interrupt/trigger mode (0xA4 = 0x01) which only
  // updates registers when INT fires. Since we poll by timer, force polling mode.
  if (err == 0) {
    Wire1.beginTransmission(CTP_ADDR); Wire1.write(0x00); Wire1.write(0x00); Wire1.endTransmission(); delay(5);
    Wire1.beginTransmission(CTP_ADDR); Wire1.write(0xA4); Wire1.write(0x00); Wire1.endTransmission(); delay(5);
    Serial.println("[TOUCH] Polling mode set (reg 0xA4 = 0x00)");
  }

  // ── Mega UART ─────────────────────────────────────────────────────
  Serial1.setTX(0); Serial1.setRX(1);
  MEGA_SERIAL.begin(115200);
  delay(100);
  MEGA_SERIAL.println("PONG");

  // ── Main screen ───────────────────────────────────────────────────
  initScreen(SCR_MAIN);
  screenDirty = false;
  Serial.println("[PICO] Ready.");
}

// ════════════════════════════════════════════════════════════════════
//  LOOP
// ════════════════════════════════════════════════════════════════════
// Returns true if current screen is a game (games handle own touch)
bool isGameScreen() {
  return (curScreen==GAME_MARIO || curScreen==GAME_PACMAN ||
          curScreen==GAME_STARSHIP || curScreen==GAME_MEMORY ||
          curScreen==GAME_COLORMATCH || curScreen==GAME_MATH);
}

bool gameDirty = true;   // games set this when they need a full redraw

// Consistent top-left back button for all games (larger area for easier exit)
bool handleGameBack(const Touch& t) {
  if (t.pressed && t.y < 50 && t.x < 100) {
    curScreen = SCR_GAMES;
    screenDirty = true;
    return true;
  }
  return false;
}

void loop() {
  sndUpdate();
  handleMegaSerial();

  if (megaLinked && millis()-lastMegaRx > 12000) {
    megaLinked = false;
    if (!isGameScreen()) screenDirty = true;
  }

  if (isGameScreen()) {
    // Games handle ALL touch and drawing internally — main loop stays out
    if      (curScreen==GAME_MARIO)      updateMario();
    else if (curScreen==GAME_PACMAN)     updatePacman();
    else if (curScreen==GAME_STARSHIP)   updateStarship();
    else if (curScreen==GAME_MEMORY)     updateMemory();
    else if (curScreen==GAME_COLORMATCH) updateColMatch();
    else if (curScreen==GAME_MATH)       updateMath();

    // Only do expensive full redraw when a game actually needs it
    if (gameDirty && isGameScreen()) {
      gameDirty = false;
    }

    // If game just exited, draw the menu it returned to
    if (!isGameScreen()) { screenDirty=false; initScreen(curScreen); }
  } else {
    // Menu/sensor screens
    if (screenDirty) { screenDirty=false; initScreen(curScreen); }
    static unsigned long lastRefresh=0;
    bool isSensor=(curScreen==SCR_SENS_EYES||curScreen==SCR_SENS_NOSE||
                   curScreen==SCR_SENS_BRAIN||curScreen==SCR_SENS_TUMMY||
                   false); // SCR_COMMS removed — no timer refresh needed
    if (isSensor && millis()-lastRefresh>3000) { lastRefresh=millis(); initScreen(curScreen); }

    // Touch only handled here — not during games
    if (millis()-lastTouchMs>100) {
      Touch t=readTouch();
      if (t.pressed) {
        lastTouchMs=millis();
        Screen prev=curScreen;
        handleTouch(t);
        if (curScreen!=prev) { screenDirty=false; initScreen(curScreen); }
      }
    }
  }
}













