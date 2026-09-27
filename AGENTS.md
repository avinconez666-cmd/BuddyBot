# Agent rules — BuddyBot

You are editing a live kid robot. Wrong UART or a blocked Mega loop is worse than a missing feature.

## Before any edit

1. Read `docs/ARCHITECTURE.md` and `docs/PROTOCOL.md`.
2. Touch **one subsystem** per task: KidsApp *or* Pico *or* Mega *or* web-kidsapp *or* ParentApp.
3. Do not start from `old_firmware/` or `android_logs.txt`.

## Hard constraints

- Baud stays 115200. Newline text protocol stays.
- Do not change Mega↔Pico pins (D18/D19 ↔ GP5/GP4) or remove the 5V divider.
- Never use Pico GP0/GP1 for user wiring.
- `bridgeLoop()` once per loop. No second serial pump.
- `MessageRouter` is the only S9 line parser. No new `onMessageReceived` consumers that parse the same prefixes.
- Mega: no `delay()` longer than a few ms on the hot path. Watchdog is 10s; motor timeout is 2s — keep both.
- Hazard / ESTOP / tilt / flame / gas / edge always win over personality and LLM.
- Do not put LLM inference on Mega or Pico.
- UNHINGED stays adult-gated on the phone; do not add it to `web-kidsapp`.
- Toddler-facing strings stay short and safe (web KidsApp already caps ~18 words).

## How to change protocol

Same commit: emitter + `MessageRouter` or Pico handler + `docs/PROTOCOL.md`.

## How to make the robot "smarter"

Map natural language to the existing command whitelist on the S9. Improve face follow / guardian / mode policy in Kotlin. Do not generate new Mega verbs from the model at runtime.

## Compile targets

- Android: `android/BuddyBot/KidsApp` Gradle debug.
- Pico: Earle Philhower **Raspberry Pi Pico W**, TFT_eSPI.
- Mega: Arduino Mega 2560, `firmware/BuddyBot_Mega_V37/BuddyBot_Mega_V37.ino` only.

## Definition of done

- States what link you used (USB vs HTTP).
- Notes if motor timeout / watchdog / bridge single-call still hold.
- No drive-by refactors, no pin remaps, no extra `#include` soup.
