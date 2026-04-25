"""OpenAI Whisper STT for one collected utterance."""

import io
import logging
import wave

from openai import AsyncOpenAI

from .config import settings

log = logging.getLogger(__name__)
_client = AsyncOpenAI(api_key=settings.openai_api_key)


def _pcm_to_wav(pcm: bytes, sample_rate: int = 16000) -> bytes:
    buf = io.BytesIO()
    with wave.open(buf, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(sample_rate)
        w.writeframes(pcm)
    return buf.getvalue()


async def transcribe(pcm: bytes, sample_rate: int = 16000) -> str:
    if not pcm:
        return ""
    wav = _pcm_to_wav(pcm, sample_rate)
    file_obj = io.BytesIO(wav)
    file_obj.name = "audio.wav"
    try:
        resp = await _client.audio.transcriptions.create(
            model=settings.openai_stt_model,
            file=file_obj,
        )
    except Exception:
        log.exception("Whisper STT failed")
        return ""
    return (resp.text or "").strip()
