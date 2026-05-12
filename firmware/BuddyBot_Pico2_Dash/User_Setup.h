/*
 * User_Setup.h  —  TFT_eSPI configuration for BuddyBot Pico 2 Dashboard
 *
 * IMPORTANT: Copy this file into your TFT_eSPI library folder and
 * REPLACE the existing User_Setup.h. The library ships with a generic
 * setup — this one is tuned for ST7796S + Pico 2 at 40MHz SPI.
 *
 * Library folder location:
 *   Windows: Documents\Arduino\libraries\TFT_eSPI\
 *   Linux:   ~/Arduino/libraries/TFT_eSPI/
 */

#define USER_SETUP_LOADED    1

// ── Driver ───────────────────────────────────────────────────────────
#define ST7796_DRIVER

// ── Physical resolution (portrait native) ────────────────────────────
// TFT_eSPI swaps these automatically when setRotation(1) is called.
#define TFT_WIDTH   320
#define TFT_HEIGHT  480

// ── SPI pins (Pico 2 SPI0) ───────────────────────────────────────────
#define TFT_MISO    16   // GP16  → SDO (MISO)
#define TFT_MOSI    19   // GP19  → SDI (MOSI)
#define TFT_SCLK    18   // GP18  → SCK
#define TFT_CS      17   // GP17  → LCD_CS
#define TFT_DC      21   // GP21  → LCD_RS (D/C)
#define TFT_RST     20   // GP20  → LCD_RST

// Backlight controlled manually via GP22 in sketch (pinMode + digitalWrite)
// #define TFT_BL   22   // uncomment only if you want TFT_eSPI to control it

// ── Fonts ─────────────────────────────────────────────────────────────
#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT6
#define LOAD_FONT7
#define LOAD_FONT8
#define LOAD_GFXFF
#define SMOOTH_FONT

// Suppress TFT_eSPI resistive touch warning — we use FT6336U via I2C, not SPI touch
#define TOUCH_CS -1

// ── SPI speed ─────────────────────────────────────────────────────────
// 40MHz is the ST7796S rated maximum. Drop to 27MHz if you see artefacts.
#define SPI_FREQUENCY        20000000  // Reduced from 40MHz — increase once display confirmed working
#define SPI_READ_FREQUENCY   20000000
