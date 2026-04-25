#include "camera.h"
#include "config.h"
#include "network.h"

#include "esp_camera.h"

bool cameraInit() {
    camera_config_t c = {};
    c.ledc_channel = LEDC_CHANNEL_0;
    c.ledc_timer   = LEDC_TIMER_0;
    c.pin_d0       = CAM_PIN_D0;
    c.pin_d1       = CAM_PIN_D1;
    c.pin_d2       = CAM_PIN_D2;
    c.pin_d3       = CAM_PIN_D3;
    c.pin_d4       = CAM_PIN_D4;
    c.pin_d5       = CAM_PIN_D5;
    c.pin_d6       = CAM_PIN_D6;
    c.pin_d7       = CAM_PIN_D7;
    c.pin_xclk     = CAM_PIN_XCLK;
    c.pin_pclk     = CAM_PIN_PCLK;
    c.pin_vsync    = CAM_PIN_VSYNC;
    c.pin_href     = CAM_PIN_HREF;
    c.pin_sccb_sda = CAM_PIN_SIOD;
    c.pin_sccb_scl = CAM_PIN_SIOC;
    c.pin_pwdn     = CAM_PIN_PWDN;
    c.pin_reset    = CAM_PIN_RESET;
    c.xclk_freq_hz = 20000000;
    c.pixel_format = PIXFORMAT_JPEG;
    c.frame_size   = FRAMESIZE_VGA;        // 640x480 — small enough to upload fast
    c.jpeg_quality = 12;                    // lower = better quality
    c.fb_count     = 2;
    c.fb_location  = CAMERA_FB_IN_PSRAM;
    c.grab_mode    = CAMERA_GRAB_LATEST;

    esp_err_t err = esp_camera_init(&c);
    if (err != ESP_OK) {
        Serial.printf("[cam] init failed: 0x%x\n", err);
        return false;
    }
    return true;
}

bool captureAndUpload() {
    // Drop the first frame (often stale) then grab a fresh one.
    camera_fb_t *fb = esp_camera_fb_get();
    if (fb) esp_camera_fb_return(fb);
    fb = esp_camera_fb_get();
    if (!fb) {
        Serial.println("[cam] capture failed");
        return false;
    }
    bool ok = networkUploadImage(fb->buf, fb->len);
    esp_camera_fb_return(fb);
    return ok;
}
