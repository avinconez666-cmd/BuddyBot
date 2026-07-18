// ═════════════════════════════════════════════════════════════════════════════
//  BUDDYBOT  ·  PICO W DASHBOARD  ·  AUDIO PLAYER MODULE
//  File : audio_player.h
// ═════════════════════════════════════════════════════════════════════════════
//
//  Hardware
//  ────────
//  XC3744 Signal  →  GP2  (SPEAKER_PIN)
//  XC3744 5 V     →  Robot 5 V power rail  (NOT Pico VBUS -- USB-only)
//  XC3744 GND     →  GND
//
//  Core split (RP2040 dual-core)
//  ─────────────────────────────
//  Core 0  owns: TFT display, Mega serial, WiFi.  Never blocked by audio.
//  Core 1  owns: audio engine exclusively.
//
//  Integration -- add to your main Pico W sketch
//  ─────────────────────────────────────────────
//    #include "audio_player.h"
//    void setup1() { audioSetup(); }
//    void loop1()  { audioLoop();  }
//
//  Core 0 API
//  ──────────
//    audioPlay(SND_SMOKE);    // trigger a sound from Core 0 (non-blocking)
//    audioPlay(SND_STOP);     // kill any looping alarm
//    audioBusy();             // true while Core 1 is playing
//    audioFromMega(msg);      // parse a Mega serial message and auto-play
//
//  Mega message → sound map
//  ────────────────────────
//    MEGA_READY / SYSTEM:STANDBY  →  SND_BOOT
//    GAS:SENSOR_READY             →  SND_CHIRP
//    SAFETY:SMOKE_ALARM           →  SND_SMOKE  (loops until STOP/CLEAR)
//    SAFETY:SMOKE_CLEARED         →  SND_CLEAR
//    BAT:WARN                     →  SND_NOTIFY
//    BAT:LOW                      →  SND_LOW_BAT
//    BAT:CRITICAL                 →  SND_ALERT
//    SAFETY:TILT / OVERTEMP       →  SND_ALERT
//    DOCK:LOCKED / DOCK:COMPLETE  →  SND_NOTIFY
//    ACK:DANCE:DONE               →  SND_CHIRP
//    EMERGENCY_STOP / ESTOP       →  SND_ERROR
//
// ═════════════════════════════════════════════════════════════════════════════

#pragma once
#include <Arduino.h>

#define SPEAKER_PIN  2   // GP2 → XC3744 Signal input

// ── Sound IDs ────────────────────────────────────────────────────────────────
enum SoundID : uint8_t {
  SND_NONE    = 0,
  SND_BOOT    = 1,   // rising 3-note chime        (power-on)
  SND_CHIRP   = 2,   // quick ascending sweep       (friendly / ACK)
  SND_NOTIFY  = 3,   // two-note ding-dong          (dock / info)
  SND_ALERT   = 4,   // three urgent beeps          (tilt / overtemp / crit bat)
  SND_LOW_BAT = 5,   // four slow pulses            (battery low warning)
  SND_ERROR   = 6,   // descending three tones      (ESTOP / fault)
  SND_SMOKE   = 7,   // rising/falling siren LOOP   (stops on SND_STOP/SND_CLEAR)
  SND_CLEAR   = 8,   // ascending 4-note fanfare    (all-clear)
  SND_STOP    = 255  // immediately stop any looping alarm
};

// Cross-core shared state (volatile = visible across RP2040 cores)
static volatile uint8_t _aud_cmd  = SND_NONE;
static volatile bool    _aud_busy = false;
static volatile bool    _aud_loop = false;

// ══════════════════════════════════════════════════════════════════════════════
//  CORE 0 API  --  call freely from your main loop or serial handler
// ══════════════════════════════════════════════════════════════════════════════

inline void audioPlay(SoundID id) {
  if (id == SND_STOP || id == SND_CLEAR) _aud_loop = false;
  _aud_cmd = (uint8_t)id;
}

inline bool audioBusy() { return _aud_busy; }

// Drop this into your Mega message parser to get automatic sounds.
inline void audioFromMega(const String& msg) {
  if (msg.startsWith("MEGA_READY") || msg == "SYSTEM:STANDBY") { audioPlay(SND_BOOT);    return; }
  if (msg == "GAS:SENSOR_READY")                                { audioPlay(SND_CHIRP);   return; }
  if (msg == "SAFETY:SMOKE_ALARM")                              { audioPlay(SND_SMOKE);   return; }
  if (msg == "SAFETY:SMOKE_CLEARED")                            { audioPlay(SND_CLEAR);   return; }
  if (msg == "BAT:WARN")                                        { audioPlay(SND_NOTIFY);  return; }
  if (msg == "BAT:LOW")                                         { audioPlay(SND_LOW_BAT); return; }
  if (msg == "BAT:CRITICAL")                                    { audioPlay(SND_ALERT);   return; }
  if (msg == "SAFETY:TILT" || msg == "SAFETY:OVERTEMP")        { audioPlay(SND_ALERT);   return; }
  if (msg.startsWith("DOCK:LOCKED") || msg.startsWith("DOCK:COMP")) { audioPlay(SND_NOTIFY); return; }
  if (msg == "ACK:DANCE:DONE")                                  { audioPlay(SND_CHIRP);   return; }
  if (msg == "EMERGENCY_STOP" || msg == "ESTOP")                { audioPlay(SND_ERROR);   return; }
}

// ══════════════════════════════════════════════════════════════════════════════
//  CORE 1 INTERNALS  --  do not call from Core 0
// ══════════════════════════════════════════════════════════════════════════════

// Play one note: starts tone, waits for it to finish + 20 ms gap.
// Blocking on Core 1 only -- Core 0 is never touched.
static void _note(int freq, int ms) {
  tone(SPEAKER_PIN, freq, ms);
  delay(ms + 20);
}

static void _doSound(uint8_t id) {
  _aud_busy = true;

  switch (id) {

    // Boot chime: C5 → E5 → G5  (major triad, ascending)
    case SND_BOOT:
      _note(523, 120); _note(659, 120); _note(784, 220);
      break;

    // Chirp: fast frequency sweep -- friendly confirmation sound
    case SND_CHIRP:
      for (int f = 1000; f <= 1900; f += 75) { tone(SPEAKER_PIN, f, 14); delay(14); }
      noTone(SPEAKER_PIN);
      break;

    // Notify: two-note ding-dong
    case SND_NOTIFY:
      _note(1047, 90); delay(20); _note(1319, 150);
      break;

    // Alert: three urgent beeps
    case SND_ALERT:
      for (int i = 0; i < 3; i++) { _note(1600, 100); delay(60); }
      break;

    // Low battery: four slow pulses at A4
    case SND_LOW_BAT:
      for (int i = 0; i < 4; i++) { _note(440, 90); delay(200); }
      break;

    // Error / ESTOP: descending three tones
    case SND_ERROR:
      _note(600, 150); _note(450, 150); _note(300, 320);
      break;

    // Smoke alarm siren: rising/falling sweep, loops until _aud_loop cleared.
    // Checks _aud_loop and _aud_cmd on every step -- stops within ~80 ms.
    case SND_SMOKE: {
      _aud_loop = true;
      while (_aud_loop && _aud_cmd == SND_NONE) {
        for (int f = 700; f <= 2500 && _aud_loop && _aud_cmd == SND_NONE; f += 32) {
          tone(SPEAKER_PIN, f, 15); delay(15);
        }
        for (int f = 2500; f >= 700 && _aud_loop && _aud_cmd == SND_NONE; f -= 32) {
          tone(SPEAKER_PIN, f, 15); delay(15);
        }
        delay(50);
      }
      noTone(SPEAKER_PIN);
      break;
    }

    // All-clear fanfare: A4 → C5 → E5 → G5
    case SND_CLEAR:
      _note(880, 90); _note(1047, 90); _note(1319, 90); _note(1568, 250);
      break;

    // Stop: kill looping alarm immediately
    case SND_STOP:
      _aud_loop = false;
      noTone(SPEAKER_PIN);
      break;

    default: break;
  }

  _aud_busy = false;
}

// ── Core 1 entry points ──────────────────────────────────────────────────────

void audioSetup() {
  pinMode(SPEAKER_PIN, OUTPUT);
  digitalWrite(SPEAKER_PIN, LOW);
  delay(500);          // wait for Core 0 to finish its init
  _doSound(SND_BOOT);  // boot chime also confirms wiring is correct
}

void audioLoop() {
  if (_aud_cmd != SND_NONE) {
    uint8_t cmd = _aud_cmd;
    _aud_cmd    = SND_NONE;
    _doSound(cmd);
  }
  delay(5);
}

// ══════════════════════════════════════════════════════════════════════════════
//  UPGRADE PATH: WAV audio from flash (pre-recorded voice alerts)
// ══════════════════════════════════════════════════════════════════════════════
//
//  When you want actual speech ("Warning -- smoke detected") instead of tones:
//
//  Step 1  Record or TTS-generate a WAV file (Audacity: 8 kHz, 8-bit, mono).
//          Keep clips under 3 seconds so they fit comfortably in the 2 MB flash.
//
//  Step 2  Convert WAV to a C array on your PC:
//            Python: python -c "import sys; d=open('clip.wav','rb').read(); print('const uint8_t clip[]=\{'+','.join(str(b) for b in d)+'\};')" > sounds/clip.h
//            Or:     xxd -i clip.wav > sounds/clip.h
//
//  Step 3  Install the ESP8266Audio library (works on RP2040 with arduino-pico):
//            Arduino IDE → Sketch → Include Library → Manage Libraries → "ESP8266Audio"
//
//  Step 4  In your sketch:
//            #include <AudioFileSourcePROGMEM.h>
//            #include <AudioGeneratorWAV.h>
//            #include <AudioOutputPWM.h>       // RP2040 PWM audio output
//            #include "sounds/smoke_warning.h" // your converted C array
//
//            AudioOutputPWM   *pwmOut;
//            AudioGeneratorWAV *wavGen;
//            AudioFileSourcePROGMEM *src;
//
//            void playVoice(const uint8_t* data, size_t sz) {
//              src    = new AudioFileSourcePROGMEM(data, sz);
//              wavGen = new AudioGeneratorWAV();
//              pwmOut = new AudioOutputPWM(SPEAKER_PIN);
//              pwmOut->SetGain(0.8);
//              wavGen->begin(src, pwmOut);
//            }
//
//            // In loop1():
//            if (wavGen && wavGen->isRunning()) {
//              if (!wavGen->loop()) { wavGen->stop(); delete wavGen; wavGen=nullptr; }
//            }
//
//  The synthetic tones in this file and WAV playback can coexist -- just
//  guard playVoice() so it only fires when wavGen is nullptr (not already playing).
//
// ═════════════════════════════════════════════════════════════════════════════