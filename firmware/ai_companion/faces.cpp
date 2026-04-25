#include "faces.h"
#include "config.h"

#include <TFT_eSPI.h>
#include <SPI.h>

// 240x240 round display (GC9A01). Sprite-based double buffer for tear-free
// animation. Adapted from the FaceTracking-Robot example, with new moods
// to match the server's [emotion] tag set.

#define SCR_W 240
#define SCR_H 240
#define BG_COLOR  TFT_BLACK
#define EYE_COLOR TFT_WHITE

static TFT_eSPI    tft = TFT_eSPI();
static TFT_eSprite spr = TFT_eSprite(&tft);

struct EyeConfig {
    float width        = 50;
    float height       = 55;
    float borderRadius = 14;
    float spaceBetween = 24;
    float offsetX      = 0;
    float offsetY      = 0;
    float leftOpen     = 1.0f;
    float rightOpen    = 1.0f;
    float leftTopLid   = 0;
    float leftBottomLid = 0;
    float rightTopLid  = 0;
    float rightBottomLid = 0;
    float leftScaleX   = 1.0f;
    float leftScaleY   = 1.0f;
    float rightScaleX  = 1.0f;
    float rightScaleY  = 1.0f;
};

static EyeConfig current;
static EyeConfig target;
static Mood currentMood = MOOD_NEUTRAL;

// Auto-blink
static unsigned long nextBlinkTime = 0;
static bool          blinking      = false;
static unsigned long blinkStart    = 0;
static const int     blinkDuration = 150;

// Idle gaze drift
static unsigned long nextIdleTime  = 0;

// FreeRTOS task
static TaskHandle_t  faceTaskHandle = nullptr;

static float flerp(float a, float b, float t) { return a + (b - a) * t; }

// ───────── primitive drawing ─────────

static void drawEyeShape(int cx, int cy, int w, int h, int r, uint16_t color) {
    int x = cx - w / 2;
    int y = cy - h / 2;
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    spr.fillRoundRect(x, y, w, h, r, color);
}

static void drawSingleEye(int cx, int cy, float w, float h, float radius,
                          float topLid, float bottomLid, float openAmount) {
    float effectiveH = h * openAmount;
    if (effectiveH < 2) effectiveH = 2;
    int iw = (int)w;
    int ih = (int)effectiveH;
    int ir = (int)radius;
    if (ir > iw / 2) ir = iw / 2;
    if (ir > ih / 2) ir = ih / 2;

    drawEyeShape(cx, cy, iw, ih, ir, EYE_COLOR);

    if (topLid > 1 && openAmount > 0.5f) {
        int lidH = (int)(topLid * openAmount);
        spr.fillRect(cx - iw / 2 - 2, cy - ih / 2 - 2, iw + 4, lidH + 2, BG_COLOR);
    }
    if (bottomLid > 1 && openAmount > 0.5f) {
        int lidH = (int)(bottomLid * openAmount);
        spr.fillRect(cx - iw / 2 - 2, cy + ih / 2 - lidH, iw + 4, lidH + 4, BG_COLOR);
    }
}

static void drawAngryBrows(int leftCX, int leftCY, int rightCX, int rightCY,
                           float w, float h, float intensity) {
    if (intensity < 0.1f) return;
    int browThick = 4;
    int browLen   = (int)(w * 0.85f);
    int browGap   = (int)(h * 0.55f);
    int kick      = (int)(7 * intensity);

    int lx1 = leftCX - browLen / 2, ly1 = leftCY - browGap - kick;
    int lx2 = leftCX + browLen / 2, ly2 = leftCY - browGap + kick;
    for (int t = 0; t < browThick; t++) spr.drawLine(lx1, ly1 + t, lx2, ly2 + t, EYE_COLOR);

    int rx1 = rightCX - browLen / 2, ry1 = rightCY - browGap + kick;
    int rx2 = rightCX + browLen / 2, ry2 = rightCY - browGap - kick;
    for (int t = 0; t < browThick; t++) spr.drawLine(rx1, ry1 + t, rx2, ry2 + t, EYE_COLOR);
}

static void drawSkepticalBrow(int cx, int cy, float w, float h) {
    int browThick = 3;
    int browLen   = (int)(w * 0.9f);
    int browGap   = (int)(h * 0.55f);
    int x1 = cx - browLen / 2, y1 = cy - browGap;
    int x2 = cx + browLen / 2, y2 = cy - browGap - 9;
    for (int t = 0; t < browThick; t++) spr.drawLine(x1, y1 + t, x2, y2 + t, EYE_COLOR);
}

static void drawHeart(int cx, int cy, int size, uint16_t color) {
    int r = size / 2;
    spr.fillCircle(cx - r / 2, cy - r / 4, r / 2 + 1, color);
    spr.fillCircle(cx + r / 2, cy - r / 4, r / 2 + 1, color);
    spr.fillTriangle(cx - r,     cy + 1,
                     cx + r,     cy + 1,
                     cx,         cy + r, color);
}

static void drawSparkle(int cx, int cy, int size, uint16_t color) {
    spr.drawLine(cx - size, cy,        cx + size, cy,        color);
    spr.drawLine(cx,        cy - size, cx,        cy + size, color);
    spr.drawLine(cx - size + 1, cy, cx + size + 1, cy, color);
    spr.drawLine(cx, cy - size + 1, cx, cy + size + 1, color);
}

static void drawZ(int cx, int cy, int size, uint16_t color) {
    int s = size;
    spr.drawLine(cx - s, cy - s, cx + s, cy - s, color);
    spr.drawLine(cx + s, cy - s, cx - s, cy + s, color);
    spr.drawLine(cx - s, cy + s, cx + s, cy + s, color);
}

// ───────── mood targets ─────────

void setMood(Mood mood) {
    currentMood = mood;

    // reset everything to defaults
    target.width        = 50;
    target.height       = 55;
    target.borderRadius = 14;
    target.spaceBetween = 24;
    target.leftOpen = target.rightOpen = 1.0f;
    target.leftTopLid = target.leftBottomLid = 0;
    target.rightTopLid = target.rightBottomLid = 0;
    target.leftScaleX = target.leftScaleY = 1.0f;
    target.rightScaleX = target.rightScaleY = 1.0f;
    target.offsetX = target.offsetY = 0;

    float h = target.height;

    switch (mood) {
        case MOOD_HAPPY:
            target.leftBottomLid  = h * 0.50f;
            target.rightBottomLid = h * 0.50f;
            break;
        case MOOD_SAD:
            target.leftTopLid  = h * 0.30f;
            target.rightTopLid = h * 0.30f;
            target.offsetY     = 8;
            break;
        case MOOD_ANGRY:
            target.leftTopLid  = h * 0.40f;
            target.rightTopLid = h * 0.40f;
            break;
        case MOOD_CURIOUS:
            target.rightBottomLid = h * 0.18f;
            target.height         = 60;
            break;
        case MOOD_TIRED:
            target.leftTopLid  = h * 0.50f;
            target.rightTopLid = h * 0.50f;
            target.leftBottomLid  = h * 0.10f;
            target.rightBottomLid = h * 0.10f;
            break;
        case MOOD_EXCITED:
            target.height = 72;
            target.width  = 56;
            target.borderRadius = 18;
            break;
        case MOOD_SURPRISED:
            target.height = 78;
            target.width  = 62;
            target.borderRadius = 30;
            break;
        case MOOD_LOVE:
            target.width  = 66;
            target.height = 60;
            break;
        case MOOD_CONFUSED:
            target.leftTopLid     = h * 0.20f;
            target.rightBottomLid = h * 0.25f;
            target.leftScaleY     = 0.9f;
            target.rightScaleY    = 1.1f;
            break;
        case MOOD_SLEEPY:
            target.leftOpen  = 0.25f;
            target.rightOpen = 0.25f;
            break;
        case MOOD_SHOCKED:
            target.height = 84;
            target.width  = 64;
            target.borderRadius = 30;
            break;
        case MOOD_SKEPTICAL:
            target.leftOpen      = 0.55f;
            target.rightTopLid   = h * 0.10f;
            target.rightBottomLid = h * 0.10f;
            break;
        case MOOD_FOCUSED:
            target.leftOpen  = 0.55f;
            target.rightOpen = 0.55f;
            target.leftTopLid    = h * 0.10f;
            target.rightTopLid   = h * 0.10f;
            target.leftBottomLid  = h * 0.10f;
            target.rightBottomLid = h * 0.10f;
            break;
        case MOOD_BORED:
            target.leftOpen  = 0.45f;
            target.rightOpen = 0.45f;
            target.leftTopLid    = h * 0.40f;
            target.rightTopLid   = h * 0.40f;
            target.offsetY = 4;
            break;
        case MOOD_THINKING:
            target.leftTopLid  = h * 0.18f;
            target.rightTopLid = h * 0.18f;
            target.offsetY = -8;
            target.offsetX = 12;
            break;
        case MOOD_NEUTRAL:
        default:
            break;
    }
}

void setEmotionByName(const char *name) {
    if (!name) return;
    struct { const char *name; Mood mood; } table[] = {
        {"neutral",  MOOD_NEUTRAL},  {"happy",     MOOD_HAPPY},
        {"sad",      MOOD_SAD},      {"angry",     MOOD_ANGRY},
        {"curious",  MOOD_CURIOUS},  {"tired",     MOOD_TIRED},
        {"excited",  MOOD_EXCITED},  {"surprised", MOOD_SURPRISED},
        {"love",     MOOD_LOVE},     {"confused",  MOOD_CONFUSED},
        {"sleepy",   MOOD_SLEEPY},   {"shocked",   MOOD_SHOCKED},
        {"skeptical",MOOD_SKEPTICAL},{"focused",   MOOD_FOCUSED},
        {"bored",    MOOD_BORED},    {"thinking",  MOOD_THINKING},
    };
    for (auto &row : table) {
        if (strcmp(name, row.name) == 0) { setMood(row.mood); return; }
    }
    setMood(MOOD_NEUTRAL);
}

// ───────── animation helpers ─────────

static void interpolateState(float speed) {
    current.width        = flerp(current.width,        target.width,        speed);
    current.height       = flerp(current.height,       target.height,       speed);
    current.borderRadius = flerp(current.borderRadius, target.borderRadius, speed);
    current.spaceBetween = flerp(current.spaceBetween, target.spaceBetween, speed);
    current.offsetX      = flerp(current.offsetX,      target.offsetX,      speed);
    current.offsetY      = flerp(current.offsetY,      target.offsetY,      speed);
    current.leftOpen     = flerp(current.leftOpen,     target.leftOpen,     speed);
    current.rightOpen    = flerp(current.rightOpen,    target.rightOpen,    speed);
    current.leftTopLid     = flerp(current.leftTopLid,     target.leftTopLid,     speed);
    current.leftBottomLid  = flerp(current.leftBottomLid,  target.leftBottomLid,  speed);
    current.rightTopLid    = flerp(current.rightTopLid,    target.rightTopLid,    speed);
    current.rightBottomLid = flerp(current.rightBottomLid, target.rightBottomLid, speed);
    current.leftScaleX  = flerp(current.leftScaleX,  target.leftScaleX,  speed);
    current.leftScaleY  = flerp(current.leftScaleY,  target.leftScaleY,  speed);
    current.rightScaleX = flerp(current.rightScaleX, target.rightScaleX, speed);
    current.rightScaleY = flerp(current.rightScaleY, target.rightScaleY, speed);
}

static void handleAutoBlink() {
    unsigned long now = millis();
    if (currentMood == MOOD_SLEEPY) return;  // already nearly closed

    if (!blinking && now >= nextBlinkTime) {
        blinking = true;
        blinkStart = now;
        target.leftOpen  *= 0.0f;
        target.rightOpen *= 0.0f;
    }
    if (blinking && (now - blinkStart > (unsigned long)blinkDuration)) {
        // restore mood-driven open value
        Mood m = currentMood;
        setMood(m);
        blinking = false;
        nextBlinkTime = now + 3500UL + (unsigned long)random(0, 2000);
    }
}

static void handleIdleGaze() {
    unsigned long now = millis();
    // Skip drift in expressive moods so the face stays aimed.
    if (currentMood == MOOD_THINKING || currentMood == MOOD_LOVE ||
        currentMood == MOOD_SHOCKED || currentMood == MOOD_SLEEPY) return;
    if (now < nextIdleTime) return;
    float maxX = 30, maxY = 18;
    target.offsetX = (float)random(-(int)maxX, (int)maxX);
    target.offsetY = (float)random(-(int)maxY, (int)maxY);
    nextIdleTime = now + 2500UL + (unsigned long)random(0, 2500);
}

// ───────── drawing ─────────

static void drawLoveEyes(int leftCX, int leftCY, int rightCX, int rightCY) {
    int sz = (int)current.height;
    drawHeart(leftCX,  leftCY, sz, EYE_COLOR);
    drawHeart(rightCX, rightCY, sz, EYE_COLOR);
}

static void drawExcitedSparkles(int leftCX, int leftCY, int rightCX, int rightCY) {
    drawSparkle(leftCX  - 32, leftCY  - 32, 5, EYE_COLOR);
    drawSparkle(rightCX + 32, rightCY - 32, 5, EYE_COLOR);
    drawSparkle(leftCX  - 38, leftCY  + 30, 3, EYE_COLOR);
    drawSparkle(rightCX + 38, rightCY + 30, 3, EYE_COLOR);
}

static void drawSleepyZ(int cx, int cy) {
    drawZ(cx + 60, cy - 60, 6, EYE_COLOR);
    drawZ(cx + 78, cy - 80, 4, EYE_COLOR);
}

static void drawEyes() {
    spr.fillSprite(BG_COLOR);

    float ox = current.offsetX;
    float oy = current.offsetY;

    int sp = (int)current.spaceBetween;
    float wL = current.width  * current.leftScaleX;
    float hL = current.height * current.leftScaleY;
    float wR = current.width  * current.rightScaleX;
    float hR = current.height * current.rightScaleY;

    int leftCX  = SCR_W / 2 - sp / 2 - (int)(wL / 2) + (int)ox;
    int leftCY  = SCR_H / 2 + (int)oy;
    int rightCX = SCR_W / 2 + sp / 2 + (int)(wR / 2) + (int)ox;
    int rightCY = SCR_H / 2 + (int)oy;

    if (currentMood == MOOD_LOVE) {
        drawLoveEyes(leftCX, leftCY, rightCX, rightCY);
    } else {
        drawSingleEye(leftCX,  leftCY,  wL, hL, current.borderRadius,
                      current.leftTopLid,  current.leftBottomLid,  current.leftOpen);
        drawSingleEye(rightCX, rightCY, wR, hR, current.borderRadius,
                      current.rightTopLid, current.rightBottomLid, current.rightOpen);
    }

    if (currentMood == MOOD_ANGRY) {
        float intensity = current.leftTopLid / (target.height * 0.40f + 0.01f);
        if (intensity > 1) intensity = 1;
        drawAngryBrows(leftCX, leftCY, rightCX, rightCY,
                       current.width, current.height, intensity);
    } else if (currentMood == MOOD_SKEPTICAL) {
        drawSkepticalBrow(rightCX, rightCY, current.width, current.height);
    } else if (currentMood == MOOD_EXCITED) {
        drawExcitedSparkles(leftCX, leftCY, rightCX, rightCY);
    } else if (currentMood == MOOD_SLEEPY) {
        drawSleepyZ(rightCX, rightCY);
    }

    spr.pushSprite(0, 0);
}

// ───────── public API ─────────

static void faceTask(void *) {
    nextBlinkTime = millis() + 2000;
    nextIdleTime  = millis() + 3000;
    while (true) {
        facesUpdate();
        vTaskDelay(pdMS_TO_TICKS(20));   // ~50 fps target
    }
}

void facesInit() {
    tft.init();
    tft.setRotation(0);
    tft.fillScreen(BG_COLOR);

    spr.setColorDepth(16);
    if (!spr.createSprite(SCR_W, SCR_H)) {
        Serial.println("[face] 16-bit sprite failed, falling back to 8-bit");
        spr.setColorDepth(8);
        spr.createSprite(SCR_W, SCR_H);
    }
    setMood(MOOD_NEUTRAL);
    current = target;

    xTaskCreatePinnedToCore(faceTask, "face", 4096, nullptr, 2, &faceTaskHandle, 1);
}

void facesUpdate() {
    handleAutoBlink();
    handleIdleGaze();
    interpolateState(0.30f);
    drawEyes();
}
