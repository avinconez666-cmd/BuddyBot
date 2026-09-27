# BuddyBot Protocol (Mega V37 + Pico bridge + S9)

Text protocol. Baud **115200** everywhere. Lines end `\n` (`\r` ignored). Max useful line ~120 chars on the Pico bridge (overflow clears the buffer).

Canonical parsers:

- Pico: `firmware/BuddyBot_PicoW_Dash_V1.2/serial_bridge.h`
- S9: `android/BuddyBot/KidsApp/.../MessageRouter.kt`

If you change a prefix, change **both** plus this file in the same commit.

## Link roles

| Direction | Raw path | Transform |
|---|---|---|
| S9 → Mega | USB CDC → Pico Serial → Serial2 | Strip leading `CMD:` if present. `PICO:*` is **not** forwarded |
| Mega → S9 | Serial1 → Pico Serial2 → USB | Strip `\|CRC:XX` suffix before S9 |
| Pico → Mega | `picoToMega()` | Use wrapper, not raw `Serial2.println` from random files |
| Pico → S9 | `picoToS9()` | Local status / display ACK |

S9 may wrap payloads as `CMD:<payload>`. Mega should accept both wrapped and bare forms. Bridge already unwraps.

## Command classes (S9 / Pico → Mega)

Keep new motion verbs in the existing families. Prefer extending a prefix over inventing a parallel language.

Documented / expected families (confirm exact tokens in `BuddyBot_Mega_V37.ino` before adding aliases):

| Family | Example intent | Notes |
|---|---|---|
| Motor | `MOTOR:F` / `MOTOR:B` / `MOTOR:L` / `MOTOR:R` / `MOTOR:S` | Mega 2 s timeout stops motion if stream dies |
| Mode | `MODE:NORMAL` etc. | NORMAL DOG BODYGUARD UNHINGED PARTY |
| Pico-local | `PICO:...` | Display, audio, games. Never hits Mega |
| Ping | `PING_PICO` / Mega ping variants | Bridge comments mention Pico pings |

**Do not** send free-form English to the Mega. LLM output must map to a whitelist on the S9.

Suggested S9 whitelist (extend in Kotlin, not in firmware first):

```
STOP, MOTOR:F, MOTOR:B, MOTOR:L, MOTOR:R,
MODE:NORMAL, MODE:DOG, MODE:BODYGUARD, MODE:PARTY,
PICO:BEEP, PICO:GAME:...
```

UNHINGED only from an adult-gated path on the phone.

## Telemetry (Mega → S9) — MessageRouter contract

### `STAT:` (10 fields, colon-separated)

```
STAT:<gas>:<temp>:<hum>:<haz>:<pir>:<tilt>:<flame>:<volt>:<pct>:<amps>
```

Booleans are `1` / `0`. Fewer than 10 fields = drop the line.

### `US:` ultrasonics

```
US:<front>,<rear>,<left>,<right>
```

`-1` = sensor offline.

### `STATUS|` key:value pipe list

Keys used by the app:

`ESTOP`, `AUTO`, `BAT`, `PCT`, `HTEMP`, `FANS`, `UV`, `S9`, `FW`, `CHG`, `ADOCK`, `DOCKST`

`ESTOP` true if `YES` or `1`. `UV` on if `ON`.

`FANS` subfields: `HB:`, `HE:`, `BD:` (ints).

### `HDG:`

```
HDG:<degrees float>
```

### `TELE:`

```
TELE:<volt>,<pct>,<moving 0|1>
```

### `DIAG|`

Pipes including `GPS:<lat>,<lon>`, `SAT:<n>`, `UPT:<seconds>s`.

### `IR:`

```
IR:<a>,<b>
```

Alert if either field is `1`.

### Mode / control

| Line | Meaning |
|---|---|
| `MODE:<NAME>` | Mega reports active mode |
| `REQ_MODE:<NAME>` | Mega/Pico asks S9 to switch face/persona |
| `ACK|<payload>|END` | Command ack; app strips `ACK|` and `|END` |
| `ALERT:<code>` | Critical |
| `EVENT:<code>` | Info |
| `GESTURE:<name>` | APDS / gesture |
| `SYSTEM|READY|<fw>|END` | Boot banner |
| `WIFI_IP:<ip>` | Pico/STA address |
| `PONG_PICO:` | Ignored by router (link alive) |
| `DBG:` | Log only |
| `BEEP:` | Consumed / ignored at router |

Unknown prefixes: verbose log, do not crash.

## CRC

Mega may append `|CRC:XX`. Pico strips it. S9 parser does **not** validate CRC. If you add CRC checks, do it on Pico or Mega, not by breaking old lines.

## Bridge overflow and flood rules

- Line buffer > 120 chars → discard.
- Call `bridgeLoop()` **once** per Pico `loop()`.
- Do not enable extra USB debug prints on the hot path unless diagnosing; `BRIDGE_USB_DEBUG` already echoes `[S9->M]` / `[M->S9]`.

## Adding a new message (checklist)

1. Write one example line in this file.
2. Emit from exactly one board.
3. Parse in `MessageRouter` **or** Pico `onMegaLine` / `onS9Line`, not both unless snooping.
4. Keep it under 120 chars.
5. Hazard / stop semantics stay on Mega.
6. Update KidsApp HUD only if a human needs to see it.

## HTTP fallback (Pico :80)

Used when USB CDC is down. Treat as **lossy control**, same command strings in query/body as serial payloads. Do not invent a second JSON dialect without an adapter in `ArduinoComms.kt`.

## What not to do

- Binary frames, protobuf, or different baud "just for one sensor"
- Second parser beside `MessageRouter` on the S9
- Forwarding `PICO:` to Mega
- Leaving `CMD:` on the wire *and* also parsing `CMD:CMD:`
- Blocking Mega `loop()` for TFT, WiFi, or String concatenation sprees
