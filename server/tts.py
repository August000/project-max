"""
Gemini TTS streaming.

The model returns 24 kHz / 16-bit / mono PCM by default (mime
`audio/L16;rate=24000`). The ESP32 plays at the same rate, so chunks are
forwarded verbatim — no resampling, no WAV header.

Emotion is conveyed by prepending the directive from `emotions.directive_for`
to the text. See:
https://ai.google.dev/gemini-api/docs/speech-generation#prompting-strategies
"""

from __future__ import annotations

import logging
from typing import AsyncIterator

from google import genai
from google.genai import types

from .config import settings
from .emotions import directive_for, normalize

log = logging.getLogger(__name__)
_client = genai.Client(api_key=settings.gemini_api_key)


async def stream(text: str, emotion: str) -> AsyncIterator[bytes]:
    text = (text or "").strip()
    if not text:
        return

    full = f"{directive_for(normalize(emotion))} {text}"
    contents = [types.Content(role="user", parts=[types.Part.from_text(text=full)])]

    config = types.GenerateContentConfig(
        response_modalities=["audio"],
        speech_config=types.SpeechConfig(
            voice_config=types.VoiceConfig(
                prebuilt_voice_config=types.PrebuiltVoiceConfig(
                    voice_name=settings.gemini_tts_voice,
                )
            )
        ),
    )

    try:
        gen = await _client.aio.models.generate_content_stream(
            model=settings.gemini_tts_model,
            contents=contents,
            config=config,
        )
    except Exception:
        log.exception("Gemini TTS request failed")
        return

    async for chunk in gen:
        if not chunk.candidates:
            continue
        parts = chunk.candidates[0].content.parts if chunk.candidates[0].content else None
        if not parts:
            continue
        for part in parts:
            inline = getattr(part, "inline_data", None)
            if inline and inline.data:
                yield inline.data
