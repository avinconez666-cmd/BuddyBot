// ═════════════════════════════════════════════════════════════════════════════
//  BUDDYBOT  ·  PICO W  ·  USB ↔ UART SERIAL BRIDGE
//  File : serial_bridge.h
// ═════════════════════════════════════════════════════════════════════════════
//
//  Architecture
//  ─────────────
//  S9 Android app  ──USB-C──►  Pico W  ──Serial2 (GP4/GP5)──►  Mega
//                  ◄──────────          ◄────────────────────────
//
//  The Pico sits in the middle and snoop-forwards in both directions:
//    S9 → Mega : every line from USB is forwarded to Serial2.
//                Pico also inspects it for local commands (audio, display).
//    Mega → S9 : every line from Serial2 is forwarded to USB (CRC stripped).
//                Pico also inspects it to update the dashboard and audio.
//
//  Serial mapping (arduino-pico core)
//  ────────────────────────────────────
//    Serial   = USB CDC     (S9 via hub)
//    Serial2  = UART1       (Mega,  GP4=TX → D19/RX1,  GP5=RX ← D18/TX1)
//
//  Integration -- add to your main Pico W sketch
//  ──────────────────────────────────────────────
//    #include "serial_bridge.h"
//    #include "audio_player.h"   // optional, for audio triggers
//
//    void setup() {
//      bridgeSetup();
//      // ... rest of your setup
//    }
//
//    void loop() {
//      bridgeLoop();
//      // ... rest of your loop
//    }
//
//  Callbacks -- implement these in your main sketch to receive parsed events
//  ─────────────────────────────────────────────────────────────────────────
//    void onMegaLine(const String& line);   // called for every line from Mega
//    void onS9Line(const String& line);     // called for every line from S9
//    // Define empty stubs if you don't need them:
//    //   void onMegaLine(const String&) {}
//    //   void onS9Line(const String&)   {}
//
//  Pico-only prefix
//  ─────────────────
//    S9 can send "PICO:xxx" to target the Pico exclusively (not forwarded to Mega).
//    Handled in onS9Line() in your main sketch. Everything else is forwarded.
//
//  Mega V36 requirement
//  ─────────────────────
//    processPicoCommand() in the Mega must fall back to processS9Command()
//    for unrecognised messages, so S9-format commands forwarded by the Pico
//    are handled correctly. See V36 firmware.
//
// ═════════════════════════════════════════════════════════════════════════════

#pragma once
#include <Arduino.h>

// ── Pin definitions for Serial2 (UART1 on RP2040) ────────────────────────────
#define BRIDGE_UART_TX  4   // GP4 → Mega D19 (RX1)
#define BRIDGE_UART_RX  5   // GP5 ← Mega D18 (TX1)
#define BRIDGE_BAUD  115200

// Set to 1 to echo every bridged line on USB Serial (PC diagnostics on COM port).
#ifndef BRIDGE_USB_DEBUG
#define BRIDGE_USB_DEBUG 1
#endif

// ── Forward declarations (implement these in your main sketch) ────────────────
void onMegaLine(const String& line);
void onS9Line(const String& line);

// ── Internal state ────────────────────────────────────────────────────────────
static String _bridgeS9Buf   = "";   // accumulates characters from S9 (USB)
static String _bridgeMegaBuf = "";   // accumulates characters from Mega (Serial2)

// Strip |CRC:XX suffix that the Mega appends to outgoing messages.
// The S9 app doesn't use CRC so we remove it before forwarding.
static String _stripCRC(const String& s) {
  int idx = s.indexOf("|CRC:");
  return (idx > 0) ? s.substring(0, idx) : s;
}

// S9 Android app wraps commands as "CMD:<payload>". Mega processS9Command()
// accepts both forms, but processPicoCommand() used to drop CMD:MOTOR:F lines.
// Strip the wrapper before forwarding so Mega always receives clean payloads.
static String _normalizeS9ToMega(const String& s) {
  String out = s;
  out.trim();
  if (out.startsWith("CMD:")) {
    out = out.substring(4);
    out.trim();
  }
  return out;
}

// ── Public API ────────────────────────────────────────────────────────────────

// Call once from setup().
void bridgeSetup() {
  Serial2.setTX(BRIDGE_UART_TX);
  Serial2.setRX(BRIDGE_UART_RX);
  Serial2.setFIFOSize(256);
  Serial2.begin(BRIDGE_BAUD);
  // USB CDC to S9 — must be started before bridgeLoop() reads Serial.
  Serial.begin(BRIDGE_BAUD);
}

// Send a line from the Pico itself to the Mega (e.g. PING_PICO, dashboard ACKs).
// Do NOT use Serial2.println() directly -- use this wrapper so traffic is logged.
inline void picoToMega(const String& msg) {
  Serial2.println(msg);
}

// Send a line from the Pico itself to the S9 (e.g. local status, display ACKs).
inline void picoToS9(const String& msg) {
  Serial.println(msg);
}

// ── Bridge loop -- call every iteration of loop() ─────────────────────────────
void bridgeLoop() {

  // ── S9 → Mega path ─────────────────────────────────────────────────────────
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n') {
      _bridgeS9Buf.trim();
      if (_bridgeS9Buf.length() > 0) {

        // Pico-only prefix: "PICO:xxx" is consumed here, never forwarded.
        if (_bridgeS9Buf.startsWith("PICO:")) {
          onS9Line(_bridgeS9Buf);   // let main sketch handle it
        } else {
          // Forward to Mega — strip CMD: wrapper from S9 app payloads
          String fwd = _normalizeS9ToMega(_bridgeS9Buf);
          if (fwd.length() > 0) {
            Serial2.println(fwd);
#if BRIDGE_USB_DEBUG
            Serial.print(F("[S9->M] "));
            Serial.println(fwd);
#endif
          }
          // Also let main sketch snoop (audio triggers, display commands, etc.)
          onS9Line(_bridgeS9Buf);
        }
      }
      _bridgeS9Buf = "";
    } else if (c != '\r') {
      _bridgeS9Buf += c;
      if (_bridgeS9Buf.length() > 120) _bridgeS9Buf = "";  // overflow guard
    }
  }

  // ── Mega → S9 path ─────────────────────────────────────────────────────────
  while (Serial2.available()) {
    char c = (char)Serial2.read();
    if (c == '\n') {
      _bridgeMegaBuf.trim();
      if (_bridgeMegaBuf.length() > 0) {
        // Strip CRC before forwarding to S9
        String clean = _stripCRC(_bridgeMegaBuf);
        // Forward to S9
        Serial.println(clean);
#if BRIDGE_USB_DEBUG
        Serial.print(F("[M->S9] "));
        Serial.println(clean);
#endif
        // Let main sketch process for dashboard / audio
        onMegaLine(clean);
      }
      _bridgeMegaBuf = "";
    } else if (c != '\r') {
      _bridgeMegaBuf += c;
      if (_bridgeMegaBuf.length() > 120) _bridgeMegaBuf = "";  // overflow guard
    }
  }
}