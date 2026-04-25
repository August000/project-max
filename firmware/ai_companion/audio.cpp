#include "audio.h"
#include "config.h"

// Use the legacy I2S driver. It ships with both Arduino-ESP32 v2.x and v3.x,
// so this firmware compiles on either core.
#include <driver/i2s.h>
#include <freertos/stream_buffer.h>

static StreamBufferHandle_t playback_buf = nullptr;

static void initRx() {
    i2s_config_t cfg = {};
    cfg.mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX);
    cfg.sample_rate          = MIC_SAMPLE_RATE;
    cfg.bits_per_sample      = I2S_BITS_PER_SAMPLE_32BIT;     // INMP441 is 24-bit upper-aligned in 32
    cfg.channel_format       = I2S_CHANNEL_FMT_ONLY_LEFT;     // L/R tied to GND
    cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    cfg.intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1;
    cfg.dma_buf_count        = 6;
    cfg.dma_buf_len          = 240;                            // ~15 ms @ 16 kHz
    cfg.use_apll             = false;
    cfg.tx_desc_auto_clear   = false;
    cfg.fixed_mclk           = 0;

    i2s_pin_config_t pins = {};
    pins.mck_io_num   = I2S_PIN_NO_CHANGE;
    pins.bck_io_num   = MIC_SCK_PIN;
    pins.ws_io_num    = MIC_WS_PIN;
    pins.data_out_num = I2S_PIN_NO_CHANGE;
    pins.data_in_num  = MIC_SD_PIN;

    if (i2s_driver_install(I2S_NUM_0, &cfg, 0, nullptr) != ESP_OK) {
        Serial.println("[audio] mic i2s_driver_install failed");
        return;
    }
    i2s_set_pin(I2S_NUM_0, &pins);
    i2s_start(I2S_NUM_0);
}

static void initTx() {
    i2s_config_t cfg = {};
    cfg.mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
    cfg.sample_rate          = SPK_SAMPLE_RATE;
    cfg.bits_per_sample      = I2S_BITS_PER_SAMPLE_16BIT;
    cfg.channel_format       = I2S_CHANNEL_FMT_ONLY_LEFT;
    cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    cfg.intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1;
    cfg.dma_buf_count        = 8;
    cfg.dma_buf_len          = 480;                            // ~20 ms @ 24 kHz
    cfg.use_apll             = false;
    cfg.tx_desc_auto_clear   = true;
    cfg.fixed_mclk           = 0;

    i2s_pin_config_t pins = {};
    pins.mck_io_num   = I2S_PIN_NO_CHANGE;
    pins.bck_io_num   = SPK_BCLK_PIN;
    pins.ws_io_num    = SPK_LRC_PIN;
    pins.data_out_num = SPK_DIN_PIN;
    pins.data_in_num  = I2S_PIN_NO_CHANGE;

    if (i2s_driver_install(I2S_NUM_1, &cfg, 0, nullptr) != ESP_OK) {
        Serial.println("[audio] spk i2s_driver_install failed");
        return;
    }
    i2s_set_pin(I2S_NUM_1, &pins);
    i2s_start(I2S_NUM_1);
}

static void playbackTask(void *) {
    uint8_t buf[1024];
    while (true) {
        size_t n = xStreamBufferReceive(playback_buf, buf, sizeof(buf), portMAX_DELAY);
        if (n == 0) continue;
        size_t written = 0;
        i2s_write(I2S_NUM_1, buf, n, &written, pdMS_TO_TICKS(200));
    }
}

void audioInit() {
    initRx();
    initTx();
    playback_buf = xStreamBufferCreate(96 * 1024, 256);   // ~2 s @ 24 kHz
    xTaskCreatePinnedToCore(playbackTask, "playback", 4096, nullptr, 5, nullptr, 1);
}

size_t micRead(int16_t *out, size_t max_samples) {
    static int32_t raw[512];
    if (max_samples > 512) max_samples = 512;
    size_t bytes_read = 0;
    if (i2s_read(I2S_NUM_0, raw, max_samples * sizeof(int32_t),
                 &bytes_read, pdMS_TO_TICKS(40)) != ESP_OK) {
        return 0;
    }
    size_t n = bytes_read / sizeof(int32_t);
    for (size_t i = 0; i < n; i++) {
        int32_t s = raw[i] >> MIC_GAIN_SHIFT;
        if (s >  32767) s =  32767;
        if (s < -32768) s = -32768;
        out[i] = (int16_t)s;
    }
    return n;
}

void playbackPush(const uint8_t *pcm, size_t len) {
    if (!playback_buf || !len) return;
    xStreamBufferSend(playback_buf, pcm, len, 0);
}

void playbackFlush() {
    if (playback_buf) xStreamBufferReset(playback_buf);
    // Zero the DMA buffer so any audio still mid-flight goes silent.
    i2s_zero_dma_buffer(I2S_NUM_1);
}
