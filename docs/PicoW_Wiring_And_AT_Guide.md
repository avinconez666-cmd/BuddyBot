# BuddyBot — Pico W-2023 Consolidation
## Wiring Table + AT Firmware Guide
### Replacing: ArtronShop RP2 Nano + Keyestudio ESP32
### With: Single RP2040 Pico W-2023

---

## 1. What Changed

| Before | After |
|---|---|
| RP2 Nano (RP2040) — HMI/TFT dashboard | Pico W-2023 — HMI/TFT dashboard (Core 0) |
| Keyestudio ESP32 — WiFi web bridge | Pico W-2023 — WiFi via onboard ESP8285 (Core 1) |
| Mega Serial1 → RP2 Nano GP0/GP1 | Mega Serial1 → Pico W-2023 GP5/GP4 |
| Mega Serial3 → ESP32 GPIO16/17 | Serial3 freed → GPS NEO-6M (hardware UART) |
| GPS on SoftwareSerial D10/D11 | GPS on hardware Serial3 D14/D15 |

---

## 2. Full Wiring Table (Pico W-2023 ↔ Mega)

### UART — Mega Serial1 → Pico W-2023 UART1 (GP4/GP5)
> ⚠️ GP0 and GP1 are INTERNAL to ESP8285. Never wire to them.

| Mega Pin | Wire | Pico W-2023 Pin | Note |
|---|---|---|---|
| D18 (TX1) | → 1 kΩ → 2 kΩ → GND | GP5 (RX, UART1) | 5V→3.3V divider: 1kΩ series then 2kΩ to GND |
| D19 (RX1) | ← direct | GP4 (TX, UART1) | 3.3V output — safe for Mega RX input |
| GND | — | GND | Common ground |

Voltage divider on Mega TX1 (D18) side:
```
D18 ──[1kΩ]──┬── GP5
              │
           [2kΩ]
              │
             GND
```

### SPI — TFT Display ST7796S (UNCHANGED)

| Pico W-2023 Pin | TFT Pin | Note |
|---|---|---|
| GP16 | SDO (MISO) | SPI0 RX |
| GP17 | LCD_CS | Chip select |
| GP18 | SCK | SPI0 clock |
| GP19 | SDI (MOSI) | SPI0 TX |
| GP20 | LCD_RST | Reset |
| GP21 | LCD_RS (D/C) | Data/Command |
| GP22 | LED (backlight) | GPIO high = on |
| 3V3 | VCC | 3.3V supply |
| GND | GND | |

### I2C1 — FT6336U Touch (UNCHANGED)

| Pico W-2023 Pin | FT6336U Pin |
|---|---|
| GP26 (SDA1) | SDA |
| GP27 (SCL1) | SCL |
| GP28 | INT (interrupt, optional — polling mode used) |
| GP15 | RST |
| 3V3 | VCC |
| GND | GND |

### Audio — SC8002B Amplifier (UNCHANGED)

| Pico W-2023 Pin | SC8002B Pin | Note |
|---|---|---|
| GP14 | IN (via 100Ω + 100nF filter) | PWM audio signal |
| VBUS (pin 40) | VCC | Must be 5V — not 3.3V |
| GND | GND | |

RC filter: GP14 ──[100Ω]──┬── SC8002B IN
                            [100nF]
                            GND

---

## 3. GPS Wiring Change (SoftwareSerial D10/D11 → Hardware Serial3 D14/D15)

| GPS Module Pin | Old Wire | New Wire |
|---|---|---|
| TX (GPS output) | → D10 (was SS RX) | → D15 (RX3) — direct, 3.3V OK |
| RX (GPS input) | ← D11 (was SS TX) | ← D14 (TX3) via 1kΩ resistor |
| VCC | 3.3V or 5V (check module) | unchanged |
| GND | GND | unchanged |

> Note: Mega D14 (TX3) outputs 5V. Most GPS modules (NEO-6M) have
> 5V-tolerant RX, but the 1kΩ series resistor provides extra protection.

---

## 4. Remove Entirely

- All ESP32 wiring: D14, D15, 5V, GND, 1kΩ resistor
- The RP2 Nano board itself
- The ESP32 board itself

---

## 5. Board Selection in Arduino IDE

| Board | Setting |
|---|---|
| Pico W-2023 | **Raspberry Pi Pico W** (Earle Philhower arduino-pico core) |
| NOT | ArtronShop RP2 Nano (that was the old board) |
| NOT | Generic RP2040 / standard Pico |

Install core: Arduino IDE → Boards Manager → search "pico" → install
"Raspberry Pi RP2040 Boards" by Earle Philhower.

---

## 6. Libraries Required (Pico W sketch)

| Library | Version | Install via |
|---|---|---|
| TFT_eSPI | ≥ 2.5.43 | Library Manager |
| WiFiEspAT | ≥ 1.4.1 | Library Manager (search "WiFiEspAT") |

WiFiEspAT by Juraj Andrássy — not to be confused with WiFiEsp or ESP8266WiFi.
Search exactly: **WiFiEspAT**

---

## 7. ⚠️ AT Firmware Version Check (CRITICAL — do this before flashing)

The WiFiEspAT library requires AT firmware **≥ 1.7.4** on the ESP8285.

### Step 1 — Check current version

1. Connect Pico W-2023 USB to PC
2. Open Arduino Serial Monitor at **115200 baud**
3. Type exactly: `AT+GMR` and press Enter (or use the send field)
4. You will see something like:
   ```
   AT version:1.7.5.0(Jul 10 2020 14:00:00)
   SDK version:3.0.4(9532ceb)
   ```
5. If AT version is **≥ 1.7.4** → you are good. Skip to Step 3.
6. If AT version is **< 1.7.4** → proceed to Step 2.

### Step 2 — Update AT firmware (only if needed)

The ESP8285 on this board is connected to GP0/GP1 of the RP2040.
To flash it, you need to set up a UART passthrough sketch on the Pico W.

**Passthrough sketch** (flash this temporarily):
```cpp
void setup() {
  Serial.begin(115200);   // USB to PC
  Serial1.setTX(0);
  Serial1.setRX(1);
  Serial1.begin(115200);  // RP2040 → ESP8285
}
void loop() {
  if (Serial.available())  Serial1.write(Serial.read());
  if (Serial1.available()) Serial.write(Serial1.read());
}
```

Then use esptool.py or ESP Flash Download Tool to flash the ESP8285
AT firmware. Download from Espressif:
https://github.com/espressif/ESP8266_NONOS_SDK/releases

Flash the `AT_x.x.x.x.bin` image (non-OTA, 1MB flash for ESP8285).

### Step 3 — Confirm WiFiEspAT connectivity

After flashing your Pico W-2023 sketch, open Serial Monitor (Pico USB)
at 115200 and watch for:
```
[PICO] Ready.
```
If WiFi connects successfully, you will see T.espok = true on the
COMMS screen (WIFI (PICO W) showing green / CONNECTED).

---

## 8. credentials

Set your WiFi credentials at the top of BuddyBot_PicoW_Dash.ino:
```cpp
const char* WIFI_SSID = "YOUR_SSID";
const char* WIFI_PASS = "YOUR_PASS";
```

Web dashboard available at: http://<PicoW_IP>  (find IP from your
router DHCP table, or check Serial Monitor on Pico USB after boot).

---

## 9. Sketch → Board Map (V32 system)

| Board | Sketch | Upload via |
|---|---|---|
| Keyestudio Mega 2560 Plus WiFi | BuddyBot_Mega_V32 | USB direct |
| RP2040 Pico W-2023 | BuddyBot_PicoW_Dash | USB-C (hold BOOTSEL if needed) |
| Arduino Uno R3 + Motor Shield | BuddyBot_R3_Motors_V2 | USB direct |

R3 sketch is unchanged.

---