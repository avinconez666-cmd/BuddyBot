# BuddyBot Kids — Web companion

Browser KidsApp for AJ. Same job as `android/BuddyBot/KidsApp/` when the S9 is not in your hands: talk to Buddy, switch personalities, play the six Pico games, watch a sensor HUD, and practice Call Daddy / SOS.

This is **kid-safe**. Unhinged mode stays on the phone app for adults.

## What it mirrors

| Android KidsApp | Web KidsApp |
|---|---|
| AI router (Groq → Gemini → Claude → offline) | Grok with the same toddler prompt, 18-word cap, offline fallback |
| Face videos on the S9 | Animated visor face (idle / listen / speak / alert, plus Dog / Guard / Party) |
| NORMAL / DOG / BODYGUARD / PARTY | Same four modes |
| Pico games | Buddy Run, Maze Munch, Starship, Matrix Memory, Color Match, Math Blast |
| Mega V37 telemetry | Live simulated HUD (DHT11, MQ-2, flame, PIR, tilt, 4-zone ultrasonics, compass, GPS, battery) |
| Call Daddy + SOS | Practice call + SOS overlay |
| First-meeting intro | Name gate, default AJ |

Hardware (USB UVC, Mega motors, wake word) is not in the browser. Sensors are simulated so the HUD still feels like the robot.

## Source

The running web app lives in the Grok App Builder workspace (TanStack Start + React). Product files in this folder:

- `src/lib/personalities.ts` — kid-safe system prompts
- `src/lib/buddy-chat.ts` — Grok server function
- `src/lib/types.ts` — modes, telemetry, games

Name, parent label, voice toggle, and high scores stay in `localStorage`.

Built with ❤️ for AJ.
