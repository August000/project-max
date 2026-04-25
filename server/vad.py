"""
Streaming voice-activity detection wrapper around silero-vad.

Always-on. The ESP32 fire-hoses 16 kHz / 16-bit / mono PCM at us; we slice
that into 32 ms windows, push them through silero, and emit `start` / `end`
events with the recorded utterance bytes attached to `end`.

A short rolling pre-roll buffer is prepended on `start` so the first ~300 ms
of speech aren't lost while VAD makes up its mind.
"""

from __future__ import annotations

import logging
from collections import deque
from dataclasses import dataclass

import numpy as np
import torch
from silero_vad import VADIterator, load_silero_vad

from .config import settings

log = logging.getLogger(__name__)

SAMPLE_RATE = 16000
WINDOW_SAMPLES = 512        # silero v5 requires exactly 512 samples @ 16 kHz
WINDOW_BYTES = WINDOW_SAMPLES * 2
PRE_ROLL_MS = 300


@dataclass
class VADEvent:
    type: str                 # "start" | "end"
    audio: bytes | None = None  # bytes of the utterance (only on "end")


_model = None


def _get_model():
    global _model
    if _model is None:
        log.info("loading silero-vad model")
        _model = load_silero_vad(onnx=True)
    return _model


class VADStream:
    def __init__(self) -> None:
        self.iter = VADIterator(
            _get_model(),
            threshold=settings.vad_threshold,
            sampling_rate=SAMPLE_RATE,
            min_silence_duration_ms=settings.vad_min_silence_ms,
        )
        self._buf = bytearray()
        self._pre_roll = bytearray()
        self._pre_roll_max = (SAMPLE_RATE * PRE_ROLL_MS // 1000) * 2
        self._utterance = bytearray()
        self._speaking = False

    def reset(self) -> None:
        self.iter.reset_states()
        self._buf.clear()
        self._utterance.clear()
        self._speaking = False

    def feed(self, pcm: bytes) -> list[VADEvent]:
        events: list[VADEvent] = []

        # Pre-roll ring buffer.
        self._pre_roll.extend(pcm)
        if len(self._pre_roll) > self._pre_roll_max:
            self._pre_roll = self._pre_roll[-self._pre_roll_max:]

        if self._speaking:
            self._utterance.extend(pcm)

        self._buf.extend(pcm)
        while len(self._buf) >= WINDOW_BYTES:
            window = bytes(self._buf[:WINDOW_BYTES])
            del self._buf[:WINDOW_BYTES]
            arr = np.frombuffer(window, dtype=np.int16).astype(np.float32) / 32768.0
            tensor = torch.from_numpy(arr)
            try:
                ev = self.iter(tensor)
            except Exception:
                log.exception("silero VAD step failed")
                continue
            if not ev:
                continue
            if "start" in ev:
                self._speaking = True
                # Seed the utterance with whatever's in the pre-roll so we don't
                # chop the first phoneme.
                self._utterance = bytearray(self._pre_roll)
                events.append(VADEvent(type="start"))
            elif "end" in ev:
                self._speaking = False
                events.append(VADEvent(type="end", audio=bytes(self._utterance)))
                self._utterance = bytearray()
        return events
