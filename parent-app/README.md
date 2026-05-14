# BuddyBot Parent App

React component — sci-fi HUD dashboard for monitoring and controlling BuddyBot.

## Features
- **BUDDYBOT** + **REINSMA INNOVATIONS GENERATIVE AI** branding
- Starfield animated background
- Live camera feed with face detection targeting reticle + scan line overlay
- Real-time proximity radar with animated sweep
- Environmental sensor bars (gas, temp, humidity, flame, battery)
- Full movement control pad (hold to drive, release to stop)
- Speed selector (SLOW / NORMAL / FAST)
- Mode commands (AUTO, DANCE, PATROL, DOG GUARD)
- E-STOP button always visible
- Status chips (R3, ESP32, S9, CAM)
- Alert toasts for flame / gas / battery events
- Settings modal for ESP32 IP + camera stream URL

## Setup
1. Open in any React environment (Claude artifact, CodeSandbox, Vite, etc.)
2. Click ⚙ Settings
3. Enter your ESP32 IP address (found from Serial Monitor on boot)
4. Enter camera stream URL (IP Webcam app on S9, port 8080)
5. App polls /status every 900ms and sends commands via /cmd

## Camera Stream
Install "IP Webcam" on the S9. Start server, note the URL shown.
Enter it in Settings as: http://<s9-ip>:8080/video
