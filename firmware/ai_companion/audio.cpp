#include "audio.h"
#include "config.h"

#include <driver/i2s_std.h>
#include <freertos/stream_buffer.h>

#if ESP_ARDUINO_VERSION_MAJOR < 3
#error "This firmware requires Arduino-ESP32 v3.0.0+ (uses the new I2S driver)."
#endif

static i2s_chan_handle_t rx_handle = nullptr;
static i2s_chan_handle_t tx_handle = nullptr;
static StreamBufferHandle_t playback_buf = nullptr;

static void initRx() {
    i2s_chan_config_t cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    cfg.dma_desc_num  = 6;
    cfg.dma_frame_num = 240;   // ~15 ms at 16 kHz
    cfg.auto_clear    = true;
    ESP_ERROR_CHECK(i2s_new_channel(&cfg, nullptr, &rx_handle));

    i2s_std_config_t std = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(MIC_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = (gpio_num_t)MIC_SCK_PIN,
            .ws   = (gpio_num_t)MIC_WS_PIN,
            .dout = I2S_GPIO_UNUSED,
            .din  = (gpio_num_t)MIC_SD_PIN,
            .invert_flags = {false, false, false},
        },
    };
    std.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;   // INMP441 with L/R tied to GND
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx_handle, &std));
    ESP_ERROR_CHECK(i2s_channel_enable(rx_handle));
}

static void initTx() {
    i2s_chan_config_t cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    cfg.dma_desc_num  = 8;
    cfg.dma_frame_num = 480;   // ~20 ms at 24 kHz
    cfg.auto_clear    = true;
    ESP_ERROR_CHECK(i2s_new_channel(&cfg, &tx_handle, nullptr));

    i2s_std_config_t std = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SPK_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = (gpio_num_t)SPK_BCLK_PIN,
            .ws   = (gpio_num_t)SPK_LRC_PIN,
            .dout = (gpio_num_t)SPK_DIN_PIN,
            .din  = I2S_GPIO_UNUSED,
            .invert_flags = {false, false, false},
        },
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_handle, &std));
    ESP_ERROR_CHECK(i2s_channel_enable(tx_handle));
}

static void playbackTask(void *) {
    uint8_t buf[1024];
    while (true) {
        size_t n = xStreamBufferReceive(playback_buf, buf, sizeof(buf), portMAX_DELAY);
        if (n == 0) continue;
        size_t written = 0;
        i2s_channel_write(tx_handle, buf, n, &written, pdMS_TO_TICKS(200));
    }
}

void audioInit() {
    initRx();
    initTx();
    playback_buf = xStreamBufferCreate(96 * 1024, 256);   // ~2s @ 24 kHz
    xTaskCreatePinnedToCore(playbackTask, "playback", 4096, nullptr, 5, nullptr, 1);
}

size_t micRead(int16_t *out, size_t max_samples) {
    static int32_t raw[512];
    if (max_samples > 512) max_samples = 512;
    size_t bytes_read = 0;
    if (i2s_channel_read(rx_handle, raw, max_samples * sizeof(int32_t),
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
    if (tx_handle) {
        i2s_channel_disable(tx_handle);
        i2s_channel_enable(tx_handle);
    }
}
