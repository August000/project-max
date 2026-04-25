"""
Shared emotion vocabulary used by:

  * the LLM system prompt (it must emit `[emotion]` tags from this list),
  * the TTS module (each tag maps to a natural-language directive prepended
    to the text we send Gemini),
  * the firmware (same names; a JSON `{"type":"emotion","value":...}` is
    forwarded to the ESP32 to drive the TFT face).

If you add a new emotion here, also add the matching `MOOD_*` value in
`firmware/ai_companion/faces.h`.
"""

NEUTRAL = "neutral"

EMOTIONS: list[str] = [
    NEUTRAL, "happy", "sad", "angry", "curious", "tired",
    "excited", "surprised", "love", "confused",
    "sleepy", "shocked", "skeptical", "focused", "bored", "thinking",
]

EMOTION_SET = set(EMOTIONS)

# Natural-language directive Gemini TTS will follow. The text content is
# appended after this prefix. Reference:
# https://ai.google.dev/gemini-api/docs/speech-generation#prompting-strategies
TTS_DIRECTIVES: dict[str, str] = {
    NEUTRAL:    "Say in a natural, conversational tone:",
    "happy":    "Say cheerfully and warmly:",
    "sad":      "Say in a quiet, melancholy voice:",
    "angry":    "Say with sharp, irritated emphasis:",
    "curious":  "Say with bright curiosity, slightly inquisitive:",
    "tired":    "Say in a low, weary voice:",
    "excited":  "Say with high energy and excitement:",
    "surprised":"Say with surprise, voice rising:",
    "love":     "Say warmly and affectionately:",
    "confused": "Say slowly with confusion, slightly uncertain:",
    "sleepy":   "Say in a soft, drowsy voice as if half asleep:",
    "shocked":  "Say with sudden shock, almost gasping:",
    "skeptical":"Say with skepticism and a hint of doubt:",
    "focused":  "Say in a calm, focused tone:",
    "bored":    "Say in a flat, bored monotone:",
    "thinking": "Say slowly and thoughtfully, as if working it out:",
}


def normalize(emotion: str | None) -> str:
    """Return a known emotion name, defaulting to neutral."""
    if not emotion:
        return NEUTRAL
    e = emotion.strip().lower()
    return e if e in EMOTION_SET else NEUTRAL


def directive_for(emotion: str) -> str:
    return TTS_DIRECTIVES.get(normalize(emotion), TTS_DIRECTIVES[NEUTRAL])
