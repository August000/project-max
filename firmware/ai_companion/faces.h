#pragma once
#include <Arduino.h>

// Order must match `server/emotions.py::EMOTIONS`. setEmotionByName() does a
// string lookup so the wire protocol stays human-readable.
enum Mood {
    MOOD_NEUTRAL = 0,
    MOOD_HAPPY,
    MOOD_SAD,
    MOOD_ANGRY,
    MOOD_CURIOUS,
    MOOD_TIRED,
    MOOD_EXCITED,
    MOOD_SURPRISED,
    MOOD_LOVE,
    MOOD_CONFUSED,
    MOOD_SLEEPY,
    MOOD_SHOCKED,
    MOOD_SKEPTICAL,
    MOOD_FOCUSED,
    MOOD_BORED,
    MOOD_THINKING,
};

void facesInit();
void facesUpdate();                       // call every frame from loop()
void setEmotionByName(const char *name);  // wire protocol
void setMood(Mood mood);
