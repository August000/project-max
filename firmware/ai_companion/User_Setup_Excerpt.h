// ─────────────────────────────────────────────────────────────────────────
// TFT_eSPI configuration for the Teyleten 1.28" GC9A01 round display on
// the Freenove ESP32-S3 WROOM CAM board.
//
// HOW TO USE:
//   1. Locate your TFT_eSPI library install:
//        macOS:  ~/Documents/Arduino/libraries/TFT_eSPI/
//   2. Open `User_Setup.h`. Replace its contents with the block below.
//      (Or comment out everything in it and paste this on top.)
//   3. Save. Re-compile your sketch — TFT_eSPI is configured at LIBRARY
//      compile time, so any change here forces a rebuild.
//
// Pins MUST match firmware/ai_companion/config.h.
// ─────────────────────────────────────────────────────────────────────────

#define USER_SETUP_INFO "ESP32-S3 AI Companion - GC9A01 1.28in"

#define GC9A01_DRIVER

#define TFT_WIDTH  240
#define TFT_HEIGHT 240

// SPI pins.
#define TFT_MOSI  42    // display SDA
#define TFT_SCLK  41    // display SCL
#define TFT_DC     1
#define TFT_CS     2
#define TFT_RST   -1    // hardware: tie display RST to 3V3 (no GPIO needed)
#define TFT_MISO  -1    // GC9A01 is write-only

// Fonts (only what we use — keeps binary small).
#define LOAD_GLCD
#define LOAD_FONT2
#define SMOOTH_FONT

// 40 MHz is normally fine. Drop to 27 MHz if the display flickers,
// shows artefacts, or never inits.
#define SPI_FREQUENCY        40000000
#define SPI_READ_FREQUENCY   20000000
