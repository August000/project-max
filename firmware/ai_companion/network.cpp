#include "network.h"
#include "config.h"
#include "audio.h"
#include "camera.h"
#include "faces.h"

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>

static WebSocketsClient ws;
static SemaphoreHandle_t ws_send_mtx = nullptr;
static volatile bool mic_muted = false;

void setMicMuted(bool m) { mic_muted = m; }
bool isMicMuted() { return mic_muted; }

static void handleControlJson(const uint8_t *payload, size_t length) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload, length);
    if (err) {
        Serial.printf("[ws] bad JSON: %s\n", err.c_str());
        return;
    }
    const char *type = doc["type"] | (const char *)nullptr;
    if (!type) return;

    if (strcmp(type, "interrupt") == 0) {
        playbackFlush();
        setEmotionByName("neutral");
    } else if (strcmp(type, "request_image") == 0) {
        setMicMuted(true);
        captureAndUpload();
        setMicMuted(false);
    } else if (strcmp(type, "emotion") == 0) {
        const char *value = doc["value"] | (const char *)nullptr;
        if (value) setEmotionByName(value);
    }
}

static void onWsEvent(WStype_t type, uint8_t *payload, size_t length) {
    switch (type) {
        case WStype_CONNECTED:
            Serial.println("[ws] connected");
            break;
        case WStype_DISCONNECTED:
            Serial.println("[ws] disconnected");
            playbackFlush();
            setEmotionByName("neutral");
            break;
        case WStype_BIN:
            // PCM TTS audio chunk (24 kHz from Gemini).
            playbackPush(payload, length);
            break;
        case WStype_TEXT:
            handleControlJson(payload, length);
            break;
        case WStype_ERROR:
            Serial.println("[ws] error");
            break;
        default: break;
    }
}

void wifiConnect() {
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.print("[wifi] connecting");
    while (WiFi.status() != WL_CONNECTED) {
        delay(250);
        Serial.print(".");
    }
    Serial.printf("\n[wifi] ip=%s\n", WiFi.localIP().toString().c_str());
}

void wsBegin() {
    ws_send_mtx = xSemaphoreCreateMutex();
    String path = String("/ws/esp32/") + DEVICE_ID + "?key=" + DEVICE_SHARED_SECRET;
    Serial.printf("[ws] connecting to %s://%s:%d%s\n",
                  SERVER_USE_TLS ? "wss" : "ws", SERVER_HOST, SERVER_PORT, path.c_str());
    if (SERVER_USE_TLS) {
        // Empty fingerprint = no validation; CF terminates TLS at the edge.
        // Replace with beginSslWithCA() to pin.
        ws.beginSSL(SERVER_HOST, SERVER_PORT, path.c_str(), "", "arduino");
    } else {
        ws.begin(SERVER_HOST, SERVER_PORT, path.c_str());
    }
    ws.onEvent(onWsEvent);
    ws.setReconnectInterval(2000);
    ws.enableHeartbeat(15000, 3000, 2);
}

void wsLoop() { ws.loop(); }

bool wsSendAudio(const uint8_t *pcm, size_t len) {
    if (!ws.isConnected()) return false;
    bool ok = false;
    if (xSemaphoreTake(ws_send_mtx, pdMS_TO_TICKS(20)) == pdTRUE) {
        ok = ws.sendBIN(pcm, len);
        xSemaphoreGive(ws_send_mtx);
    }
    return ok;
}

bool networkUploadImage(const uint8_t *jpeg, size_t len) {
    String url = String(SERVER_USE_TLS ? "https://" : "http://")
                 + SERVER_HOST + ":" + SERVER_PORT
                 + "/upload_image/" + DEVICE_ID
                 + "?key=" + DEVICE_SHARED_SECRET;
    HTTPClient http;
    int code = -1;
    if (SERVER_USE_TLS) {
        WiFiClientSecure client;
        client.setInsecure();
        if (!http.begin(client, url)) {
            Serial.println("[upload] begin failed");
            return false;
        }
        http.addHeader("Content-Type", "image/jpeg");
        code = http.POST(const_cast<uint8_t *>(jpeg), len);
    } else {
        if (!http.begin(url)) return false;
        http.addHeader("Content-Type", "image/jpeg");
        code = http.POST(const_cast<uint8_t *>(jpeg), len);
    }
    Serial.printf("[upload] %d bytes -> HTTP %d\n", (int)len, code);
    http.end();
    return code >= 200 && code < 300;
}
