#pragma once
#include <Arduino.h>

void wifiConnect();
void wsBegin();
void wsLoop();
bool wsSendAudio(const uint8_t *pcm, size_t len);
bool networkUploadImage(const uint8_t *jpeg, size_t len);

void setMicMuted(bool muted);
bool isMicMuted();
