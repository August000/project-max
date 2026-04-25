// ESP32-S3 AI companion firmware.
//
// Streams microphone audio to the server over a Wi-Fi WebSocket and plays
// back TTS audio that comes back. Uploads a camera frame on demand whenever
// the agent calls the `take_picture` tool. Renders robot eyes on the
// 1.28" GC9A01 TFT, driven by `{"type":"emotion","value":...}` messages.
//
// Required Arduino-ESP32 core: 2.0.x or 3.x (uses the legacy I2S driver).
// Required libraries:
//   - WebSockets (Markus Sattler / Links2004)   — Library Manager
//   - ArduinoJson (Benoit Blanchon, v7+)        — Library Manager
//   - TFT_eSPI (Bodmer)                          — Library Manager
//
// TFT_eSPI must be configured for the GC9A01 — see User_Setup_Excerpt.h
// and copy that block into your TFT_eSPI install's User_Setup.h.

#include <Arduino.h>
#include "config.h"
#include "audio.h"
#include "camera.h"
#include "network.h"
#include "faces.h"

static const size_t CHUNK_SAMPLES = 320;   // 20 ms at 16 kHz
static int16_t pcm_chunk[CHUNK_SAMPLES];

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println("\n[boot] ai_companion starting");

    facesInit();          // start TFT face task on core 1
    setEmotionByName("sleepy");

    if (!cameraInit()) {
        Serial.println("[boot] camera init failed (continuing without vision)");
    }
    audioInit();
    wifiConnect();
    wsBegin();

    setEmotionByName("neutral");
    Serial.println("[boot] ready");
}

void loop() {
    wsLoop();

    size_t n = micRead(pcm_chunk, CHUNK_SAMPLES);
    if (n > 0 && !isMicMuted()) {
        wsSendAudio(reinterpret_cast<const uint8_t *>(pcm_chunk), n * sizeof(int16_t));
    }
}
