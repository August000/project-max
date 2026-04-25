#pragma once
#include <Arduino.h>

void audioInit();
size_t micRead(int16_t *out, size_t max_samples);   // returns sample count
void  playbackPush(const uint8_t *pcm, size_t len);  // non-blocking
void  playbackFlush();                                // drop everything in flight
