/*
 * BuddyBot  Pico W-2023 Dashboard  V1.1  PORTRAIT 320x480
 * Board : RP2040 Pico W-2023  TFT_eSPI  FT6336U touch  WiFiEspAT
 * Audio : Keyestudio SC8002B Power Amplifier (GP14 IN)
 *         Non-blocking hardware-PWM tone engine — receives BEEP:/BEEP:SEQ:
 *         from Mega (Timer2 clash on Mega freed motor PWM pins 9/10).
 * Serial: GP4(TX)/GP5(RX) <-> Mega Serial1 @ 115200 (UART1, GP0/GP1 are ESP8285)
 *         S9 <-> USB CDC (Serial). Bridging via serial_bridge.h
 *         Mega ↔ Pico ↔ S9 line-forwarding is inside bridgeLoop(); this
 *         sketch implements onMegaLine() and onS9Line() callbacks.
 */

#include <TFT_eSPI.h>
#include <Wire.h>
#include <SPI.h>
#include <math.h>
#include <WiFiEspAT.h>
#include "serial_bridge.h"    // S9↔Mega USB↔UART1 bridge (owns Serial2 GP4/GP5)
extern volatile bool wifiOK;
extern volatile bool webCmdReady;
extern volatile char webCmd[64];
void updateShared();

TFT_eSPI tft = TFT_eSPI();

struct TouchPt { int16_t x,y; bool pressed; };
struct SSBullet { float x,y; bool alive; };
struct SSBug    { float x,y,vx,vy; bool alive; uint16_t col; };
unsigned long lastTouchMs = 0;

#define SCR_W    320
#define SCR_H    480
#define ROTATION   0
#define AUDIO_PIN  14
#define SND_QUEUE  12
#define PIN_CTP_SDA  26
#define PIN_CTP_SCL  27
#define PIN_CTP_INT  28
#define PIN_CTP_RST  15
#define CTP_ADDR     0x38
// FT6336U reports native portrait (x 0..319, y 0..479) — no flip needed for ROTATION 0
#define TOUCH_FLIP_X false
#define TOUCH_FLIP_Y false
#define HDR_H        57
#define GAME_TOP     40

enum Screen : uint8_t {
  SCR_MAIN, SCR_GAMES, SCR_SENSORS,
  SCR_SENS_EYES, SCR_SENS_NOSE, SCR_SENS_BRAIN, SCR_SENS_TUMMY,
  SCR_COMMS, SCR_SETTINGS, SCR_LIGHTS,
  GAME_MARIO, GAME_PACMAN, GAME_STARSHIP, GAME_MEMORY, GAME_COLORMATCH, GAME_MATH
};
Screen curScreen = SCR_MAIN, prevScreen = SCR_MAIN;
bool   screenDirty = true;
bool   headerDirty = true;
bool   bodyDirty   = true;
Screen paintedScreen = (Screen)255;

static void requestHeaderRefresh() { headerDirty = true; }
static void requestBodyRefresh()   { if (curScreen < GAME_MARIO) bodyDirty = true; }
static void requestFullRefresh()   { screenDirty = true; headerDirty = true; bodyDirty = true; }

TouchPt readTouch() {
  TouchPt t = {0, 0, false};
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
  t.x = TOUCH_FLIP_X ? constrain((SCR_W - 1) - rx, 0, SCR_W - 1) : constrain(rx, 0, SCR_W - 1);
  t.y = TOUCH_FLIP_Y ? constrain((SCR_H - 1) - ry, 0, SCR_H - 1) : constrain(ry, 0, SCR_H - 1);
  t.pressed = true;
  return t;
}

bool touchReady() { return (millis() - lastTouchMs > 100); }

// Colour palette (RGB565)
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

// Telemetry
struct Telem {
  int   gas=0; float temp=0,hum=0,volt=0,amps=0; int pct=0;
  long  dFront=-1,dRear=-1,dLeft=-1,dRight=-1;
  bool  megaUartOk=false,wifiOk=false,s9ok=false,estop=false,autoM=false;
  bool  irFront=false,irRear=false,pir=false,tilt=false;
  char  fw[16]=""; char mode[16]="NORMAL";
  bool  autodock=false; char dockSt[10]="IDLE";
  float heading=-1; int gpsSats=0; bool cob=false;
  char ledMode[10]="OFF"; char ledWhite[6]="OFF"; int ledBright=255;
} T;
bool brainToggle[6]={true,true,true,true,true,true};
const char* brainLabels[6]={"TEMP","GAS","VOLTAGE","ULTRASONICS","STATUS","MOTOR"};

// MEGA_SERIAL — writes go directly to Serial2 (the bridge's UART).
// The bridge owns bidirectional forwarding S9↔Mega; this alias exists so
// legacy direct .print/.println() call sites still work without rewriting.
#define MEGA_SERIAL Serial2

// GP14: SC8002B audio amplifier input. Non-blocking hardware-PWM tone engine.
#define AUDIO_PIN 14
#define MEGA_BUF_LEN  320
#define MEGA_PING_MS  5000
#define MEGA_USB_ECHO 1
char megaBuf[MEGA_BUF_LEN]; uint16_t megaBufLen=0;
unsigned long lastMegaRx=0;
unsigned long lastS9Rx=0;
unsigned long lastPingTx=0;
uint8_t pingSeq=0;
bool megaLinked=false;

// WiFi credentials — shared between core0 (Mega link) and core1 (ESP8285)
char wifiSsid[33] = "YOUR_SSID";
char wifiPass[64] = "YOUR_PASS";
volatile bool wifiReconnectPending = false;
char pendingWifiSsid[33] = {0};
char pendingWifiPass[64] = {0};
volatile bool wifiIpReady = false;
char pendingWifiIp[16] = {0};

#define DBG_LINES 10
#define DBG_LEN   48
char   dbgLog[DBG_LINES][DBG_LEN];
int    dbgHead = 0;

void dbgPush(const char* msg) {
  strncpy(dbgLog[dbgHead], msg, DBG_LEN-1);
  dbgLog[dbgHead][DBG_LEN-1] = 0;
  dbgHead = (dbgHead+1) % DBG_LINES;
  Serial.println(msg);
}

void parseWifiConnect(const char* line) {
  const char* p = line + 5;
  const char* sep = strchr(p, '|');
  if (!sep) return;
  int slen = (int)(sep - p);
  if (slen <= 0 || slen > 32) return;
  strncpy(pendingWifiSsid, p, slen);
  pendingWifiSsid[slen] = 0;
  strncpy(pendingWifiPass, sep + 1, 63);
  pendingWifiPass[63] = 0;
  wifiReconnectPending = true;
  dbgPush("[PICO] WiFi connect requested");
}

// Audio queue
struct Note { uint16_t freq; uint16_t dur; };
Note          sndQ[SND_QUEUE];
int           sndHead=0, sndTail=0, sndLen=0;
unsigned long sndEndMs=0;

void sndUpdate(){
  if(sndLen==0||millis()<sndEndMs)return;
  if(sndQ[sndHead].freq == 0){
    analogWrite(AUDIO_PIN, 0);
  } else {
    analogWriteFreq(sndQ[sndHead].freq);
    analogWrite(AUDIO_PIN, 127);
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

// ─── Beep handler (V1.1) ─────────────────────────────────────────────────────
//  Called by onMegaLine() when a BEEP: line arrives over Serial2.
//  Wire protocol from Mega V37:
//    BEEP:<hz>:<ms>              single tone (queued after any current beep)
//    BEEP:SEQ:hz,ms,hz,ms,...    multi-tone chirp (queued in order)
//  Zero-length or freq==0 is treated as silence (gap between tones).
void handleBeep(const char* line){
  // Skip leading "BEEP:"
  const char* p = line + 5;

  // Multi-tone sequence: "SEQ:hz,ms,hz,ms,..."
  if (strncmp(p,"SEQ:",4)==0) {
    p += 4;
    while (*p) {
      long f = strtol(p,(char**)&p,10);
      if (*p==',') p++;
      long d = strtol(p,(char**)&p,10);
      if (d <= 0) break;
      sndQ1((uint16_t)constrain(f,0,20000),(uint16_t)constrain(d,1,5000));
      if (*p==',') p++;
      else break;
    }
    return;
  }

  // Single tone: "hz:ms"
  long f = strtol(p,(char**)&p,10);
  if (*p==':') p++;
  long d = strtol(p,NULL,10);
  if (d <= 0) return;
  sndQ1((uint16_t)constrain(f,0,20000),(uint16_t)constrain(d,1,5000));
}

// Parsers
static void markTelemDirty(){
  requestHeaderRefresh();
  requestBodyRefresh();
}

static void markDirty(){
  if (curScreen >= GAME_MARIO) return;
  markTelemDirty();
}

bool parseStatFields(const char* s,char* fields[],int maxFields){
  static char b[180];
  strncpy(b,s,sizeof(b)-1); b[sizeof(b)-1]=0;
  int n=0;
  char* p=b;
  while(n<maxFields){
    fields[n++]=p;
    char* col=strchr(p,':');
    if(!col) break;
    *col=0;
    p=col+1;
  }
  return n>=maxFields;
}

void parseStat(const char* s){
  char* f[10];
  if(!parseStatFields(s+5,f,10)){
    dbgPush("STAT:parse err");
    return;
  }
  T.gas=atoi(f[0]);
  T.temp=atof(f[1]);
  T.hum=atof(f[2]);
  T.estop=(atoi(f[3])>0);
  T.pir=(f[4][0]=='1');
  T.tilt=(f[5][0]=='1');
  T.volt=atof(f[7]);
  T.pct=atoi(f[8]);
  T.amps=atof(f[9]);
  markTelemDirty();
  updateShared();
}
void parseIR(const char* s){
  static char b[16]; static char* f[2]; int n=0;
  strncpy(b,s+3,sizeof(b)-1); b[sizeof(b)-1]=0;
  char* p=strtok(b,","); while(p&&n<2){f[n++]=p;p=strtok(NULL,",");}
  if(n<2)return;
  bool nf=(f[0][0]=='1'), nr=(f[1][0]=='1');
  T.irFront=nf; T.irRear=nr;
  markTelemDirty(); updateShared();
}
void parseUS(const char* s){
  static char b[64]; static char* f[4]; int n=0;
  strncpy(b,s+3,sizeof(b)-1); b[sizeof(b)-1]=0;
  char* p=strtok(b,","); while(p&&n<4){f[n++]=p;p=strtok(NULL,",");}
  if(n<4)return;
  long nf=atol(f[0]),nr=atol(f[1]),nl2=atol(f[2]),nrr=atol(f[3]);
  T.dFront=nf; T.dRear=nr; T.dLeft=nl2; T.dRight=nrr;
  markTelemDirty(); updateShared();
}
void parseStatus(const char* s){
  // V37 STATUS| — no R3/ESP fields; motors are on-board TB6612 on the Mega.
  T.megaUartOk = megaLinked;
  T.wifiOk     = wifiOK;
  const char* tags[]={"S9:","ESTOP:","AUTO:"};
  bool* vals[]={&T.s9ok,&T.estop,&T.autoM};
  char trueChars[]={'O','Y','O'};
  T.autodock=(strstr(s,"ADOCK:ON")!=NULL);
  const char* ds=strstr(s,"DOCKST:");
  if(ds){
    ds+=7;
    const char* de=strchr(ds,'|');
    size_t l=de?(size_t)(de-ds):strlen(ds);
    if(l>9) l=9;
    char nd[10];
    memcpy(nd,ds,l); nd[l]=0;
    if(strncmp(nd,T.dockSt,10)) strncpy(T.dockSt,nd,10);
  }
  const char* mp=strstr(s,"MODE:");
  if(mp){
    char nm[16];
    strncpy(nm,mp+5,15); nm[15]=0;
    char* nl2=strchr(nm,'|'); if(nl2) *nl2=0;
    if(strncmp(nm,T.mode,15)) strncpy(T.mode,nm,16);
  }
  const char* fp=strstr(s,"FW:");
  if(fp){
    char nfw[16];
    strncpy(nfw,fp+3,15); nfw[15]=0;
    char* nl2=strchr(nfw,'|'); if(nl2) *nl2=0;
    if(strncmp(nfw,T.fw,15)) strncpy(T.fw,nfw,16);
  }
  for(int i=0;i<3;i++){
    const char* pp=strstr(s,tags[i]);
    if(pp) *(vals[i])=(*(pp+strlen(tags[i]))==trueChars[i]);
  }
  const char* hd=strstr(s,"HDG:"); if(hd) T.heading=atof(hd+4);
  const char* gp=strstr(s,"SAT:"); if(gp) T.gpsSats=atoi(gp+4);
  const char* bp=strstr(s,"BAT:"); if(bp) T.volt=atof(bp+4);
  const char* pp=strstr(s,"PCT:"); if(pp) T.pct=atoi(pp+4);
  markTelemDirty();
  updateShared();
}
void parseHDG(const char* s){T.heading=atof(s+4);markTelemDirty();updateShared();}
void parseGPS(const char* s){
  static char buf[48];
  strncpy(buf,s+4,47); buf[47]=0;
  char* g[3]; int n=0;
  char* p=buf;
  while(n<3){
    g[n++]=p;
    char* comma=strchr(p,',');
    if(!comma) break;
    *comma=0; p=comma+1;
  }
  if(n>=3){ T.gpsSats=atoi(g[2]); markTelemDirty(); }
  updateShared();
}

void parseLed(const char* s){
  const char* m=strstr(s,"MODE:");
  if(m){m+=5;const char* e=strchr(m,'|');size_t l=e?(size_t)(e-m):strlen(m);if(l>9)l=9;memcpy(T.ledMode,m,l);T.ledMode[l]=0;}
  const char* w=strstr(s,"WHITE:");
  if(w){w+=6;const char* e=strchr(w,'|');size_t l=e?(size_t)(e-w):strlen(w);if(l>5)l=5;memcpy(T.ledWhite,w,l);T.ledWhite[l]=0;}
  const char* b=strstr(s,"BR:");
  if(b)T.ledBright=atoi(b+3);
  T.cob=(strcmp((const char*)T.ledWhite,"OFF")!=0);
  markTelemDirty(); updateShared();
}
void handleMegaLine(const char* line){
  if(!megaLinked){megaLinked=true;markTelemDirty();dbgPush("[PICO] Mega UART link OK");}
  lastMegaRx=millis();
  T.megaUartOk=true;
  T.wifiOk=(bool)wifiOK;
#if MEGA_USB_ECHO
  Serial.print(F("[RX] ")); Serial.println(line);
#endif
  if(strncmp(line,"STAT:",5)!=0 && strncmp(line,"US:",3)!=0 &&
     strncmp(line,"IR:",3)!=0 && strncmp(line,"STATUS|",7)!=0 &&
     strncmp(line,"PONG_PICO:",10)!=0){
    if(strncmp(line,"DBG:",4)==0) dbgPush(line+4);
    else dbgPush(line);
  }
  if(strncmp(line,"STAT:",5)==0)parseStat(line);
  else if(strncmp(line,"US:",3)==0)parseUS(line);
  else if(strncmp(line,"IR:",3)==0)parseIR(line);
  else if(strncmp(line,"STATUS|",7)==0||strncmp(line,"STATUS:",7)==0||strncmp(line,"SYSTEM|READY|",13)==0)parseStatus(line);
  else if(strncmp(line,"HDG:",4)==0)parseHDG(line);
  else if(strncmp(line,"GPS:",4)==0)parseGPS(line);
  else if(strncmp(line,"LED|",4)==0)parseLed(line);
  else if(strcmp(line,"AUTODOCK:ON")==0){T.autodock=true;requestBodyRefresh();}
  else if(strcmp(line,"AUTODOCK:OFF")==0){T.autodock=false;requestBodyRefresh();}
  else if(strncmp(line,"WIFI|",5)==0) parseWifiConnect(line);
  else if(strncmp(line,"BEEP:",5)==0) handleBeep(line);
}
// ═══ V1.1 — Serial bridge integration ═══════════════════════════════════════
//  All Mega-line reading now happens inside bridgeLoop() (serial_bridge.h).
//  The bridge invokes these two callbacks for every line it parses:
//
//    onMegaLine(line) — every line received from Mega (Serial2 / UART1)
//                      Bridge has already forwarded it to S9 over USB and
//                      stripped the |CRC:XX suffix. We just process locally.
//
//    onS9Line(line)   — every line received from S9 (USB CDC).
//                      Bridge has already forwarded non-PICO: lines to Mega.
//                      We inspect for PICO:-prefixed commands aimed at us.
//
//  handleMegaSerial() kept as an empty stub for backwards compatibility with
//  any legacy call sites; the actual work is inside bridgeLoop().
// Legacy alias — all reads/forwards now happen in bridgeLoop().
// Kept so waitMs() and other legacy call sites can still keep the pipe flowing.
void handleMegaSerial(){ bridgeLoop(); }

// Bridge callbacks — invoked by bridgeLoop() in serial_bridge.h
void onMegaLine(const String& line){
  handleMegaLine(line.c_str());
}

void onS9Line(const String& line){
  // Any USB traffic from the S9 means the phone link is alive.
  T.s9ok = true;
  lastS9Rx = millis();
  updateShared();

  if (line.startsWith("PICO:")) {
    dbgPush(line.c_str() + 5);
    // Future: PICO:BEEP:..., PICO:DISPLAY:..., PICO:AUDIO:... etc.
    return;
  }
  // Non-PICO: lines are forwarded to Mega by bridgeLoop() before this callback.
}

void waitMs(unsigned ms){
  unsigned long until=millis()+ms;
  while((long)(millis()-until)<0){handleMegaSerial();delay(2);}
}

void sendMegaHeartbeat(){
  if(millis()-lastPingTx<MEGA_PING_MS)return;
  lastPingTx=millis();
  MEGA_SERIAL.print(F("PING_PICO:"));
  MEGA_SERIAL.println(pingSeq++);
  MEGA_SERIAL.println(F("STATUS"));
}

// UI Primitives
uint16_t dimCol(uint16_t c,uint8_t shift=1){
  return (uint16_t)(((c>>11)>>shift)<<11)|(uint16_t)((((c>>5)&0x3F)>>shift)<<5)|(uint16_t)(((c&0x1F)>>shift));
}
uint16_t blendCol(uint16_t a,uint16_t b,uint8_t t){
  uint8_t r=(((a>>11)*(255-t)+(b>>11)*t)>>8)&0x1F;
  uint8_t g=(((((a>>5)&0x3F)*(255-t))+(((b>>5)&0x3F)*t))>>8)&0x3F;
  uint8_t bl=((((a&0x1F)*(255-t))+((b&0x1F)*t))>>8)&0x1F;
  return (r<<11)|(g<<5)|bl;
}
void gradientRect(int x,int y,int w,int h,uint16_t c1,uint16_t c2){
  for(int i=0;i<h;i++) tft.drawFastHLine(x,y+i,w,blendCol(c1,c2,(uint8_t)((i*255)/max(h-1,1))));
}
void glassCard(int x,int y,int w,int h,uint16_t accent){
  gradientRect(x+1,y+1,w-2,h-2,0x0C62,0x0841);
  tft.drawRoundRect(x,y,w,h,8,dimCol(accent,1));
  tft.drawFastHLine(x+4,y+1,w-8,dimCol(accent,0));
  tft.drawFastHLine(x+2,y,w-4,accent);
  tft.drawFastHLine(x+4,y+h,w-4,0x0208);
  tft.drawFastVLine(x+w,y+4,h-4,0x0208);
}
void neonBox(int x,int y,int w,int h,uint16_t col,uint16_t bg=C_SURF){
  tft.fillRoundRect(x+3,y+3,w,h,6,0x0208);
  gradientRect(x+1,y+1,w-2,h-2,blendCol(bg,col,40),bg);
  tft.drawRoundRect(x+1,y+1,w-2,h-2,5,dimCol(col,2));
  tft.drawRoundRect(x,y,w,h,6,col);
  tft.drawFastHLine(x+4,y+1,w-8,dimCol(col,1));
}
void glowText(int x,int y,const char* txt,uint16_t col,uint8_t sz=1){
  uint16_t d1=dimCol(col,2), d2=dimCol(col,1);
  tft.setTextSize(sz); tft.setTextDatum(TL_DATUM);
  tft.setTextColor(d1,C_BG); tft.setCursor(x+2,y+2); tft.print(txt);
  tft.setTextColor(d2,C_BG); tft.setCursor(x+1,y+1); tft.print(txt);
  tft.setTextColor(col,C_BG); tft.setCursor(x,y);     tft.print(txt);
}
void centreText(int cx,int y,const char* txt,uint16_t col,uint8_t sz,uint16_t bg=C_BG){
  tft.setTextSize(sz); tft.setTextColor(col,bg);
  int16_t tw=strlen(txt)*6*sz;
  tft.setCursor(cx-tw/2,y); tft.print(txt);
}
void neonBtn(int x,int y,int w,int h,const char* icon,const char* label,uint16_t col,bool pressed=false){
  uint16_t bg   = pressed ? dimCol(col,1) : 0x0A41;
  uint16_t tc   = pressed ? C_BLACK : col;
  tft.fillRoundRect(x+4,y+4,w,h,10,0x0104);
  gradientRect(x,y,w,h,blendCol(bg,col,30),bg);
  tft.drawRoundRect(x,y,w,h,10,bg);
  tft.drawRoundRect(x,y,w,h,10,col);
  tft.drawFastHLine(x+4,y+1,w-8,dimCol(col,1));
  tft.drawFastVLine(x+1,y+4,h/3,dimCol(col,2));
  tft.drawFastHLine(x+4,y+h-1,w-8,dimCol(col,3));
  tft.setTextSize(3); tft.setTextColor(tc,0x0A41);
  int16_t iw=strlen(icon)*18;
  tft.setCursor(x+(w-iw)/2, y+h/2-26); tft.print(icon);
  tft.setTextSize(2); tft.setTextColor(col,0x0A41);
  int16_t lw=strlen(label)*12;
  tft.setCursor(x+(w-lw)/2, y+h/2+8); tft.print(label);
}
void statPill(int x,int y,int w,const char* lbl,const char* val,uint16_t col){
  gradientRect(x,y,w,26,blendCol(C_SURF,col,20),C_SURF);
  tft.fillRect(x,y,3,26,col);
  tft.drawRect(x,y,w,26,dimCol(col,1));
  tft.drawFastHLine(x+3,y+1,w-4,dimCol(col,2));
  tft.setTextSize(1); tft.setTextColor(C_LGRAY,C_SURF);
  tft.setCursor(x+8,y+5); tft.print(lbl);
  if(val&&strlen(val)>0){
    tft.setTextColor(col,C_SURF);
    tft.setCursor(x+w-strlen(val)*6-4,y+9); tft.print(val);
  }
}
void usBar(int x,int y,int w,long dist,const char* lbl){
  gradientRect(x,y,w,34,0x0C62,C_SURF);
  tft.drawRect(x,y,w,34,C_BORDER);
  tft.drawFastHLine(x+1,y+1,w-2,0x18C6);
  tft.setTextSize(1); tft.setTextColor(C_LGRAY,C_SURF);
  tft.setCursor(x+4,y+4); tft.print(lbl);
  if(dist<0){ tft.setTextColor(C_DGRAY,C_SURF); tft.setCursor(x+4,y+20); tft.print("NO SIGNAL"); return; }
  long capped=min(dist,200L);
  int bw=(int)((capped*(w-8))/200);
  uint16_t col=dist<20?C_RED:dist<50?C_ORANGE:dist<100?C_YELLOW:C_GREEN;
  for(int i=0;i<bw;i+=5) tft.fillRect(x+4+i,y+18,4,10,blendCol(C_RED,C_GREEN,(uint8_t)((i*255)/max(bw,1))));
  tft.drawRect(x+4,y+18,w-8,10,dimCol(col,1));
  char buf[12]; snprintf(buf,12,"%ldcm",dist);
  tft.setTextColor(C_WHITE,C_SURF); tft.setCursor(x+w-42,y+20); tft.print(buf);
}
void drawBack(uint16_t col=C_CYAN){
  gradientRect(SCR_W-68,4,64,28,blendCol(C_SURF,col,30),C_SURF);
  tft.drawRoundRect(SCR_W-68,4,64,28,6,col);
  tft.drawFastHLine(SCR_W-64,5,56,dimCol(col,1));
  tft.setTextSize(2); tft.setTextColor(col,C_SURF);
  tft.setCursor(SCR_W-60,9); tft.print("< BCK");
}

void drawGameExitBtn(uint16_t col=C_CYAN){
  gradientRect(4,4,80,32,blendCol(C_SURF,col,30),C_SURF);
  tft.drawRoundRect(4,4,80,32,6,col);
  tft.drawFastHLine(8,5,72,dimCol(col,1));
  tft.setTextSize(2); tft.setTextColor(col,C_SURF);
  tft.setCursor(10,12); tft.print("< EXIT");
}

static Screen gameChromeScreen = (Screen)255;

void resetGameChrome() { gameChromeScreen = (Screen)255; }

void drawGameChrome(uint16_t col=C_CYAN){
  tft.fillRect(0,0,SCR_W,GAME_TOP,C_BG);
  drawGameExitBtn(col);
}

void paintGameChromeOnce(Screen id, uint16_t col){
  if (gameChromeScreen != id) {
    drawGameChrome(col);
    gameChromeScreen = id;
  }
}

void paintGameHudLine(int x, int y, const char* text, uint16_t col){
  tft.fillRect(x - 2, y - 4, SCR_W - x - 2, 16, C_BG);
  tft.setTextSize(1);
  tft.setTextColor(col, C_BG);
  tft.setCursor(x, y);
  tft.print(text);
}
void hRule(int y,uint16_t col=C_CYAN){
  tft.drawFastHLine(0,y+2,SCR_W,dimCol(col,3));
  tft.drawFastHLine(0,y+1,SCR_W,dimCol(col,1));
  tft.drawFastHLine(0,y,  SCR_W,col);
}

void drawHeader(){
  tft.fillRect(0,0,SCR_W,57,C_BG);
  gradientRect(0,0,SCR_W,56,0x1082,0x0208);
  tft.drawFastHLine(0,0,SCR_W,C_CYAN);
  tft.drawFastHLine(0,1,SCR_W,dimCol(C_CYAN,1));
  tft.drawFastHLine(0,55,SCR_W,dimCol(C_CYAN,2));
  tft.drawFastHLine(0,56,SCR_W,dimCol(C_CYAN,3));
  tft.setTextSize(2);
  uint16_t s1=dimCol(C_CYAN,3),s2=dimCol(C_CYAN,2);
  int16_t tw=13*12; int tx=SCR_W/2-tw/2;
  tft.setTextColor(s1,0x0000); tft.setCursor(tx+2,8+2); tft.print("AJ2BUDDYCOMMS");
  tft.setTextColor(s2,0x0000); tft.setCursor(tx+1,8+1); tft.print("AJ2BUDDYCOMMS");
  tft.setTextColor(C_CYAN,0x0000); tft.setCursor(tx,8);  tft.print("AJ2BUDDYCOMMS");
  uint16_t lk=megaLinked?C_GREEN:C_RED;
  tft.fillCircle(10,44,5,dimCol(lk,2));
  tft.fillCircle(10,44,3,lk);
  tft.setTextSize(1); tft.setTextColor(megaLinked?C_GREEN:C_RED,0x0000);
  tft.setCursor(18,40); tft.print(megaLinked?"LIVE":"WAIT");
  char buf[10];
  uint16_t bc=T.pct>50?C_GREEN:T.pct>20?C_ORANGE:C_RED;
  snprintf(buf,10,"%d%%",T.pct);
  tft.drawRect(54,38,32,12,C_LGRAY);
  tft.fillRect(55,39,(int)(T.pct*30/100),10,bc);
  tft.fillRect(86,41,3,6,C_LGRAY);
  tft.setTextColor(bc,0x0000); tft.setCursor(92,40); tft.print(buf);
  snprintf(buf,10,"%.1fV",T.volt);
  tft.setTextColor(C_CYAN,0x0000); tft.setCursor(128,40); tft.print(buf);
  uint16_t mc=C_PURPLE;
  int16_t mw=strlen(T.mode)*6+8;
  gradientRect(SCR_W-mw-2,34,mw,18,blendCol(C_SURF,mc,40),C_SURF);
  tft.drawRect(SCR_W-mw-2,34,mw,18,mc);
  tft.setTextColor(mc,0x0000); tft.setCursor(SCR_W-mw+2,39); tft.print(T.mode);
}

// MAIN SCREEN
void drawMain() {
  tft.fillScreen(C_BG);
  drawHeader();
  const int BW=148, BH=118, PAD=8;
  const int ROW1=60, ROW2=ROW1+BH+PAD;
  neonBtn(PAD,         ROW1, BW, BH, ">",  "GAMES",   C_GREEN);
  neonBtn(PAD+BW+PAD,  ROW1, BW, BH, "o",  "SENSORS", C_CYAN);
  neonBtn(PAD,         ROW2, BW, BH, "~",  "COMMS",   C_PURPLE);
  neonBtn(PAD+BW+PAD,  ROW2, BW, BH, "*",  "SETTINGS",C_ORANGE);
  const int LY=ROW2+BH+PAD, LH=52;
  neonBtn(PAD, LY, SCR_W-2*PAD, LH, "::", "LIGHTS", C_PINK);
  int sy=LY+LH+8;
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
  uint16_t adCol=T.autodock?C_GREEN:C_LGRAY;
  uint16_t adBg=T.autodock?0x0440:0x1082;
  tft.fillRoundRect(162,sy+2,SCR_W-170,SCR_H-sy-4,5,adBg);
  tft.drawRoundRect(162,sy+2,SCR_W-170,SCR_H-sy-4,5,adCol);
  tft.setTextSize(1); tft.setTextColor(adCol,adBg);
  tft.setCursor(168,sy+8);  tft.print("AUTO DOCK");
  tft.setTextColor(T.autodock?C_GREEN:C_RED,adBg);
  tft.setCursor(168,sy+22); tft.print(T.autodock?"ENABLED ":"DISABLED");
  tft.setTextColor(C_CYAN,adBg);
  tft.setCursor(168,sy+36); tft.print(T.dockSt);
}

void handleMainTouch(TouchPt& t) {
  const int BW=148,BH=118,PAD=8,ROW1=60,ROW2=ROW1+BH+PAD;
  const int LY=ROW2+BH+PAD, LH=52, sy=LY+LH+8;
  if(t.y>=LY && t.y<LY+LH){ sndClick(); curScreen=SCR_LIGHTS; screenDirty=true; return; }
  if(t.y>=sy){ if(t.x>162){ T.autodock=!T.autodock; MEGA_SERIAL.println(T.autodock?"AUTODOCK:ON":"AUTODOCK:OFF"); sndClick(); requestBodyRefresh(); } return; }
  if(t.x>=PAD && t.x<PAD+BW){
    if(t.y>=ROW1 && t.y<ROW1+BH){ sndClick(); curScreen=SCR_GAMES;  screenDirty=true; }
    else if(t.y>=ROW2 && t.y<ROW2+BH){ sndClick(); curScreen=SCR_COMMS; screenDirty=true; }
  } else if(t.x>=PAD+BW+PAD && t.x<PAD+BW+PAD+BW){
    if(t.y>=ROW1 && t.y<ROW1+BH){ sndClick(); curScreen=SCR_SENSORS;  screenDirty=true; }
    else if(t.y>=ROW2 && t.y<ROW2+BH){ sndClick(); curScreen=SCR_SETTINGS; screenDirty=true; }
  }
}

// GAMES MENU
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

void handleGamesTouch(TouchPt& t) {
  if(t.y<72){ curScreen=SCR_MAIN; screenDirty=true; return; }
  Screen games[]={GAME_MARIO,GAME_PACMAN,GAME_STARSHIP,GAME_MEMORY,GAME_COLORMATCH,GAME_MATH};
  for(int i=0;i<6;i++){
    int by=80+i*62;
    if(t.y>=by && t.y<by+54 && t.x>=8 && t.x<SCR_W-8){
      sndClick(); prevScreen=SCR_GAMES; curScreen=games[i]; screenDirty=true; return;
    }
  }
}

// SENSORS MENU
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

void handleSensorsTouch(TouchPt& t) {
  if(t.y<72){ curScreen=SCR_MAIN; screenDirty=true; return; }
  Screen subs[]={SCR_SENS_EYES,SCR_SENS_NOSE,SCR_SENS_BRAIN,SCR_SENS_TUMMY};
  for(int i=0;i<4;i++){
    int by=80+i*96;
    if(t.y>=by && t.y<by+86 && t.x>=8 && t.x<SCR_W-8){
      sndClick(); curScreen=subs[i]; screenDirty=true; return;
    }
  }
}

// SENSOR EYES
void drawSensEyes(bool fullLayout=true) {
  if(fullLayout){
    tft.fillScreen(C_BG);
    drawHeader();
    drawBack(C_CYAN);
    glowText(SCR_W/2-24,55,"EYES",C_CYAN,2);
    hRule(72,C_CYAN);
  }
  int rx=SCR_W/2, ry=230;
  tft.drawRoundRect(rx-30,ry-40,60,80,8,C_LGRAY);
  tft.drawRect(rx-20,ry-60,40,22,C_LGRAY);
  tft.fillCircle(rx-8,ry-50,4,C_CYAN);
  tft.fillCircle(rx+8,ry-50,4,C_CYAN);
  char buf[16];
  uint16_t fc=T.dFront<0?C_DGRAY:T.dFront<30?C_RED:T.dFront<80?C_ORANGE:C_GREEN;
  uint16_t rc=T.dRear <0?C_DGRAY:T.dRear <30?C_RED:T.dRear <80?C_ORANGE:C_GREEN;
  uint16_t lc=T.dLeft <0?C_DGRAY:T.dLeft <30?C_RED:T.dLeft <80?C_ORANGE:C_GREEN;
  uint16_t rrc=T.dRight<0?C_DGRAY:T.dRight<30?C_RED:T.dRight<80?C_ORANGE:C_GREEN;
  snprintf(buf,16,T.dFront<0?"--":"%ldcm",T.dFront);
  neonBox(rx-35,80,70,36,fc,C_SURF);
  tft.setTextSize(1); tft.setTextColor(C_LGRAY,C_SURF); tft.setCursor(rx-32,83); tft.print("FRONT");
  tft.setTextSize(2); tft.setTextColor(fc,C_SURF);
  tft.setCursor(rx-strlen(buf)*6,95); tft.print(buf);
  tft.drawFastVLine(rx,117,50,fc);
  snprintf(buf,16,T.dRear<0?"--":"%ldcm",T.dRear);
  neonBox(rx-35,334,70,36,rc,C_SURF);
  tft.setTextSize(1); tft.setTextColor(C_LGRAY,C_SURF); tft.setCursor(rx-30,337); tft.print("REAR");
  tft.setTextSize(2); tft.setTextColor(rc,C_SURF);
  tft.setCursor(rx-strlen(buf)*6,349); tft.print(buf);
  tft.drawFastVLine(rx,310,24,rc);
  snprintf(buf,16,T.dLeft<0?"--":"%ldcm",T.dLeft);
  neonBox(8,ry-18,74,36,lc,C_SURF);
  tft.setTextSize(1); tft.setTextColor(C_LGRAY,C_SURF); tft.setCursor(12,ry-15); tft.print("LEFT");
  tft.setTextSize(2); tft.setTextColor(lc,C_SURF); tft.setCursor(12,ry-3); tft.print(buf);
  tft.drawFastHLine(82,ry,rx-112,lc);
  snprintf(buf,16,T.dRight<0?"--":"%ldcm",T.dRight);
  neonBox(SCR_W-82,ry-18,74,36,rrc,C_SURF);
  tft.setTextSize(1); tft.setTextColor(C_LGRAY,C_SURF); tft.setCursor(SCR_W-78,ry-15); tft.print("RIGHT");
  tft.setTextSize(2); tft.setTextColor(rrc,C_SURF); tft.setCursor(SCR_W-78,ry-3); tft.print(buf);
  tft.drawFastHLine(rx+31,ry,SCR_W-82-rx-31,rrc);
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
  hRule(440,C_PURPLE);
  tft.setTextSize(1); tft.setTextColor(C_PURPLE,C_SURF); tft.setCursor(8,448); tft.print("CAMERA: S9 USB feed via Android app");
}

// SENSOR NOSE
void drawSensNose(bool fullLayout=true) {
  if(fullLayout){
    tft.fillScreen(C_BG);
    drawHeader();
    drawBack(C_GREEN);
    glowText(SCR_W/2-24,55,"NOSE",C_GREEN,2);
    hRule(72,C_GREEN);
  }
  int gasVal=T.gas;
  uint16_t gc= gasVal>700?C_RED:gasVal>400?C_ORANGE:gasVal>200?C_YELLOW:C_GREEN;
  tft.setTextSize(1); tft.setTextColor(C_LGRAY,C_BG); tft.setCursor(SCR_W/2-30,90); tft.print("GAS LEVEL");
  char buf[12]; snprintf(buf,12,"%d",gasVal);
  tft.setTextSize(6); tft.setTextColor(gc,C_BG);
  int16_t tw=strlen(buf)*36;
  tft.setCursor(SCR_W/2-tw/2,110); tft.print(buf);
  tft.setTextSize(2); tft.setTextColor(C_LGRAY,C_BG); tft.setCursor(SCR_W/2-12,172); tft.print("ppm");
  const char* status = gasVal>700?"DANGER!":gasVal>400?"WARNING":gasVal>200?"ELEVATED":"CLEAR";
  neonBox(SCR_W/2-60,200,120,40,gc,C_SURF);
  tft.setTextSize(2); tft.setTextColor(gc,C_SURF);
  int16_t sl=strlen(status)*12;
  tft.setCursor(SCR_W/2-sl/2,212); tft.print(status);
  hRule(258,gc);
  int bw=SCR_W-20; int filled=(int)((min(gasVal,1023)*bw)/1023);
  tft.fillRect(10,265,bw,24,C_SURF);
  tft.fillRect(10,265,filled,24,gc);
  tft.drawRect(10,265,bw,24,C_BORDER);
  tft.setTextSize(1);
  tft.setTextColor(C_GREEN,C_BG);  tft.setCursor(10,296);  tft.print("SAFE<200");
  tft.setTextColor(C_YELLOW,C_BG); tft.setCursor(90,296);  tft.print("ELEV<400");
  tft.setTextColor(C_ORANGE,C_BG); tft.setCursor(178,296); tft.print("WARN<700");
  tft.setTextColor(C_RED,C_BG);    tft.setCursor(258,296); tft.print("DANGER");
  hRule(310,C_ORANGE);
  tft.setTextSize(1); tft.setTextColor(C_LGRAY,C_BG); tft.setCursor(8,318); tft.print("ENVIRONMENT");
  uint16_t tlCol=T.tilt?C_RED:C_DGRAY;
  tft.fillRoundRect(8,328,148,44,6,C_SURF);
  tft.drawRoundRect(8,328,148,44,6,tlCol);
  tft.setTextSize(2); tft.setTextColor(tlCol,C_SURF);
  tft.setCursor(14,338); tft.print(T.tilt?"TILT!":"Level");
  uint16_t pirCol=T.pir?C_CYAN:C_DGRAY;
  tft.fillRoundRect(164,328,148,44,6,C_SURF);
  tft.drawRoundRect(164,328,148,44,6,pirCol);
  tft.setTextColor(pirCol,C_SURF);
  tft.setCursor(170,338); tft.print(T.pir?"MOTION!":"No Motion");
}

// BRAIN LOG
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
    char buf[32]; strncpy(buf, dbgLog[idx], 26); buf[26]=0;
    tft.setTextColor(C_CYAN, C_SURF); tft.setCursor(4, y+4); tft.print(buf);
    if(strlen(dbgLog[idx])>26){
      strncpy(buf, dbgLog[idx]+26, 22); buf[22]=0;
      tft.setTextColor(C_LGRAY, C_SURF); tft.setCursor(4, y+20); tft.print(buf);
    }
  }
}

// SENSOR BRAIN
void drawSensBrain(bool fullLayout=true) {
  if(fullLayout){
    tft.fillScreen(C_BG);
    drawHeader();
    drawBack(C_PURPLE);
    glowText(SCR_W/2-30,55,"BRAIN",C_PURPLE,2);
    hRule(72,C_PURPLE);
  }
  char buf[32]; int y=80;
  if(brainToggle[0]) {
    snprintf(buf,32,"TEMP   %.1f C  HUM %.0f%%",T.temp,T.hum);
    uint16_t c=T.temp>38?C_RED:T.temp>32?C_ORANGE:C_CYAN;
    statPill(8,y,SCR_W-16,buf,"",c); y+=28;
  }
  if(brainToggle[1]) {
    snprintf(buf,32,"GAS    %d ppm",T.gas);
    uint16_t c=T.gas>700?C_RED:T.gas>400?C_ORANGE:C_GREEN;
    statPill(8,y,SCR_W-16,buf,"",c); y+=28;
  }
  if(brainToggle[2]) {
    snprintf(buf,32,"VOLT   %.2fV   %.2fA   %d%%",T.volt,T.amps,T.pct);
    uint16_t c=T.volt>7.5?C_GREEN:T.volt>7.0?C_ORANGE:C_RED;
    statPill(8,y,SCR_W-16,buf,"",c); y+=28;
  }
  if(brainToggle[3]) {
    snprintf(buf,32,"US  F%ld R%ld L%ld Rg%ld",
      T.dFront<0?-1:T.dFront, T.dRear<0?-1:T.dRear,
      T.dLeft<0?-1:T.dLeft,   T.dRight<0?-1:T.dRight);
    statPill(8,y,SCR_W-16,buf,"",C_CYAN); y+=28;
  }
  if(brainToggle[4]) {
    uint16_t sc=T.estop?C_RED:C_GREEN;
    snprintf(buf,32,"MEGA:%s WIFI:%s S9:%s AUTO:%s",
      T.megaUartOk?"Y":"N", T.wifiOk?"Y":"N", T.s9ok?"Y":"N", T.autoM?"ON":"OFF");
    statPill(8,y,SCR_W-16,buf,"",sc); y+=28;
  }
  if(brainToggle[5]) {
    snprintf(buf,32,"MODE   %s   FW:%s",T.mode,T.fw);
    statPill(8,y,SCR_W-16,buf,"",C_PURPLE); y+=28;
  }
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

void handleBrainTouch(TouchPt& t) {
  if(t.y<72 && t.x<(SCR_W-80)){ curScreen=SCR_SENSORS; screenDirty=true; return; }
  if(t.y<72){ showBrainLog=!showBrainLog; screenDirty=true; return; }
  int y=80; int used=0;
  for(int i=0;i<6;i++) if(brainToggle[i]) used++;
  int firstToggleY = 80 + used*28 + 24;
  for(int i=0;i<6;i++){
    int bx=8+(i%3)*(SCR_W/3), by=firstToggleY+(i/3)*28;
    if(t.x>=bx && t.x<bx+(SCR_W/3)-4 && t.y>=by && t.y<by+22){
      brainToggle[i]=!brainToggle[i]; screenDirty=true; return;
    }
  }
}

// SENSOR TUMMY
void drawSensTummy(bool fullLayout=true) {
  if(fullLayout){
    tft.fillScreen(C_BG);
    drawHeader();
    drawBack(C_ORANGE);
    glowText(SCR_W/2-30,55,"TUMMY",C_ORANGE,2);
    hRule(72,C_ORANGE);
  }
  char buf[20];
  uint16_t vc=T.volt>7.5?C_GREEN:T.volt>7.0?C_ORANGE:C_RED;
  uint16_t ac=T.amps>3.0?C_RED:T.amps>2.0?C_ORANGE:C_GREEN;
  uint16_t pc=T.pct>50?C_GREEN:T.pct>20?C_ORANGE:C_RED;
  snprintf(buf,20,"%.2f V",T.volt);
  tft.setTextSize(1); tft.setTextColor(C_LGRAY,C_BG); tft.setCursor(SCR_W/2-24,88); tft.print("VOLTAGE");
  tft.setTextSize(5); tft.setTextColor(vc,C_BG);
  int16_t tw=strlen(buf)*30; tft.setCursor(SCR_W/2-tw/2,102); tft.print(buf);
  snprintf(buf,20,"%.2f A",T.amps);
  tft.setTextSize(1); tft.setTextColor(C_LGRAY,C_BG); tft.setCursor(SCR_W/2-22,174); tft.print("CURRENT");
  tft.setTextSize(5); tft.setTextColor(ac,C_BG);
  tw=strlen(buf)*30; tft.setCursor(SCR_W/2-tw/2,188); tft.print(buf);
  snprintf(buf,20,"%d%%",T.pct);
  tft.setTextSize(1); tft.setTextColor(C_LGRAY,C_BG); tft.setCursor(SCR_W/2-24,260); tft.print("BATTERY");
  tft.setTextSize(5); tft.setTextColor(pc,C_BG);
  tw=strlen(buf)*30; tft.setCursor(SCR_W/2-tw/2,274); tft.print(buf);
  int bw=SCR_W-40;
  tft.fillRect(20,330,bw,28,C_SURF);
  int filled=(int)(T.pct*bw/100);
  for(int x=0;x<filled;x+=4) {
    uint16_t col=x<filled/3?C_RED:x<2*filled/3?C_ORANGE:C_GREEN;
    tft.fillRect(20+x,330,3,28,col);
  }
  tft.drawRect(20,330,bw,28,C_BORDER);
  float watts=T.volt*T.amps;
  snprintf(buf,20,"%.1f W",watts);
  hRule(374,C_ORANGE);
  tft.setTextSize(1); tft.setTextColor(C_LGRAY,C_BG); tft.setCursor(8,382); tft.print("POWER DRAW:");
  tft.setTextSize(3); tft.setTextColor(C_ORANGE,C_BG);
  tw=strlen(buf)*18; tft.setCursor(SCR_W-tw-8,378); tft.print(buf);
  float mAh=2000.0f;
  float runtimeH = (T.amps>0.1) ? (mAh/1000.0f)*(T.pct/100.0f)/T.amps : 0;
  int rMin=(int)(runtimeH*60);
  snprintf(buf,20,"%dm left",rMin);
  tft.setTextSize(1); tft.setTextColor(C_LGRAY,C_BG); tft.setCursor(8,420); tft.print("EST RUNTIME:");
  tft.setTextSize(2); tft.setTextColor(pc,C_BG);
  tw=strlen(buf)*12; tft.setCursor(SCR_W-tw-8,416); tft.print(buf);
}

void refreshMainBody(){
  const int ROW1=60,ROW2=ROW1+118+8,LY=ROW2+118+8,LH=52,sy=LY+LH+8;
  tft.fillRect(0,sy,SCR_W,SCR_H-sy,C_SURF);
  hRule(sy,C_CYAN);
  char buf[16];
  tft.setTextSize(1);
  snprintf(buf,16,T.dFront<0?"--":"%ldcm",T.dFront);
  tft.setTextColor(T.dFront<0?C_DGRAY:T.dFront<30?C_RED:C_CYAN,C_SURF);
  tft.setCursor(6,sy+8); tft.print("F:"); tft.print(buf);
  snprintf(buf,16,T.dRear<0?"--":"%ldcm",T.dRear);
  tft.setTextColor(T.dRear<0?C_DGRAY:T.dRear<30?C_RED:C_CYAN,C_SURF);
  tft.setCursor(86,sy+8); tft.print("R:"); tft.print(buf);
  snprintf(buf,16,T.dLeft<0?"--":"%ldcm",T.dLeft);
  tft.setTextColor(T.dLeft<0?C_DGRAY:T.dLeft<30?C_RED:C_CYAN,C_SURF);
  tft.setCursor(166,sy+8); tft.print("L:"); tft.print(buf);
  uint16_t adCol=T.autodock?C_GREEN:C_LGRAY, adBg=T.autodock?0x0440:0x1082;
  tft.fillRoundRect(162,sy+2,SCR_W-170,SCR_H-sy-4,5,adBg);
  tft.drawRoundRect(162,sy+2,SCR_W-170,SCR_H-sy-4,5,adCol);
  tft.setTextSize(1); tft.setTextColor(adCol,adBg);
  tft.setCursor(168,sy+8); tft.print("AUTO DOCK");
  tft.setTextColor(T.autodock?C_GREEN:C_RED,adBg);
  tft.setCursor(168,sy+22); tft.print(T.autodock?"ENABLED ":"DISABLED");
  tft.setTextColor(C_CYAN,adBg);
  tft.setCursor(168,sy+36); tft.print(T.dockSt);
}

void refreshCommsBody(){
  int y=84;
  const char* labels[]={"MEGA UART","MEGA MOTORS","WIFI (PICO W)","S9 ANDROID"};
  bool states[]={megaLinked, megaLinked && !T.estop, T.wifiOk, T.s9ok};
  uint16_t cols[]={C_CYAN,C_GREEN,C_ORANGE,C_PURPLE};
  for(int i=0;i<4;i++){
    bool ok=states[i]; uint16_t c=ok?cols[i]:C_DGRAY;
    tft.fillRect(8,y,SCR_W-16,76,C_SURF);
    neonBox(8,y,SCR_W-16,76,c,C_SURF);
    tft.fillCircle(24,y+22,8,ok?c:C_DGRAY);
    tft.setTextSize(2); tft.setTextColor(c,C_SURF);
    tft.setCursor(40,y+14); tft.print(labels[i]);
    tft.setTextSize(1); tft.setTextColor(ok?C_WHITE:C_DGRAY,C_SURF);
    tft.setCursor(40,y+38); tft.print(ok?"CONNECTED - ONLINE":"NOT DETECTED");
    y+=84;
  }
  tft.fillRect(0,y+4,SCR_W,30,C_BG);
  hRule(y+4,C_PURPLE);
  char buf[40];
  unsigned long since=(millis()-lastMegaRx)/1000;
  snprintf(buf,40,"Last Mega RX: %lus ago",since);
  tft.setTextSize(1); tft.setTextColor(since>10?C_RED:C_GREEN,C_BG);
  tft.setCursor(8,y+12); tft.print(buf);
}

void refreshScreenBody(){
  switch(curScreen){
    case SCR_MAIN:        refreshMainBody(); break;
    case SCR_COMMS:       refreshCommsBody(); break;
    case SCR_SENS_EYES:
    case SCR_SENS_NOSE:
    case SCR_SENS_BRAIN:
    case SCR_SENS_TUMMY:
      tft.fillRect(0,72,SCR_W,SCR_H-72,C_BG);
      if(curScreen==SCR_SENS_EYES) drawSensEyes(false);
      else if(curScreen==SCR_SENS_NOSE) drawSensNose(false);
      else if(curScreen==SCR_SENS_BRAIN){ if(showBrainLog) drawBrainLog(); else drawSensBrain(false); }
      else drawSensTummy(false);
      break;
    default: break;
  }
}

// COMMS SCREEN
void drawComms() {
  tft.fillScreen(C_BG);
  drawHeader();
  drawBack(C_PURPLE);
  glowText(SCR_W/2-30,55,"COMMS",C_PURPLE,2);
  hRule(72,C_PURPLE);
  char buf[40]; int y=84;
  const char* labels[]={"MEGA UART","MEGA MOTORS","WIFI (PICO W)","S9 ANDROID"};
  bool motorOk = megaLinked && !T.estop;
  bool states[]={megaLinked, motorOk, T.wifiOk, T.s9ok};
  uint16_t cols[]={C_CYAN,C_GREEN,C_ORANGE,C_PURPLE};
  for(int i=0;i<4;i++){
    bool ok=states[i];
    uint16_t c=ok?cols[i]:C_DGRAY;
    neonBox(8,y,SCR_W-16,76,c,C_SURF);
    tft.fillCircle(24,y+22,8,ok?c:C_DGRAY);
    tft.setTextSize(2); tft.setTextColor(c,C_SURF);
    tft.setCursor(40,y+14); tft.print(labels[i]);
    tft.setTextSize(1); tft.setTextColor(ok?C_WHITE:C_DGRAY,C_SURF);
    tft.setCursor(40,y+38); tft.print(ok?"CONNECTED - ONLINE":"NOT DETECTED");
    y+=84;
  }
  hRule(y+4,C_PURPLE);
  unsigned long since=(millis()-lastMegaRx)/1000;
  snprintf(buf,40,"Last Mega RX: %lus ago",since);
  tft.setTextSize(1); tft.setTextColor(since>10?C_RED:C_GREEN,C_BG);
  tft.setCursor(8,y+12); tft.print(buf);
}

// SETTINGS SCREEN
void drawSettings() {
  tft.fillScreen(C_BG);
  drawHeader();
  drawBack(C_ORANGE);
  glowText(SCR_W/2-36,55,"SETTINGS",C_ORANGE,2);
  hRule(72,C_ORANGE);
  tft.setTextSize(1); tft.setTextColor(C_LGRAY,C_BG);
  tft.setCursor(8,84); tft.print("BuddyBot PicoW Dash v1.1");
  tft.setCursor(8,98); tft.print("Mega UART: GP4/GP5 @ 115200 (UART1)");
  tft.setCursor(8,112); tft.print("Touch: FT6336U I2C @ 400kHz");
  tft.setCursor(8,126); tft.print("Display: ST7796S SPI 320x480");
  const char* cmds[]={"PING MEGA","REQ STATUS","REQ SENSOR","ESTOP"};
  uint16_t cc[]={C_CYAN,C_GREEN,C_ORANGE,C_RED};
  for(int i=0;i<4;i++){
    neonBox(8,150+i*72,SCR_W-16,60,cc[i],C_SURF);
    tft.setTextSize(2); tft.setTextColor(cc[i],C_SURF);
    int16_t tw=strlen(cmds[i])*12;
    tft.setCursor(8+(SCR_W-16-tw)/2,168+i*72); tft.print(cmds[i]);
  }
}

void drainMegaSerial(unsigned ms=300){
  unsigned long until=millis()+ms;
  while((long)(millis()-until)<0) handleMegaSerial();
}

void handleSettingsTouch(TouchPt& t) {
  if(t.y<72){ curScreen=SCR_MAIN; screenDirty=true; return; }
  const char* megaCmds[]={"PING","STATUS","SENSOR_STATUS","ESTOP"};
  for(int i=0;i<4;i++){
    if(t.y>=150+i*72 && t.y<210+i*72 && t.x>=8 && t.x<SCR_W-8){
      MEGA_SERIAL.println(megaCmds[i]); sndAlert();
      neonBox(8,150+i*72,SCR_W-16,60,C_WHITE,C_WHITE);
      drainMegaSerial(350);
      requestHeaderRefresh();
      requestBodyRefresh();
      drawHeader();
      if(i==1 || i==2) refreshScreenBody();
    }
  }
}

// LIGHTS CONTROL SCREEN
void sendLed(const char* a){ MEGA_SERIAL.print("LED:"); MEGA_SERIAL.println(a); }
bool isLedMode(const char* m){ return strcmp((const char*)T.ledMode,m)==0; }

void litCell(int x,int y,int w,int h,const char* label,uint16_t col,bool active){
  uint16_t bg = active ? col : C_SURF;
  neonBox(x,y,w,h,col,bg);
  tft.setTextSize(2);
  tft.setTextColor(active?C_BLACK:col,bg);
  int16_t tw=strlen(label)*12;
  tft.setCursor(x+(w-tw)/2, y+(h-16)/2);
  tft.print(label);
}

#define LIT_EFW 97
void drawLights(){
  tft.fillScreen(C_BG);
  drawHeader();
  drawBack(C_PINK);
  glowText(SCR_W/2-36,55,"LIGHTS",C_PINK,2);
  hRule(72,C_PINK);

  tft.setTextSize(1); tft.setTextColor(C_LGRAY,C_BG);
  tft.setCursor(8,78); tft.print("EFFECTS");
  const char* en[]={"OFF","POLICE","ALERT","RAINBOW","BREATHE","PARTY"};
  uint16_t ec[]={C_LGRAY,C_RED,C_ORANGE,C_CYAN,C_GREEN,C_PURPLE};
  for(int i=0;i<6;i++){
    int cx=i%3, rw=i/3;
    int x=8+cx*(LIT_EFW+6), y=92+rw*50;
    litCell(x,y,LIT_EFW,44,en[i],ec[i],isLedMode(en[i]));
  }

  tft.setTextSize(1); tft.setTextColor(C_LGRAY,C_BG);
  tft.setCursor(8,196); tft.print("COLORS");
  uint16_t sw[]={C_RED,C_GREEN,C_BLUE,C_CYAN,C_PURPLE,C_ORANGE,C_YELLOW};
  for(int i=0;i<7;i++){
    int x=8+i*44, y=208;
    tft.fillRoundRect(x,y,40,40,6,sw[i]);
    tft.drawRoundRect(x,y,40,40,6,C_WHITE);
  }

  tft.setTextSize(1); tft.setTextColor(C_LGRAY,C_BG);
  tft.setCursor(8,256); tft.print("WHITE INTERIOR");
  char wl[20]; snprintf(wl,20,"WHITE:%s",(const char*)T.ledWhite);
  bool won=(strcmp((const char*)T.ledWhite,"OFF")!=0);
  litCell(8,268,190,42,wl,C_WHITE,won);
  litCell(206,268,106,42,"W-SOLO",C_YELLOW,false);

  tft.setTextSize(1); tft.setTextColor(C_LGRAY,C_BG);
  tft.setCursor(8,318); tft.print("BRIGHTNESS");
  litCell(8,330,70,42,"DIM",C_CYAN,false);
  litCell(242,330,70,42,"BRT",C_CYAN,false);
  int bw=156, bx=86, by=330;
  tft.drawRoundRect(bx,by,bw,42,6,C_CYAN);
  int fill=(int)((long)(bw-4)*T.ledBright/255);
  tft.fillRect(bx+2,by+2,fill,38,dimCol(C_CYAN,1));
  char bb[8]; snprintf(bb,8,"%d",T.ledBright);
  tft.setTextSize(2); tft.setTextColor(C_WHITE,C_BG);
  int16_t tw=strlen(bb)*12; tft.setCursor(bx+(bw-tw)/2,by+13); tft.print(bb);

  hRule(380,C_PINK);
  char mb[44]; snprintf(mb,44,"MODE:%s  W:%s  BR:%d",(const char*)T.ledMode,(const char*)T.ledWhite,T.ledBright);
  tft.setTextSize(1); tft.setTextColor(C_PINK,C_BG);
  tft.setCursor(8,390); tft.print(mb);
}

void handleLightsTouch(TouchPt& t){
  if(t.y<72){ curScreen=SCR_MAIN; screenDirty=true; return; }
  const char* en[]={"OFF","POLICE","ALERT","RAINBOW","BREATHE","PARTY"};
  for(int i=0;i<6;i++){
    int cx=i%3, rw=i/3;
    int x=8+cx*(LIT_EFW+6), y=92+rw*50;
    if(t.x>=x&&t.x<x+LIT_EFW&&t.y>=y&&t.y<y+44){ sndClick(); sendLed(en[i]); screenDirty=true; return; }
  }
  const char* cn[]={"RED","GREEN","BLUE","CYAN","PURPLE","ORANGE","YELLOW"};
  for(int i=0;i<7;i++){
    int x=8+i*44, y=208;
    if(t.x>=x&&t.x<x+40&&t.y>=y&&t.y<y+40){ sndClick(); sendLed(cn[i]); screenDirty=true; return; }
  }
  if(t.y>=268&&t.y<310){
    if(t.x>=8&&t.x<198){
      const char* nx;
      if(strcmp((const char*)T.ledWhite,"OFF")==0) nx="WHITE:ON";
      else if(strcmp((const char*)T.ledWhite,"ON")==0) nx="WHITE:AUTO";
      else nx="WHITE:OFF";
      sndClick(); sendLed(nx); screenDirty=true; return;
    }
    if(t.x>=206&&t.x<312){ sndClick(); sendLed("WHITE:SOLO"); screenDirty=true; return; }
  }
  if(t.y>=330&&t.y<372){
    char bb[16];
    if(t.x>=8&&t.x<78){ int nb=constrain(T.ledBright-32,16,255); snprintf(bb,16,"BRIGHT:%d",nb); sndClick(); sendLed(bb); screenDirty=true; return; }
    if(t.x>=242&&t.x<312){ int nb=constrain(T.ledBright+32,16,255); snprintf(bb,16,"BRIGHT:%d",nb); sndClick(); sendLed(bb); screenDirty=true; return; }
  }
}

// GAME: SUPER MARIO
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
  int    prevScore=-1, prevLives=-1;
  float  camX=0;
  bool   running=false, gameOver=false;
  unsigned long lastFrame=0;
  Platform plats[NUM_PLAT]={{0,GROUND_Y,3200},{200,300,120},{400,240,100},{650,280,140},{900,200,160}};
  Coin     coins[NUM_COIN]={{220,268,true},{260,268,true},{420,208,true},{670,248,true},{920,168,true},{1000,168,true}};
  Enemy    enemies[NUM_ENEMY]={{400,GROUND_Y-16,1.0f,true},{700,GROUND_Y-16,-1.0f,true},{960,220-16,1.0f,true}};
} M;

void marioReset() {
  M.px=60; M.py=GROUND_Y-24; M.pvx=0; M.pvy=0;
  M.onGround=false; M.score=0; M.lives=3; M.prevScore=-1; M.prevLives=-1; M.camX=0; M.gameOver=false;
  for(auto& c:M.coins) c.alive=true;
  for(auto& e:M.enemies){ e.alive=true; }
  M.enemies[0].x=400; M.enemies[1].x=700; M.enemies[2].x=960;
}

void drawMario(int sx,int sy,bool flip) {
  tft.fillRect(sx+2,sy,12,4,C_MRED);
  tft.fillRect(sx,sy+4,16,5,C_MSKIN);
  tft.fillRect(sx+(flip?3:9),sy+5,3,3,C_BLACK);
  tft.fillRect(sx+2,sy+9,12,7,C_MRED);
  tft.fillRect(sx,sy+9,4,5,C_MBLUE);
  tft.fillRect(sx+12,sy+9,4,5,C_MBLUE);
  tft.fillRect(sx+2,sy+14,12,5,C_MBLUE);
  int wa=(millis()/120)%2;
  tft.fillRect(sx+(wa?0:6),sy+19,6,4,C_MBROWN);
  tft.fillRect(sx+(wa?10:4),sy+19,6,4,C_MBROWN);
}

void drawMarioGame() {
  tft.startWrite();
  tft.fillRect(0,GAME_TOP,SCR_W,SCR_H-GAME_TOP,C_MSKY);
  if(M.gameOver) {
    glowText(SCR_W/2-48,200,"GAME OVER",C_RED,3);
    char buf[20]; snprintf(buf,20,"Score: %d",M.score);
    centreText(SCR_W/2,260,buf,C_WHITE,2,C_MSKY);
    centreText(SCR_W/2,300,"Tap to restart",C_YELLOW,2,C_MSKY);
    paintGameChromeOnce(GAME_MARIO, C_GREEN);
    tft.endWrite();
    return;
  }
  int camX=(int)M.camX;
  tft.fillRect(0,GROUND_Y,SCR_W,SCR_H-GROUND_Y,C_MGRND);
  tft.fillRect(0,GROUND_Y,SCR_W,8,0x0300);
  for(auto& p:M.plats){
    int sx=p.x-camX, sw=p.w;
    if(sx+sw<0||sx>SCR_W) continue;
    tft.fillRect(sx,p.y,sw,14,C_MPIPE);
    tft.fillRect(sx,p.y,sw,4,0x0700);
  }
  for(auto& c:M.coins){
    if(!c.alive) continue;
    int sx=c.x-camX;
    if(sx<0||sx>SCR_W) continue;
    tft.fillCircle(sx,c.y,6,C_MYELL);
    tft.fillCircle(sx,c.y,3,C_ORANGE);
  }
  for(auto& e:M.enemies){
    if(!e.alive) continue;
    int sx=(int)e.x-camX;
    if(sx<-20||sx>SCR_W+20) continue;
    tft.fillRect(sx,  (int)e.y,16,6,C_MBROWN);
    tft.fillRect(sx+2,(int)e.y+6,12,8,C_MBROWN);
    tft.fillRect(sx+1,(int)e.y+2,4,4,C_BLACK);
    tft.fillRect(sx+11,(int)e.y+2,4,4,C_BLACK);
  }
  int msx=(int)(M.px-camX);
  drawMario(msx,(int)M.py, M.pvx<0);
  paintGameChromeOnce(GAME_MARIO, C_GREEN);
  if (M.score != M.prevScore || M.lives != M.prevLives) {
    char buf[32]; snprintf(buf,32,"SC:%05d LV:%d",M.score,M.lives);
    paintGameHudLine(90, 12, buf, C_WHITE);
    M.prevScore = M.score; M.prevLives = M.lives;
  }
  tft.endWrite();
}

void updateMario() {
  unsigned long now=millis();
  if(now-M.lastFrame<33) return;
  M.lastFrame=now;
  TouchPt t; bool tpressed=false;
  if(touchReady()){ t=readTouch(); tpressed=t.pressed; if(tpressed) lastTouchMs=millis(); }
  if (tpressed && handleGameBack(t)) return;
  if(tpressed) {
    if(t.y>GAME_TOP) {
      if(t.x<SCR_W/3) M.pvx=-MARIO_SPD;
      else if(t.x>2*SCR_W/3) M.pvx=MARIO_SPD;
      else if(M.onGround) { M.pvy=MARIO_JUMP; M.onGround=false; sndJump(); }
    }
  } else {
    M.pvx*=0.8f;
  }
  M.pvy+=MARIO_GRAV;
  M.py+=M.pvy; M.px+=M.pvx;
  if(M.px<0) M.px=0;
  M.onGround=false;
  for(auto& p:M.plats){
    if(M.px+14>p.x && M.px<p.x+p.w && M.py+24>=p.y && M.py+24<=p.y+16 && M.pvy>=0){
      M.py=p.y-24; M.pvy=0; M.onGround=true;
    }
  }
  for(auto& c:M.coins){
    if(!c.alive) continue;
    if(abs((int)M.px+8-c.x)<12 && abs((int)M.py+12-c.y)<12){ c.alive=false; M.score+=100; sndCoin(); }
  }
  for(auto& e:M.enemies){
    if(!e.alive) continue;
    e.x+=e.vx;
    if(e.x<0||e.x>1200) e.vx*=-1;
    if(abs((int)M.px+8-(int)e.x+8)<14 && abs((int)M.py+24-(int)e.y)<14){
      if(M.pvy>0){ e.alive=false; M.score+=200; M.pvy=-6; sndStomp(); }
      else { M.lives--; M.px=60; M.py=GROUND_Y-24; M.pvy=0; M.camX=0; sndDeath(); if(M.lives<=0){ M.gameOver=true; sndGameOver(); } }
    }
  }
  M.camX=M.px-SCR_W/3;
  if(M.camX<0) M.camX=0;
  if(M.py>SCR_H+40){ M.lives--; M.px=60; M.py=GROUND_Y-24; M.pvy=0; M.camX=0; sndDeath(); if(M.lives<=0){ M.gameOver=true; sndGameOver(); } }
  drawMarioGame();
}

// GAME: PACMAN
#define PM_COLS  16
#define PM_ROWS  20
#define PM_CELL  14
#define PM_OX    ((SCR_W-PM_COLS*PM_CELL)/2)
#define PM_OY    (GAME_TOP+10)
#define PM_FRAME_MS  150
#define PM_SPEED     0.35f
#define PM_SNAP      0.20f
#define PM_TURN_TOL  0.38f
#define PM_GHOST_SPD 0.22f
#define PM_BTN_SZ    46
#define PM_BTN_GAP   10
#define PM_BTN_CY    (PM_OY + PM_ROWS*PM_CELL + 62)
#define PM_BTN_CX    (SCR_W/2)
#define PM_SPR_PAD   16

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
  float px,py,pvx,pvy;
  float prevPx,prevPy;
  int   dx,dy;
  int   score; int lives; int dots;
  int   prevScore,prevLives;
  bool  gameOver, win;
  float gx[2],gy[2];
  float prevGx[2],prevGy[2];
  int gdx[2],gdy[2];
  uint16_t gcol[2];
  unsigned long lastFrame;
  bool powered; unsigned long powerEnd;
  bool mazeDrawn;
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
  PM.prevPx=PM.px; PM.prevPy=PM.py;
  PM.prevGx[0]=PM.gx[0]; PM.prevGy[0]=PM.gy[0];
  PM.prevGx[1]=PM.gx[1]; PM.prevGy[1]=PM.gy[1];
  PM.prevScore=-1; PM.prevLives=-1;
  PM.mazeDrawn=false;
}

void pacDrawCell(int c,int r){
  int x=PM_OX+c*PM_CELL, y=PM_OY+r*PM_CELL;
  uint8_t cell=PM.maze[r][c];
  if(cell==1){ tft.fillRect(x,y,PM_CELL,PM_CELL,0x000D); tft.drawRect(x,y,PM_CELL,PM_CELL,C_BLUE); }
  else if(cell==0) tft.fillCircle(x+PM_CELL/2,y+PM_CELL/2,2,C_YELLOW);
  else if(cell==3) tft.fillCircle(x+PM_CELL/2,y+PM_CELL/2,5,C_WHITE);
  else tft.fillRect(x,y,PM_CELL,PM_CELL,C_BLACK);
}

void pacToPix(float gx,float gy,int& px,int& py){
  px=PM_OX+(int)(gx*PM_CELL)+PM_CELL/2;
  py=PM_OY+(int)(gy*PM_CELL)+PM_CELL/2;
}

void pacRestoreMazeRect(int x0,int y0,int x1,int y1){
  int mazeBot=PM_OY+PM_ROWS*PM_CELL;
  if(y0<PM_OY) y0=PM_OY;
  if(y1>mazeBot) y1=mazeBot;
  if(x0<PM_OX) x0=PM_OX;
  if(x1>PM_OX+PM_COLS*PM_CELL) x1=PM_OX+PM_COLS*PM_CELL;
  int c0=max(0,(x0-PM_OX)/PM_CELL), c1=min(PM_COLS-1,(x1-PM_OX-1)/PM_CELL);
  int r0=max(0,(y0-PM_OY)/PM_CELL), r1=min(PM_ROWS-1,(y1-PM_OY-1)/PM_CELL);
  for(int r=r0;r<=r1;r++) for(int c=c0;c<=c1;c++) pacDrawCell(c,r);
}

void pacEraseSprite(float gx,float gy){
  int px,py; pacToPix(gx,gy,px,py);
  int x0=px-PM_SPR_PAD, y0=py-PM_SPR_PAD, w=PM_SPR_PAD*2, h=PM_SPR_PAD*2;
  tft.fillRect(x0,y0,w,h,C_BLACK);
  pacRestoreMazeRect(x0,y0,x0+w,y0+h);
}

void pacDrawPacSprite(){
  int pac_px=PM_OX+(int)(PM.px*PM_CELL)+PM_CELL/2;
  int pac_py=PM_OY+(int)(PM.py*PM_CELL)+PM_CELL/2;
  int mouth=(millis()/100)%2?30:5;
  tft.fillCircle(pac_px,pac_py,6,C_YELLOW);
  if(mouth>10) tft.fillTriangle(pac_px,pac_py,pac_px+8*(PM.dx?PM.dx:1),pac_py-4,pac_px+8*(PM.dx?PM.dx:1),pac_py+4,C_BLACK);
}

void pacDrawGhostSprite(int i){
  int gx=PM_OX+(int)(PM.gx[i]*PM_CELL)+PM_CELL/2;
  int gy=PM_OY+(int)(PM.gy[i]*PM_CELL)+PM_CELL/2;
  uint16_t gc=PM.powered?C_BLUE:PM.gcol[i];
  tft.fillCircle(gx,gy,6,gc);
  tft.fillRect(gx-6,gy,12,6,gc);
}

void pacGetBtnRect(int dir,int& bx,int& by,int& bw,int& bh){
  int cx=PM_BTN_CX, cy=PM_BTN_CY, b=PM_BTN_SZ, g=PM_BTN_GAP;
  bw=bh=b;
  if(dir==0){ bx=cx-b/2; by=cy-b-g; }
  else if(dir==1){ bx=cx-b/2; by=cy+g; }
  else if(dir==2){ bx=cx-b-g; by=cy-b/2; }
  else { bx=cx+g; by=cy-b/2; }
}

void pacDrawButtons(){
  const char* labels[]={"^","v","<",">"};
  for(int i=0;i<4;i++){
    int bx,by,bw,bh; pacGetBtnRect(i,bx,by,bw,bh);
    neonBox(bx,by,bw,bh,C_YELLOW,C_SURF2);
    tft.setTextSize(2); tft.setTextColor(C_YELLOW,C_SURF2);
    int16_t tw=strlen(labels[i])*12;
    tft.setCursor(bx+(bw-tw)/2, by+(bh-16)/2); tft.print(labels[i]);
  }
}

bool pacBtnHit(const TouchPt& t,int bx,int by,int bw,int bh){
  return t.x>=bx && t.x<bx+bw && t.y>=by && t.y<by+bh;
}

bool pacHandleDirTouch(const TouchPt& t){
  if(t.y<GAME_TOP) return false;
  static const int dirs[4][2]={{0,-1},{0,1},{-1,0},{1,0}};
  for(int i=0;i<4;i++){
    int bx,by,bw,bh; pacGetBtnRect(i,bx,by,bw,bh);
    if(pacBtnHit(t,bx,by,bw,bh)){
      PM.dx=dirs[i][0]; PM.dy=dirs[i][1]; sndClick(); return true;
    }
  }
  return false;
}

bool pacCellFree(int c,int r){
  return c>=0 && c<PM_COLS && r>=0 && r<PM_ROWS && PM.maze[r][c]!=1;
}

int pacCol(){ return (int)(PM.px+0.5f); }
int pacRow(){ return (int)(PM.py+0.5f); }

bool pacAlignedH(){
  int rr=pacRow();
  return abs(PM.py-rr) < PM_TURN_TOL;
}

bool pacAlignedV(){
  int cc=pacCol();
  return abs(PM.px-cc) < PM_TURN_TOL;
}

void pacSnapTunnel(){
  int cc=pacCol(), rr=pacRow();
  if(abs(PM.px-cc) < PM_SNAP) PM.px=cc;
  if(abs(PM.py-rr) < PM_SNAP) PM.py=rr;
}

void pacUpdateVelocity(){
  int cc=pacCol(), rr=pacRow();
  PM.pvx=0; PM.pvy=0;
  if(PM.dx!=0 && pacCellFree(cc+PM.dx,rr) && pacAlignedH()){
    PM.pvx=PM.dx*PM_SPEED;
    return;
  }
  if(PM.dy!=0 && pacCellFree(cc,rr+PM.dy) && pacAlignedV()){
    PM.pvy=PM.dy*PM_SPEED;
    return;
  }
  if(PM.dx!=0 && pacCellFree(cc+PM.dx,rr))
    PM.pvx=PM.dx*PM_SPEED;
  else if(PM.dy!=0 && pacCellFree(cc,rr+PM.dy))
    PM.pvy=PM.dy*PM_SPEED;
}

bool pacTryMove(){
  bool moved=false;
  if(PM.pvx!=0.0f){
    float nx=PM.px+PM.pvx;
    int tc=(int)(nx+0.5f), tr=pacRow();
    if(pacCellFree(tc,tr)){ PM.px=nx; moved=true; }
    else { PM.px=pacCol(); PM.pvx=0; }
  }
  if(PM.pvy!=0.0f){
    float ny=PM.py+PM.pvy;
    int tc=pacCol(), tr=(int)(ny+0.5f);
    if(pacCellFree(tc,tr)){ PM.py=ny; moved=true; }
    else { PM.py=pacRow(); PM.pvy=0; }
  }
  return moved;
}

void pacDrawMaze(){
  tft.fillRect(0,GAME_TOP,SCR_W,SCR_H-GAME_TOP,C_BLACK);
  for(int r=0;r<PM_ROWS;r++) for(int c=0;c<PM_COLS;c++) pacDrawCell(c,r);
  pacDrawButtons();
  PM.mazeDrawn=true;
}

void pacDrawHud(){
  paintGameChromeOnce(GAME_PACMAN, C_YELLOW);
  if(PM.score!=PM.prevScore||PM.lives!=PM.prevLives){
    char buf[24]; snprintf(buf,24,"SC:%d LV:%d",PM.score,PM.lives);
    paintGameHudLine(90,12,buf,C_YELLOW);
    PM.prevScore=PM.score; PM.prevLives=PM.lives;
  }
}

void pacDrawFrame(){
  tft.startWrite();
  if(!PM.mazeDrawn) pacDrawMaze();
  pacEraseSprite(PM.prevPx,PM.prevPy);
  for(int i=0;i<2;i++) pacEraseSprite(PM.prevGx[i],PM.prevGy[i]);
  pacDrawPacSprite();
  for(int i=0;i<2;i++) pacDrawGhostSprite(i);
  PM.prevPx=PM.px; PM.prevPy=PM.py;
  for(int i=0;i<2;i++){ PM.prevGx[i]=PM.gx[i]; PM.prevGy[i]=PM.gy[i]; }
  pacDrawHud();
  tft.endWrite();
}

void drawPacGame(){
  tft.startWrite();
  pacDrawMaze();
  pacDrawPacSprite();
  for(int i=0;i<2;i++) pacDrawGhostSprite(i);
  PM.prevPx=PM.px; PM.prevPy=PM.py;
  for(int i=0;i<2;i++){ PM.prevGx[i]=PM.gx[i]; PM.prevGy[i]=PM.gy[i]; }
  pacDrawHud();
  if(PM.gameOver){ centreText(SCR_W/2,200,"GAME OVER!",C_RED,3,C_BLACK); centreText(SCR_W/2,240,"Tap restart",C_WHITE,2,C_BLACK); }
  if(PM.win)     { centreText(SCR_W/2,200,"YOU WIN!",C_GREEN,3,C_BLACK); }
  tft.endWrite();
}

void updatePacman(){
  unsigned long now=millis();
  if(now-PM.lastFrame<PM_FRAME_MS) return; PM.lastFrame=now;
  if(PM.gameOver||PM.win) { if(touchReady()){readTouch();lastTouchMs=millis();pacReset();drawPacGame();} return; }
  if(touchReady()){
    TouchPt t=readTouch(); lastTouchMs=millis();
    if(t.pressed && handleGameBack(t)) return;
    if(t.pressed) pacHandleDirTouch(t);
  }
  pacSnapTunnel();
  pacUpdateVelocity();
  pacTryMove();
  int cr=pacRow(), cc=pacCol();
  if(cr>=0&&cr<PM_ROWS&&cc>=0&&cc<PM_COLS){
    if(PM.maze[cr][cc]==0){ PM.maze[cr][cc]=2; PM.score+=10; PM.dots--; sndDot(); if(PM.dots<=0) PM.win=true; }
    if(PM.maze[cr][cc]==3){ PM.maze[cr][cc]=2; PM.score+=50; PM.powered=true; sndPower(); PM.powerEnd=now+6000; }
  }
  if(PM.powered&&now>PM.powerEnd) PM.powered=false;
  for(int i=0;i<2;i++){
    int gr=(int)(PM.gy[i]+0.5f), gc2=(int)(PM.gx[i]+0.5f);
    int nr2=gr+PM.gdy[i], nc2=gc2+PM.gdx[i];
    if(nr2<0||nr2>=PM_ROWS||nc2<0||nc2>=PM_COLS||PM.maze[nr2][nc2]==1||random(14)==0){
      int dirs[4][2]={{1,0},{-1,0},{0,1},{0,-1}};
      int tries=0;
      do{ int d=random(4); PM.gdx[i]=dirs[d][0]; PM.gdy[i]=dirs[d][1]; tries++; }
      while(tries<8&&(PM.maze[gr+PM.gdy[i]][gc2+PM.gdx[i]]==1));
    }
    PM.gx[i]+=PM.gdx[i]*PM_GHOST_SPD; PM.gy[i]+=PM.gdy[i]*PM_GHOST_SPD;
    if(abs(PM.gx[i]-PM.px)<1.2f&&abs(PM.gy[i]-PM.py)<1.2f){
      if(PM.powered){ PM.gx[i]=7; PM.gy[i]=8; PM.score+=200; sndHit(); }
      else{ PM.lives--; PM.px=1; PM.py=1; PM.pvx=0; PM.pvy=0; sndDeath(); if(PM.lives<=0){ PM.gameOver=true; sndGameOver(); } }
    }
  }
  if(PM.gameOver||PM.win) drawPacGame();
  else pacDrawFrame();
}

// GAME: STARSHIP
#define SS_MAX_BULLETS 10
#define SS_MAX_BUGS    15

struct StarShip {
  float sx, prevSx;
  SSBullet bullets[SS_MAX_BULLETS];
  SSBullet prevBullets[SS_MAX_BULLETS];
  SSBug    bugs[SS_MAX_BUGS];
  SSBug    prevBugs[SS_MAX_BUGS];
  int  score, lives, wave;
  int  prevScore, prevLives, prevWave;
  bool gameOver;
  bool bgDrawn;
  unsigned long lastFrame, lastShot;
  int  bugsAlive;
} ship;

void ssReset(){
  ship.sx=SCR_W/2; ship.prevSx=ship.sx; ship.score=0; ship.lives=3; ship.wave=1; ship.gameOver=false; ship.bugsAlive=0;
  ship.prevScore=-1; ship.prevLives=-1; ship.prevWave=-1; ship.bgDrawn=false;
  for(auto& b:ship.bullets) b.alive=false;
  for(int i=0;i<SS_MAX_BUGS;i++){
    ship.bugs[i]={(float)(20+i*18),(float)(GAME_TOP+12+(i/8)*32),(float)(random(3)-1)*0.8f,0.3f,true,(uint16_t)(i%2?C_RED:C_GREEN)};
    ship.bugsAlive++;
  }
}

void ssDrawShip(float sx){
  int shipY=SCR_H-40;
  tft.fillTriangle((int)sx,shipY-20,(int)sx-14,shipY+10,(int)sx+14,shipY+10,C_CYAN);
  tft.fillRect((int)sx-3,shipY+8,6,8,C_ORANGE);
}

void ssEraseShip(float sx){
  int shipY=SCR_H-40;
  tft.fillRect((int)sx-16,shipY-22,32,34,C_BG);
}

void ssDrawBullet(const SSBullet& b){
  if(!b.alive)return;
  tft.fillRect((int)b.x-1,(int)b.y-6,3,10,C_YELLOW);
}

void ssEraseBullet(const SSBullet& b){
  if(!b.alive)return;
  tft.fillRect((int)b.x-2,(int)b.y-7,5,12,C_BG);
}

void ssDrawBug(const SSBug& b){
  if(!b.alive)return;
  tft.fillCircle((int)b.x,(int)b.y,8,b.col);
  tft.drawLine((int)b.x-8,(int)b.y-4,(int)b.x-14,(int)b.y-8,b.col);
  tft.drawLine((int)b.x+8,(int)b.y-4,(int)b.x+14,(int)b.y-8,b.col);
}

void ssEraseBug(const SSBug& b){
  if(!b.alive)return;
  tft.fillRect((int)b.x-16,(int)b.y-12,32,24,C_BG);
}

void ssDrawHud(){
  paintGameChromeOnce(GAME_STARSHIP, C_CYAN);
  if(ship.score!=ship.prevScore||ship.lives!=ship.prevLives||ship.wave!=ship.prevWave){
    char buf[32]; snprintf(buf,32,"SC:%05d LV:%d W:%d",ship.score,ship.lives,ship.wave);
    paintGameHudLine(90,12,buf,C_CYAN);
    ship.prevScore=ship.score; ship.prevLives=ship.lives; ship.prevWave=ship.wave;
  }
}

void ssDrawFrame(){
  tft.startWrite();
  if(!ship.bgDrawn){
    tft.fillRect(0,GAME_TOP,SCR_W,SCR_H-GAME_TOP,C_BG);
    ship.bgDrawn=true;
  }
  ssEraseShip(ship.prevSx);
  for(int i=0;i<SS_MAX_BULLETS;i++) ssEraseBullet(ship.prevBullets[i]);
  for(int i=0;i<SS_MAX_BUGS;i++) ssEraseBug(ship.prevBugs[i]);
  for(int i=0;i<SS_MAX_BULLETS;i++) ssDrawBullet(ship.bullets[i]);
  for(int i=0;i<SS_MAX_BUGS;i++) ssDrawBug(ship.bugs[i]);
  ssDrawShip(ship.sx);
  ship.prevSx=ship.sx;
  for(int i=0;i<SS_MAX_BULLETS;i++) ship.prevBullets[i]=ship.bullets[i];
  for(int i=0;i<SS_MAX_BUGS;i++) ship.prevBugs[i]=ship.bugs[i];
  ssDrawHud();
  tft.endWrite();
}

void drawStarship(){
  tft.startWrite();
  tft.fillRect(0,GAME_TOP,SCR_W,SCR_H-GAME_TOP,C_BG);
  ship.bgDrawn=true;
  if(ship.gameOver){
    glowText(SCR_W/2-48,180,"GAME OVER",C_RED,3);
    char buf[24]; snprintf(buf,24,"SCORE: %d",ship.score);
    centreText(SCR_W/2,230,buf,C_WHITE,2,C_BG);
    centreText(SCR_W/2,270,"Tap to play again",C_CYAN,1,C_BG);
    paintGameChromeOnce(GAME_STARSHIP, C_CYAN);
    tft.endWrite();
    return;
  }
  for(int i=0;i<SS_MAX_BULLETS;i++) ssDrawBullet(ship.bullets[i]);
  for(int i=0;i<SS_MAX_BUGS;i++) ssDrawBug(ship.bugs[i]);
  ssDrawShip(ship.sx);
  ship.prevSx=ship.sx;
  for(int i=0;i<SS_MAX_BULLETS;i++) ship.prevBullets[i]=ship.bullets[i];
  for(int i=0;i<SS_MAX_BUGS;i++) ship.prevBugs[i]=ship.bugs[i];
  ssDrawHud();
  tft.endWrite();
}

void updateStarship(){
  unsigned long now=millis();
  if(now-ship.lastFrame<40)return; ship.lastFrame=now;
  if(ship.gameOver){if(touchReady()){readTouch();lastTouchMs=millis();ssReset();drawStarship();}return;}
  if(touchReady()){TouchPt t=readTouch();lastTouchMs=millis(); if (t.pressed && handleGameBack(t)) return; if(t.y>GAME_TOP)ship.sx=t.x;}
  ship.sx=constrain(ship.sx,16,SCR_W-16);
  if(now-ship.lastShot>250){
    ship.lastShot=now;
    for(auto& b:ship.bullets){if(!b.alive){b={ship.sx,SCR_H-50,true};break;}}
  }
  for(auto& b:ship.bullets){if(b.alive){b.y-=8;if(b.y<GAME_TOP+8)b.alive=false;}}
  for(auto& b:ship.bugs){
    if(!b.alive)continue;
    b.x+=b.vx; b.y+=b.vy;
    if(b.x<8||b.x>SCR_W-8)b.vx*=-1;
    if(b.y>SCR_H-50){ship.lives--;b.alive=false;ship.bugsAlive--;sndDeath();if(ship.lives<=0){ship.gameOver=true;sndGameOver();}}
    for(auto& blt:ship.bullets){if(!blt.alive)continue;if(abs(blt.x-b.x)<12&&abs(blt.y-b.y)<12){blt.alive=false;b.alive=false;ship.bugsAlive--;ship.score+=100;sndHit();}}
  }
  if(ship.bugsAlive<=0){
    ship.wave++; ship.bugsAlive=0;
    for(int i=0;i<SS_MAX_BUGS;i++) ship.bugs[i].alive=false;
    for(int i=0;i<min(SS_MAX_BUGS,8+ship.wave*2);i++){
      ship.bugs[i]={(float)(16+i*20),(float)(GAME_TOP+30+(i/8)*28),(float)(random(3)-1)*(0.8f+ship.wave*0.2f),0.25f+ship.wave*0.05f,true,(uint16_t)(i%3==0?C_RED:i%3==1?C_PURPLE:C_ORANGE)};
      ship.bugsAlive++;
    }
    drawStarship();
    return;
  }
  ssDrawFrame();
}

// GAME: MEMORY
#define MEM_COLS 4
#define MEM_ROWS 4
#define MEM_CW   70
#define MEM_CH   58
#define MEM_PAD  4
#define MEM_OX   ((SCR_W-(MEM_COLS*(MEM_CW+MEM_PAD)-MEM_PAD))/2)
#define MEM_OY   (GAME_TOP+15)

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
  tft.startWrite();
  tft.fillRect(0,GAME_TOP,SCR_W,SCR_H-GAME_TOP,C_BG);
  char buf[24]; snprintf(buf,24,"MEMORY  Pairs:%d/8",MEM.pairs);
  centreText(SCR_W/2,GAME_TOP+4,buf,C_PURPLE,2,C_BG);
  hRule(GAME_TOP+30,C_PURPLE);
  for(int r=0;r<MEM_ROWS;r++) for(int c=0;c<MEM_COLS;c++) drawMemCard(r,c);
  snprintf(buf,24,"Score: %d",MEM.score);
  tft.setTextSize(1); tft.setTextColor(C_LGRAY,C_BG); tft.setCursor(8,460); tft.print(buf);
  tft.setTextColor(C_DGRAY,C_BG); tft.setCursor(SCR_W-88,460); tft.print("[top-left=back]");
  if(MEM.win){glowText(SCR_W/2-48,420,"YOU WIN!",C_GREEN,3);}
  paintGameChromeOnce(GAME_MEMORY, C_PURPLE);
  paintGameHudLine(90,12,"MEMORY",C_PURPLE);
  tft.endWrite();
}

void updateMemory(){
  if(MEM.win){if(touchReady()){readTouch();lastTouchMs=millis();memShuffle();drawMemGame();}return;}
  if(MEM.state==2){
    if(millis()-MEM.checkStart>900){
      bool ok=(MEM.cards[MEM.firstR][MEM.firstC]==MEM.cards[MEM.secondR][MEM.secondC]);
      if(ok){MEM.matched[MEM.firstR][MEM.firstC]=MEM.matched[MEM.secondR][MEM.secondC]=true;MEM.score+=100;MEM.pairs++;sndMatch();if(MEM.pairs==8){MEM.win=true;sndWin();}}
      else  {MEM.flipped[MEM.firstR][MEM.firstC]=MEM.flipped[MEM.secondR][MEM.secondC]=false;sndBuzz();}
      MEM.state=0; drawMemGame();
    }
    return;
  }
  if(!touchReady())return;
  TouchPt t=readTouch(); lastTouchMs=millis();
  if (handleGameBack(t)) return;
  int c=(t.x-MEM_OX)/(MEM_CW+MEM_PAD), r=(t.y-MEM_OY)/(MEM_CH+MEM_PAD);
  if(r<0||r>=MEM_ROWS||c<0||c>=MEM_COLS)return;
  if(MEM.flipped[r][c]||MEM.matched[r][c])return;
  MEM.flipped[r][c]=true; sndClick();
  if(MEM.state==0){MEM.firstR=r;MEM.firstC=c;MEM.state=1;}
  else{MEM.secondR=r;MEM.secondC=c;MEM.state=2;MEM.checkStart=millis();}
  drawMemCard(r,c);
}

// GAME: COLOR MATCH
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
  tft.startWrite();
  tft.fillRect(0,GAME_TOP,SCR_W,SCR_H-GAME_TOP,C_BG);
  unsigned long el=millis()-CM.roundStart;
  int bw=(int)((max(0UL,5000-el)*(SCR_W-20))/5000);
  tft.fillRect(10,GAME_TOP+38,SCR_W-20,8,C_SURF);
  tft.fillRect(10,GAME_TOP+38,bw,8,el<3500?C_GREEN:C_RED);
  tft.fillRoundRect(50,GAME_TOP+52,SCR_W-100,108,10,CM.targetCol);
  tft.setTextSize(3); tft.setTextColor(C_WHITE,CM.targetCol);
  int16_t tw=strlen(CM.targetName)*18;
  tft.setCursor(50+(SCR_W-100-tw)/2,GAME_TOP+88); tft.print(CM.targetName);
  tft.setTextSize(1); centreText(SCR_W/2,GAME_TOP+170,"TAP THE MATCHING COLOR",C_LGRAY,1,C_BG);
  const int BW=(SCR_W-28)/2,BH=96;
  for(int i=0;i<4;i++){
    int bx=8+(i%2)*(BW+12),by=GAME_TOP+180+(i/2)*(BH+10);
    tft.fillRoundRect(bx,by,BW,BH,8,CM.options[i]);
    tft.drawRoundRect(bx,by,BW,BH,8,C_WHITE);
    tft.setTextSize(2); tft.setTextColor(C_WHITE,CM.options[i]);
    tw=strlen(CM.names[i])*12; tft.setCursor(bx+(BW-tw)/2,by+BH/2-8); tft.print(CM.names[i]);
  }
  char buf[32]; snprintf(buf,32,"Score:%d  Streak:%d",CM.score,CM.streak);
  tft.setTextSize(1); tft.setTextColor(C_LGRAY,C_BG); tft.setCursor(8,392); tft.print(buf);
  paintGameChromeOnce(GAME_COLORMATCH, C_ORANGE);
  paintGameHudLine(90,12,"COLOR MATCH",C_ORANGE);
  tft.endWrite();
}

void drawColMatchTimer(){
  unsigned long el=millis()-CM.roundStart;
  int bw=(int)((max(0UL,5000-el)*(SCR_W-20))/5000);
  tft.fillRect(10,GAME_TOP+38,SCR_W-20,8,C_SURF);
  tft.fillRect(10,GAME_TOP+38,bw,8,el<3500?C_GREEN:C_RED);
}

void updateColMatch(){
  if(millis()-CM.roundStart>5000){CM.streak=0;cmNewRound();drawColMatch();return;}
  static unsigned long lastTimer=0;
  if(millis()-lastTimer>120){lastTimer=millis();drawColMatchTimer();}
  if(!touchReady())return;
  TouchPt t=readTouch(); lastTouchMs=millis();
  if (handleGameBack(t)) return;
  const int BW=(SCR_W-28)/2,BH=96;
  for(int i=0;i<4;i++){
    int bx=8+(i%2)*(BW+12),by=GAME_TOP+180+(i/2)*(BH+10);
    if(t.x>=bx&&t.x<bx+BW&&t.y>=by&&t.y<by+BH){
      if(i==CM.correct){CM.score+=10+CM.streak*5;CM.streak++;sndCorrect();}else{CM.streak=0;sndBuzz();}
      cmNewRound(); drawColMatch(); return;
    }
  }
}

// GAME: MATH QUIZ
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
  tft.startWrite();
  tft.fillRect(0,GAME_TOP,SCR_W,SCR_H-GAME_TOP,C_BG);
  if(MQ.gameOver){
    glowText(SCR_W/2-48,180,"GAME OVER!",C_RED,3);
    char b[24]; snprintf(b,24,"Score: %d",MQ.score);
    centreText(SCR_W/2,240,b,C_WHITE,2,C_BG);
    centreText(SCR_W/2,280,"Tap to play again",C_CYAN,1,C_BG);
    paintGameChromeOnce(GAME_MATH, C_GREEN);
    tft.endWrite();
    return;
  }
  char buf[32]; const char* ops[]={"+","-","x"};
  snprintf(buf,32,"%d %s %d = ?",MQ.a,ops[MQ.op],MQ.b);
  tft.setTextSize(4); tft.setTextColor(C_YELLOW,C_BG);
  int16_t tw=strlen(buf)*24; tft.setCursor(SCR_W/2-tw/2,GAME_TOP+48); tft.print(buf);
  unsigned long el=millis()-MQ.roundStart;
  int bw2=(int)((max(0UL,8000-el)*(SCR_W-20))/8000);
  tft.fillRect(10,GAME_TOP+112,SCR_W-20,8,C_SURF); tft.fillRect(10,GAME_TOP+112,bw2,8,el<5000?C_GREEN:C_RED);
  const int BW=(SCR_W-28)/2,BH=92;
  uint16_t cc[]={C_CYAN,C_PURPLE,C_ORANGE,C_GREEN};
  for(int i=0;i<4;i++){
    int bx=8+(i%2)*(BW+12),by=GAME_TOP+128+(i/2)*(BH+10);
    neonBox(bx,by,BW,BH,cc[i],C_SURF);
    snprintf(buf,8,"%d",MQ.choices[i]);
    tft.setTextSize(4); tft.setTextColor(cc[i],C_SURF);
    tw=strlen(buf)*24; tft.setCursor(bx+(BW-tw)/2,by+BH/2-16); tft.print(buf);
  }
  snprintf(buf,32,"Sc:%d Str:%d Lv:%d",MQ.score,MQ.streak,MQ.lives);
  tft.setTextSize(1); tft.setTextColor(C_LGRAY,C_BG); tft.setCursor(8,390); tft.print(buf);
  paintGameChromeOnce(GAME_MATH, C_GREEN);
  paintGameHudLine(90,12,"MATH QUIZ",C_GREEN);
  tft.endWrite();
}

void drawMathTimer(){
  unsigned long el=millis()-MQ.roundStart;
  int bw2=(int)((max(0UL,8000-el)*(SCR_W-20))/8000);
  tft.fillRect(10,GAME_TOP+112,SCR_W-20,8,C_SURF);
  tft.fillRect(10,GAME_TOP+112,bw2,8,el<5000?C_GREEN:C_RED);
}

void updateMath(){
  if(!MQ.gameOver && millis()-MQ.roundStart>8000){
    MQ.lives--;MQ.streak=0;
    if(MQ.lives<=0){MQ.gameOver=true;sndGameOver();drawMathGame();}
    else{mqNewRound();drawMathGame();}
    return;
  }
  static unsigned long lastTimer=0;
  if(!MQ.gameOver && millis()-lastTimer>120){lastTimer=millis();drawMathTimer();}
  if(MQ.gameOver){if(touchReady()){readTouch();lastTouchMs=millis();MQ.score=0;MQ.streak=0;MQ.lives=3;MQ.gameOver=false;mqNewRound();drawMathGame();}return;}
  if(!touchReady())return;
  TouchPt t=readTouch(); lastTouchMs=millis();
  if (handleGameBack(t)) return;
  const int BW=(SCR_W-28)/2,BH=92;
  for(int i=0;i<4;i++){
    int bx=8+(i%2)*(BW+12),by=GAME_TOP+128+(i/2)*(BH+10);
    if(t.x>=bx&&t.x<bx+BW&&t.y>=by&&t.y<by+BH){
      if(i==MQ.correct){MQ.score+=10+MQ.streak*5;MQ.streak++;sndCorrect();}
      else{MQ.lives--;MQ.streak=0;sndWrong();if(MQ.lives<=0){MQ.gameOver=true;sndGameOver();drawMathGame();return;}}
      mqNewRound(); drawMathGame(); return;
    }
  }
}

// SCREEN ROUTER
void initScreen(Screen s) {
  handleMegaSerial();
  if (s >= GAME_MARIO) resetGameChrome();
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
    case SCR_LIGHTS:      drawLights();       break;
    case GAME_MARIO:      marioReset();       drawMarioGame();  break;
    case GAME_PACMAN:     pacReset();         drawPacGame();    break;
    case GAME_STARSHIP:   ssReset();          drawStarship();   break;
    case GAME_MEMORY:     memShuffle();       drawMemGame();    break;
    case GAME_COLORMATCH: cmNewRound();       drawColMatch();   break;
    case GAME_MATH:       MQ.score=0;MQ.streak=0;MQ.lives=3;MQ.gameOver=false;mqNewRound(); drawMathGame(); break;
    default: break;
  }
  paintedScreen = s;
  handleMegaSerial();
}

void handleTouch(TouchPt& t) {
  switch(curScreen) {
    case SCR_MAIN:       handleMainTouch(t);     break;
    case SCR_GAMES:      handleGamesTouch(t);    break;
    case SCR_SENSORS:    handleSensorsTouch(t);  break;
    case SCR_SENS_BRAIN: handleBrainTouch(t);    break;
    case SCR_SETTINGS:   handleSettingsTouch(t); break;
    case SCR_LIGHTS:     handleLightsTouch(t);   break;
    case SCR_SENS_EYES:
    case SCR_SENS_NOSE:
    case SCR_SENS_TUMMY:
      if(t.y<72){curScreen=SCR_SENSORS;screenDirty=true;}
      break;
    case SCR_COMMS:
      if(t.y<72){curScreen=SCR_MAIN;screenDirty=true;}
      break;
    default: break;
  }
}

void setup() {
  delay(200);
  // bridgeSetup() initialises USB CDC (S9) + Serial2 UART1 (Mega GP4/GP5).
  bridgeSetup();
  Serial.println("[PICO] BuddyBot Dash booting...");
  tft.init();
  tft.setRotation(ROTATION);
  tft.invertDisplay(false);
  tft.fillScreen(C_BG);
  pinMode(22, OUTPUT); digitalWrite(22, HIGH);
  centreText(SCR_W/2, SCR_H/2-24, "AJ2BUDDYCOMMS", C_CYAN, 2, C_BG);
  centreText(SCR_W/2, SCR_H/2+8,  "PicoW Dash v1.1",  C_LGRAY,1, C_BG);
  waitMs(1200);
  sndBoot();
  pinMode(AUDIO_PIN, OUTPUT);
  digitalWrite(AUDIO_PIN, LOW);
  pinMode(PIN_CTP_RST, OUTPUT);
  digitalWrite(PIN_CTP_RST, LOW);  waitMs(50);
  digitalWrite(PIN_CTP_RST, HIGH); waitMs(300);
  Wire1.setSDA(PIN_CTP_SDA);
  Wire1.setSCL(PIN_CTP_SCL);
  Wire1.begin();
  Wire1.setClock(400000);
  waitMs(50);
  pinMode(PIN_CTP_INT, INPUT_PULLUP);
  Wire1.beginTransmission(CTP_ADDR);
  int err = Wire1.endTransmission();
  if (err == 0) {
    Wire1.beginTransmission(CTP_ADDR); Wire1.write(0x00); Wire1.write(0x00); Wire1.endTransmission(); delay(5);
    Wire1.beginTransmission(CTP_ADDR); Wire1.write(0xA4); Wire1.write(0x00); Wire1.endTransmission(); delay(5);
  }
  MEGA_SERIAL.println("PING");
  waitMs(400);
  MEGA_SERIAL.println("STATUS");
  waitMs(1200);
  initScreen(SCR_MAIN);
  screenDirty = false;
  headerDirty = true;
  bodyDirty = true;
  Serial.println("[PICO] Ready.");
}

bool isGameScreen() {
  return (curScreen==GAME_MARIO || curScreen==GAME_PACMAN ||
          curScreen==GAME_STARSHIP || curScreen==GAME_MEMORY ||
          curScreen==GAME_COLORMATCH || curScreen==GAME_MATH);
}

bool gameDirty = true;

bool handleGameBack(const TouchPt& t) {
  if (t.pressed && t.y < GAME_TOP && t.x < 88) {
    sndClick();
    curScreen = SCR_GAMES;
    requestFullRefresh();
    return true;
  }
  return false;
}

void loop() {
  sndUpdate();
  bridgeLoop();               // V1.1: S9↔Mega bidirectional line forwarding
  // USB heartbeat — visible on PC serial monitor to confirm Pico firmware is alive
  static unsigned long lastUsbHb = 0;
  if (millis() - lastUsbHb > 5000) {
    lastUsbHb = millis();
    Serial.print(F("PICO_ALIVE|Mega="));
    Serial.print(megaLinked ? 'Y' : 'N');
    Serial.print(F("|LastRx="));
    Serial.print((millis() - lastMegaRx) / 1000);
    Serial.println('s');
  }
  sendMegaHeartbeat();
  if (wifiIpReady) {
    wifiIpReady = false;
    picoToMega(String(F("WIFI_IP:")) + pendingWifiIp);
    dbgPush("[PICO] Sent WiFi IP to Mega");
  }
  if (webCmdReady) {
    char wc[64]; strncpy(wc,(char*)webCmd,63); wc[63]=0; webCmdReady=false;
    picoToMega(String(wc));
  }
  bridgeLoop();
  if (megaLinked && millis()-lastMegaRx > 12000) {
    megaLinked = false;
    T.megaUartOk = false;
    if (!isGameScreen()) markDirty();
  }
  if (millis() - lastS9Rx > 30000) {
    T.s9ok = false;
  }
  if (isGameScreen()) {
    if      (curScreen==GAME_MARIO)      updateMario();
    else if (curScreen==GAME_PACMAN)     updatePacman();
    else if (curScreen==GAME_STARSHIP)   updateStarship();
    else if (curScreen==GAME_MEMORY)     updateMemory();
    else if (curScreen==GAME_COLORMATCH) updateColMatch();
    else if (curScreen==GAME_MATH)       updateMath();
    if (!isGameScreen()) {
      screenDirty = false;
      headerDirty = false;
      bodyDirty = false;
      initScreen(curScreen);
    }
  } else {
    static unsigned long lastTelemPaint=0;
    if (megaLinked && millis()-lastMegaRx<8000 && millis()-lastTelemPaint>1500) {
      lastTelemPaint=millis();
      requestHeaderRefresh();
      requestBodyRefresh();
    }
    if (screenDirty || curScreen != paintedScreen) {
      screenDirty = false;
      headerDirty = false;
      bodyDirty = false;
      initScreen(curScreen);
    } else {
      if (headerDirty) {
        headerDirty = false;
        drawHeader();
      }
      if (bodyDirty) {
        bodyDirty = false;
        refreshScreenBody();
      }
    }
    if (millis()-lastTouchMs > 100) {
      TouchPt t=readTouch();
      if (t.pressed) {
        lastTouchMs=millis();
        Screen prev=curScreen;
        handleTouch(t);
        if (curScreen != prev) {
          screenDirty = false;
          headerDirty = false;
          bodyDirty = false;
          initScreen(curScreen);
        }
      }
    }
  }
}

// WIFI BRIDGE -- Core 1 (ESP8285 on UART0 GP0/GP1 via WiFiEspAT)
WiFiServer webSrv(80);

volatile uint32_t sharedSeq = 0;
volatile int   sh_gas=0,  sh_pct=0;
volatile float sh_temp=0,sh_hum=0,sh_volt=0,sh_amps=0;
volatile long  sh_dFront=-1,sh_dRear=-1,sh_dLeft=-1,sh_dRight=-1;
volatile bool  sh_estop=false,sh_autoM=false,sh_megaOk=false,sh_s9ok=false;
volatile bool  sh_pir=false,sh_irFront=false,sh_irRear=false,sh_tilt=false;
volatile char  sh_mode[16]="NORMAL";
volatile char  sh_fw[16]="";
volatile char  sh_ledMode[10]="OFF";
volatile char  sh_ledWhite[6]="OFF";
volatile int   sh_ledBright=255;
volatile bool  wifiOK=false;
volatile char webCmd[64]={0};
volatile bool webCmdReady=false;

void updateShared(){
  sharedSeq++;
  sh_gas=T.gas; sh_pct=T.pct; sh_temp=T.temp; sh_hum=T.hum;
  sh_volt=T.volt; sh_amps=T.amps;
  sh_dFront=T.dFront; sh_dRear=T.dRear; sh_dLeft=T.dLeft; sh_dRight=T.dRight;
  sh_estop=T.estop; sh_autoM=T.autoM; sh_megaOk=T.megaUartOk; sh_s9ok=T.s9ok;
  sh_pir=T.pir; sh_irFront=T.irFront; sh_irRear=T.irRear; sh_tilt=T.tilt;
  strncpy((char*)sh_mode,T.mode,15); ((char*)sh_mode)[15]=0;
  strncpy((char*)sh_fw,  T.fw,  15); ((char*)sh_fw  )[15]=0;
  strncpy((char*)sh_ledMode, T.ledMode, 9); ((char*)sh_ledMode)[9]=0;
  strncpy((char*)sh_ledWhite,T.ledWhite,5); ((char*)sh_ledWhite)[5]=0;
  sh_ledBright=T.ledBright;
  sharedSeq++;
}

const char* batTierStr(){
  if(sh_volt<6.8f)return"CRIT";
  if(sh_volt<7.0f)return"LOW";
  if(sh_volt<7.4f)return"WARN";
  return"OK";
}

static const char HTML_A[] =
"<!DOCTYPE html><html>"
"<head><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
"<title>BuddyBot</title><style>"
"*{box-sizing:border-box;margin:0;padding:0}"
"body{font-family:Arial,sans-serif;background:#1a1a2e;color:#fff;text-align:center;padding:12px}"
"h1{color:#00d4ff;font-size:22px;margin-bottom:10px}"
".card{background:#16213e;border-radius:12px;padding:12px;margin:8px 0}"
".status-row{display:flex;justify-content:space-around;font-size:13px}"
".status-row span{color:#00d4ff;font-weight:bold}"
".grid{display:grid;grid-template-columns:repeat(3,1fr);gap:8px;max-width:280px;margin:12px auto}"
"button{padding:22px 0;width:100%;font-size:20px;font-weight:bold;border:none;border-radius:12px;cursor:pointer}"
"button:active{opacity:.7}"
".fwd{background:#00d4ff;color:#000;grid-column:2;grid-row:1}"
".left{background:#ff9500;color:#000;grid-column:1;grid-row:2}"
".stp{background:#ff3333;color:#fff;grid-column:2;grid-row:2;font-size:13px}"
".right{background:#ff9500;color:#000;grid-column:3;grid-row:2}"
".bwd{background:#00d4ff;color:#000;grid-column:2;grid-row:3}"
".auto{background:#33ff33;color:#000;grid-column:1;grid-row:3;font-size:13px}"
".dance{background:#ff00ff;color:#fff;grid-column:3;grid-row:3;font-size:13px}"
".spd{background:#16213e;color:#00d4ff;border:2px solid #00d4ff;padding:10px;margin:4px;font-size:13px;border-radius:8px}"
".spd.active{background:#00d4ff;color:#000}"
".sensors{text-align:left;font-size:12px}"
".row{display:flex;justify-content:space-between;padding:3px 0;border-bottom:1px solid #0d1b2a}"
".row span:last-child{color:#00d4ff}"
"#wifiip{font-size:11px;color:#888;margin-top:4px}"
".st{display:inline-block;padding:3px 8px;border-radius:6px;cursor:pointer;font-size:11px;font-weight:bold;margin:1px}"
".on{background:#004400;color:#33ff33;border:1px solid #33ff33}"
".off{background:#440000;color:#ff4444;border:1px solid #ff4444}"
"details summary{cursor:pointer;color:#00d4ff;font-size:12px;margin-top:8px}"
".lgrid{display:grid;grid-template-columns:repeat(3,1fr);gap:6px;max-width:300px;margin:8px auto}"
".lbtn{padding:14px 0;font-size:13px;background:#16213e;color:#fff;border:2px solid #445;border-radius:10px}"
".lbtn.active{box-shadow:inset 0 0 0 3px #fff}"
".lpolice{border-color:#ff3b3b;color:#ff7a7a}"
".lalert{border-color:#ff7a00;color:#ffae5a}"
".cgrid{display:flex;gap:6px;justify-content:center;flex-wrap:wrap;margin:8px auto}"
".sw{width:34px;height:34px;border-radius:8px;border:2px solid #000;cursor:pointer}"
".wbtn{padding:11px 14px;font-size:13px;background:#16213e;color:#00d4ff;border:2px solid #00d4ff;border-radius:10px;margin:3px}"
".lstat{font-size:12px;color:#9aa;margin-top:6px}"
".lrng{width:82%}"
"</style></head><body>";

static const char HTML_B[] =
"<h1>&#129302; BuddyBot</h1>"
"<div class=\"card\"><div class=\"status-row\">"
"<div>Status<br><span id=\"sts\">--</span></div>"
"<div>Battery<br><span id=\"bat\">--</span></div>"
"<div>Mode<br><span id=\"mod\">--</span></div>"
"</div><div id=\"wifiip\"></div></div>"
"<div class=\"grid\">"
"<button class=\"fwd\" ontouchstart=\"c('F')\" onmousedown=\"c('F')\">&#9650;</button>"
"<button class=\"left\" ontouchstart=\"c('L')\" onmousedown=\"c('L')\">&#9664;</button>"
"<button class=\"stp\" ontouchstart=\"c('S')\" onmousedown=\"c('S')\">STOP</button>"
"<button class=\"right\" ontouchstart=\"c('R')\" onmousedown=\"c('R')\">&#9654;</button>"
"<button class=\"bwd\" ontouchstart=\"c('B')\" onmousedown=\"c('B')\">&#9660;</button>"
"<button class=\"auto\" ontouchstart=\"c('AUTO')\" onmousedown=\"c('AUTO')\">AUTO</button>"
"<button class=\"dance\" ontouchstart=\"c('DANCE')\" onmousedown=\"c('DANCE')\">DANCE</button>"
"</div>"
"<div>"
"<button class=\"spd\" id=\"ss\" onclick=\"c('SLOW')\">SLOW</button>"
"<button class=\"spd active\" id=\"sn\" onclick=\"c('NORMAL')\">NORMAL</button>"
"<button class=\"spd\" id=\"sf\" onclick=\"c('FAST')\">FAST</button>"
"</div>"
"<div class=\"card\">"
"<div style=\"color:#ff7af0;font-size:15px;font-weight:bold;margin-bottom:6px\">&#128161; LIGHTS</div>"
"<div class=\"lgrid\">"
"<button class=\"lbtn\" id=\"le_OFF\" onclick=\"c('LED:OFF')\">OFF</button>"
"<button class=\"lbtn lpolice\" id=\"le_POLICE\" onclick=\"c('LED:POLICE')\">POLICE</button>"
"<button class=\"lbtn lalert\" id=\"le_ALERT\" onclick=\"c('LED:ALERT')\">ALERT</button>"
"<button class=\"lbtn\" id=\"le_RAINBOW\" onclick=\"c('LED:RAINBOW')\">RAINBOW</button>"
"<button class=\"lbtn\" id=\"le_BREATHE\" onclick=\"c('LED:BREATHE')\">BREATHE</button>"
"<button class=\"lbtn\" id=\"le_PARTY\" onclick=\"c('LED:PARTY')\">PARTY</button>"
"</div>"
"<div class=\"cgrid\">"
"<div class=\"sw\" style=\"background:#f00\" onclick=\"c('LED:RED')\"></div>"
"<div class=\"sw\" style=\"background:#0f0\" onclick=\"c('LED:GREEN')\"></div>"
"<div class=\"sw\" style=\"background:#00f\" onclick=\"c('LED:BLUE')\"></div>"
"<div class=\"sw\" style=\"background:#0ff\" onclick=\"c('LED:CYAN')\"></div>"
"<div class=\"sw\" style=\"background:#a0f\" onclick=\"c('LED:PURPLE')\"></div>"
"<div class=\"sw\" style=\"background:#f50\" onclick=\"c('LED:ORANGE')\"></div>"
"<div class=\"sw\" style=\"background:#fc0\" onclick=\"c('LED:YELLOW')\"></div>"
"</div>"
"<div><button class=\"wbtn\" id=\"wbtn\" onclick=\"wcyc()\">WHITE: --</button>"
"<button class=\"wbtn\" onclick=\"c('LED:WHITE:SOLO')\">W-SOLO</button></div>"
"<div style=\"margin-top:6px;font-size:12px\">Brightness "
"<input type=\"range\" min=\"16\" max=\"255\" value=\"255\" id=\"lbr\" class=\"lrng\" oninput=\"c('LED:BRIGHT:'+this.value)\"></div>"
"<div class=\"lstat\" id=\"lstat\">Mode: -- | White: -- | Bright: --</div>"
"</div>"
"<div class=\"card sensors\" style=\"margin-top:10px\">"
"<div class=\"row\"><span>Front</span><span id=\"fr\">--</span></div>"
"<div class=\"row\"><span>Rear</span><span id=\"re\">--</span></div>"
"<div class=\"row\"><span>Temp</span><span id=\"tp\">--</span></div>"
"<div class=\"row\"><span>Gas</span><span id=\"ga\">--</span></div>"
"<details><summary>Sensor toggles</summary><div id=\"sc\">"
"<span class=\"st\" id=\"d0\" onclick=\"ts('DHT')\">DHT</span>"
"<span class=\"st\" id=\"d1\" onclick=\"ts('LIGHT')\">Light</span>"
"<span class=\"st\" id=\"d2\" onclick=\"ts('SOUND')\">Sound</span>"
"<span class=\"st\" id=\"d3\" onclick=\"ts('GAS')\">Gas</span>"
"<span class=\"st\" id=\"d4\" onclick=\"ts('PIR')\">PIR</span>"
"<span class=\"st\" id=\"d5\" onclick=\"ts('TILT')\">Tilt</span>"
"<span class=\"st\" id=\"d6\" onclick=\"ts('IR')\">IR</span>"
"<span class=\"st\" id=\"d7\" onclick=\"ts('US')\">US</span>"
"<span class=\"st\" id=\"d8\" onclick=\"ts('CURRENT')\">Current</span>"
"<span class=\"st\" id=\"d9\" onclick=\"ts('GPS')\">GPS</span>"
"</div></details></div>";

static const char HTML_C[] =
"<script>"
"const sn={'DHT':'d0','LIGHT':'d1','SOUND':'d2','GAS':'d3','PIR':'d4','TILT':'d5','IR':'d6','US':'d7','CURRENT':'d8','GPS':'d9'};"
"const ss={DHT:1,LIGHT:1,SOUND:1,GAS:1,PIR:1,TILT:1,IR:1,US:1,CURRENT:1,GPS:1};"
"function ui(id,on){const e=document.getElementById(sn[id]);if(e)e.className='st '+(on?'on':'off');}"
"function ts(id){ss[id]=!ss[id];ui(id,ss[id]);fetch('/cmd?c=TOGGLE_SENSOR:'+id+':'+(ss[id]?'ON':'OFF')).catch(()=>{});}"
"function c(v){fetch('/cmd?c='+v).catch(()=>{});}"
"let LWS=['OFF','ON','AUTO'];let LWI=0;"
"function wcyc(){LWI=(LWI+1)%3;c('LED:WHITE:'+LWS[LWI]);}"
"function poll(){fetch('/status').then(r=>r.json()).then(d=>{"
"document.getElementById('sts').textContent=d.status;"
"document.getElementById('bat').textContent=d.battery+'V ('+d.pct+'%)';"
"document.getElementById('mod').textContent=d.mode;"
"document.getElementById('fr').textContent=d.front+'cm';"
"document.getElementById('re').textContent=d.rear+'cm';"
"document.getElementById('tp').textContent=d.temp+'C';"
"document.getElementById('ga').textContent=d.gas;"
"const wi=document.getElementById('wifiip');if(wi&&d.wip)wi.textContent='PicoW: '+d.wip;"
"if(d.led){document.querySelectorAll('.lbtn').forEach(b=>b.classList.remove('active'));var ae=document.getElementById('le_'+d.led);if(ae)ae.classList.add('active');}"
"var wb=document.getElementById('wbtn');if(wb&&d.white){wb.textContent='WHITE: '+d.white;var k=LWS.indexOf(d.white);if(k>=0)LWI=k;}"
"var lb=document.getElementById('lbr');if(lb&&document.activeElement!==lb)lb.value=d.lbr;"
"var ls=document.getElementById('lstat');if(ls)ls.textContent='Mode: '+d.led+' | White: '+d.white+' | Bright: '+d.lbr;"
"}).catch(()=>{})}"
"setInterval(poll,1500);poll();"
"Object.keys(ss).forEach(id=>ui(id,ss[id]));"
"</script></body></html>";

void sendHTTPHeaders(WiFiClient& cl, const char* st, const char* ct) {
  cl.print("HTTP/1.1 "); cl.print(st);
  cl.print("\r\nContent-Type: "); cl.print(ct);
  cl.print("\r\nConnection: close\r\nAccess-Control-Allow-Origin: *\r\n\r\n");
}

void serveStatus(WiFiClient& cl) {
  char json[512], ipBuf[20]="0.0.0.0";
  if (wifiOK) {
    IPAddress ip=WiFi.localIP();
    snprintf(ipBuf,sizeof(ipBuf),"%d.%d.%d.%d",ip[0],ip[1],ip[2],ip[3]);
  }
  snprintf(json,sizeof(json),
    "{\"status\":\"%s\",\"battery\":\"%.1f\",\"pct\":%d,\"bat_tier\":\"%s\","
    "\"mode\":\"%s\",\"front\":%ld,\"rear\":%ld,"
    "\"temp\":\"%.1f\",\"gas\":\"%d\","
    "\"wip\":\"%s\",\"fw\":\"%s\","
    "\"led\":\"%s\",\"white\":\"%s\",\"lbr\":%d}",
    sh_estop?"ESTOP":sh_autoM?"AUTO":sh_megaOk?"IDLE":"WAIT",
    sh_volt,sh_pct,batTierStr(),(char*)sh_mode,
    sh_dFront<0?0L:sh_dFront, sh_dRear<0?0L:sh_dRear,
    sh_temp,sh_gas,ipBuf,(char*)sh_fw,
    (char*)sh_ledMode,(char*)sh_ledWhite,sh_ledBright);
  sendHTTPHeaders(cl,"200 OK","application/json");
  cl.print(json);
}

void serveCmd(WiFiClient& cl, const char* req) {
  const char* ci=strstr(req,"c=");
  if (ci && !webCmdReady) {
    ci+=2;
    const char* end=strchr(ci,' ');
    size_t len=end?(size_t)(end-ci):strlen(ci);
    if (len>0 && len<63) {
      memcpy((char*)webCmd,ci,len);
      ((char*)webCmd)[len]=0;
      webCmdReady=true;
    }
  }
  sendHTTPHeaders(cl,"200 OK","text/plain");
  cl.print("OK");
}

void handleWebClient(WiFiClient& cl) {
  char req[256]={0}; int ri=0;
  unsigned long t0=millis();
  while (cl.connected() && millis()-t0<600) {
    if (cl.available()) {
      char ch=cl.read();
      if (ri<255) req[ri++]=ch;
      if (ri>=4 && req[ri-4]=='\r'&&req[ri-3]=='\n'&&req[ri-2]=='\r'&&req[ri-1]=='\n') break;
    }
  }
  if      (strncmp(req,"GET / "    ,6 )==0||strncmp(req,"GET /index",10)==0) {
    sendHTTPHeaders(cl,"200 OK","text/html");
    cl.print(HTML_A); cl.print(HTML_B); cl.print(HTML_C);
  }
  else if (strncmp(req,"GET /status",11)==0) serveStatus(cl);
  else if (strncmp(req,"GET /cmd"  , 8)==0)  serveCmd(cl,req);
  else { sendHTTPHeaders(cl,"404 Not Found","text/plain"); cl.print("404"); }
  delay(5);
  cl.stop();
}

void setup1() {
  Serial1.setTX(0); Serial1.setRX(1);
  Serial1.begin(115200);
  delay(800);
  WiFi.init(Serial1);
  if (WiFi.status()==WL_NO_MODULE) { wifiOK=false; return; }
  for (int i=0; i<5 && WiFi.status()!=WL_CONNECTED; i++) {
    WiFi.begin(wifiSsid, wifiPass);
    delay(4000);
  }
  if (WiFi.status()==WL_CONNECTED) {
    wifiOK=true;
    webSrv.begin();
    IPAddress ip = WiFi.localIP();
    snprintf(pendingWifiIp, sizeof(pendingWifiIp), "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
    wifiIpReady = true;
  }
}

void loop1() {
  if (wifiReconnectPending) {
    wifiReconnectPending = false;
    strncpy(wifiSsid, pendingWifiSsid, 32); wifiSsid[32] = 0;
    strncpy(wifiPass, pendingWifiPass, 63); wifiPass[63] = 0;
    WiFi.disconnect();
    delay(200);
    bool connected = false;
    for (int i = 0; i < 5 && !connected; i++) {
      WiFi.begin(wifiSsid, wifiPass);
      for (int j = 0; j < 20; j++) {
        delay(500);
        if (WiFi.status() == WL_CONNECTED) { connected = true; break; }
      }
    }
    if (connected) {
      wifiOK = true;
      webSrv.begin();
      IPAddress ip = WiFi.localIP();
      snprintf(pendingWifiIp, sizeof(pendingWifiIp), "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
      wifiIpReady = true;
    } else {
      wifiOK = false;
    }
    return;
  }
  if (WiFi.status()!=WL_CONNECTED) {
    wifiOK=false;
    WiFi.begin(wifiSsid, wifiPass);
    delay(5000);
    if (WiFi.status()==WL_CONNECTED) {
      wifiOK=true;
      webSrv.begin();
      IPAddress ip = WiFi.localIP();
      snprintf(pendingWifiIp, sizeof(pendingWifiIp), "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
      wifiIpReady = true;
    }
    return;
  }
  wifiOK=true;
  WiFiClient client=webSrv.available();
  if (client) handleWebClient(client);
  delay(1);
}
