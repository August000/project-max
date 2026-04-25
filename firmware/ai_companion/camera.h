#pragma once
#include <Arduino.h>

bool cameraInit();
// Captures one JPEG frame and uploads it to the server. Returns true on success.
bool captureAndUpload();
