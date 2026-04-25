// ─────────────────────────────────────────────────────────────────────────
// TFT_eSPI configuration for the Teyleten 1.28" GC9A01 round display on
// the Freenove ESP32-S3 WROOM CAM board.
//
// HOW TO USE:
//   1. Locate your TFT_eSPI library install:
//        macOS:  ~/Documents/Arduino/libraries/TFT_eSPI/
//   2. Open `User_Setup.h` (or create a new entry under `User_Setup_Select.h`).
//   3. Comment out any `#define <DRIVER>_DRIVER` already in there.
//   4. Paste the block below. Save.
//   5. Re-compile.
//
// TFT_eSPI is configured at LIBRARY-COMPILE time, not per-sketch. The pins
// here MUST match firmware/ai_companion/config.h.
// ─────────────────────────────────────────────────────────────────────────

#define USER_SETUP_INFO "ESP32-S3 AI Companion - GC9A01 1.28in"

#define GC9A01_DRIVER

#define TFT_WIDTH  240
#define TFT_HEIGHT 240

// SPI pins — match config.h on the sketch side.
#define TFT_MOSI  38
#define TFT_SCLK  39
#define TFT_DC    40
#define TFT_CS     1
#define TFT_RST    3
// No MISO / no backlight pin — Teyleten board ties BL to VCC.
#define TFT_MISO  -1

// Fonts (only what we use — keeps binary small).
#define LOAD_GLCD
#define LOAD_FONT2
#define SMOOTH_FONT

// 40 MHz is safe on most ESP32-S3 boards with short jumpers; drop to
// 27 MHz if the display flickers or shows artefacts.
#define SPI_FREQUENCY        40000000
#define SPI_READ_FREQUENCY   20000000

// ESP32-S3 must be told which SPI host to use. HSPI is typically free.
#define USE_HSPI_PORT
