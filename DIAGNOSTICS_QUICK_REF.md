# 🚀 BuddyBot System Diagnostics Quick Reference

## Monitor Real-Time Serial Communication

### Terminal 1: Kotlin/S9 Side
```bash
adb logcat ArduinoComms BuddyBotMainActivity -v time | grep -E "\[SEND\]|\[RECV\]|\[ERROR\]|\[GESTURE\]|\[AI\]|\[TTS\]"
```

**Expected Output:**
```
04-07 14:32:15.124 [SEND] USB → Mega: MOTOR:F (7 bytes)
04-07 14:32:15.245 [RECV] USB ← Mega: ACK|MOTOR:F|END (raw length: 16)
04-07 14:32:16.050 [GESTURE] Detected: UP
04-07 14:32:16.100 [AI] Claude response (12 words) → limited to (12 words): Great job AJ!
04-07 14:32:16.150 [TTS] Using LOCAL TTS: Moving forward
```

### Terminal 2: Arduino/Mega Side
```
Serial Monitor (baud 115200)
```

**Expected Output:**
```
[INIT] BuddyBot Mega V29 Starting...
[RECV] S9 Command: MOTOR:F
[SEND] S9 Response: ACK|MOTOR:F|END
[WARN] S9 buffer overflow: <garbled_data>
```

---

## Quick Diagnostic Commands

### Test Serial Connection
```bash
# Check if device is connected
adb devices

# Restart ADB serially
adb kill-server && adb start-server

# Watch all serial diagnostics
adb logcat | grep -i "arduino\|serial"
```

### Test Motor Command
```bash
# Trigger forward motion (test serial bridge)
adb shell am startservice \
  -a com.buddybot.kids.MOTOR_FORWARD

# View motor response
adb logcat | grep "ACK|MOTOR"
```

### Test AI Response
```bash
# Send test query
adb shell am startservice \
  -a com.buddybot.kids.AI_QUERY \
  --es query "Tell me a joke"

# View response with word count
adb logcat | grep "\[AI\].*Claude\|Gemini"
```

### Test Gesture Handling
```bash
# Manually inject GESTURE message (DEBUG ONLY)
adb shell am broadcast \
  -a com.buddybot.kids.TEST_GESTURE \
  --es gesture UP

# View gesture processing
adb logcat | grep "\[GESTURE\]"
```

---

## System Health Checklist (Daily)

### Morning Startup
- [ ] S9 and Mega connect (USB shows steady)
- [ ] First command round-trip completes without errors
- [ ] `[SEND]` and `[RECV]` logs appear paired
- [ ] No buffer overflow warnings

### Call Daddy Feature
- [ ] Button appears in UI
- [ ] Tap triggers Messenger (or phone dialer)
- [ ] "Hi Daddy!" spoken before placing call
- [ ] Face hidden during call
- [ ] "Bye bye Daddy!" spoken when returning

### AI Interaction
- [ ] Ask "What's your name?"
- [ ] Response word count ≤ 15 words
- [ ] Voice is ElevenLabs (premium quality)
- [ ] Example response: "I'm BuddyBot, your friend!"

### Motor Commands
- [ ] Say "Move forward"
- [ ] Verify LOCAL TTS plays instantly
- [ ] Motor responds within 500ms
- [ ] Logcat shows `[SEND] MOTOR:F` and `[RECV] ACK|MOTOR:F|END`

### Sensors
- [ ] Obstacle detection triggers warning
- [ ] Battery monitor updates every 5 seconds
- [ ] Gesture detection works (hand waving)
- [ ] Ultrasonic readings accurate (< 300cm)

---

## Common Issues & Fixes

| Issue | Symptoms | Fix |
|-------|----------|-----|
| Serial Timeout | Mega unresponsive, logcat: `[ERROR] sendCommand ignored` | Check USB cable, restart ADB, check baud rate (115200) |
| Buffer Overflow | Logcat: `[WARN] S9 buffer overflow` | Reduce command frequency, check for malformed commands (missing `\n`) |
| AI No Response | Logcat: "Claude failed", "Gemini failed", "Offline mode" | Check API keys in `BuddyBotConfig.kt`, verify network, check request format |
| TTS Silent | Speaktext called but no audio | Check Android TTS engine installed, verify ElevenLabs key, check device volume |
| Gesture Not Detected | Mega: `checkGestures()` returns 0 | Verify PAJ7620 I2C connection, check gesture threshold, test with hand motion |
| Call Not Triggering | "Call Daddy" button does nothing | Verify Messenger installed, check `DADDY_MESSENGER_ID` is set, verify Facebook permissions |

---

## Log Levels

### Info (✓ Safe)
- `[SEND]` - Command sent successfully
- `[RECV]` - Response received successfully
- `[TTS] Using LOCAL TTS` - Operational phrase
- `[TTS] Using ELEVENLABS` - AI response
- `[GESTURE] Detected:` - Gesture recognized

### Warning (⚠ Monitor)
- `[WARN] S9 buffer overflow` - Serial data accumulation
- `[ERROR] sendCommand ignored (disconnected)` - USB not ready
- `WebSocket failure` - IP connection issue

### Error (🔴 Fix Needed)
- `Failed to parse Arduino msg` - Message format error
- `Claude failed` - API error
- `Failed to open serial port` - USB permission missing

---

## Performance Targets

| Metric | Target | Current |
|--------|--------|---------|
| Serial Latency | < 100ms | ─ |
| Motor Response | < 500ms | ─ |
| AI Response Time | < 3s | ─ |
| Local TTS Latency | < 200ms | ─ |
| Gesture Detection | < 1s | ─ |
| Battery Update Freq | 5s | ─ |
| ElevenLabs Credit Usage | 60-70% reduction | ─ |

---

## Debug Builds vs Release

### Debug Build (Development)
```
./gradlew build assembleDebug
```
- Full logcat output
- Extended timeouts
- Diagnostic logging enabled
- Use for testing

### Release Build (Production)
```
./gradlew build assembleRelease
```
- Minimal logging
- Optimized timeouts
- ProGuard obfuscation
- Use for deployment

---

**Last Updated:** April 7, 2026  
**Status:** ✅ Ready for Integration Testing
