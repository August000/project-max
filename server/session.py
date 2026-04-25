"""
Per-device session orchestrator.

Owns the full conversational loop:

    mic PCM ──► silero VAD ──► (on end of utterance)
                                     ▼
                            Whisper STT  →  OpenAI LLM (streaming, [emotion] tags)
                                                     │
                                                     ├─► face emotion → ESP32 (text frame)
                                                     ├─► take_picture tool → ESP32 → OpenAI Vision
                                                     └─► text → Gemini TTS (24 kHz PCM) → ESP32 (binary)

A user-VAD-start during agent speech cancels the in-flight turn task,
flushes the ESP32 playback buffer, and resets the face to neutral. mem0
remembers the turn after the assistant finishes.
"""

from __future__ import annotations

import asyncio
import json
import logging
from enum import Enum, auto

from fastapi import WebSocket, WebSocketDisconnect

from . import llm, memory, stt, tts, vision
from .emotions import NEUTRAL, normalize
from .vad import VADStream

log = logging.getLogger(__name__)


class State(Enum):
    IDLE = auto()
    LISTENING = auto()
    THINKING = auto()
    SPEAKING = auto()


class DeviceSession:
    def __init__(self, device_id: str, esp_ws: WebSocket):
        self.device_id = device_id
        self.esp = esp_ws
        self.state = State.IDLE
        self.history: list[dict] = []
        self.vad = VADStream()
        self.turn_task: asyncio.Task | None = None
        self._pending_image: asyncio.Future[bytes] | None = None
        self._send_lock = asyncio.Lock()  # serialize WS writes

    # --------------------------------------------------------- lifecycle

    async def run(self) -> None:
        memories = memory.search_memories(
            user_id=self.device_id,
            query="recent conversation context, preferences, ongoing topics",
        )
        self.history = [{"role": "system", "content": llm.build_system_prompt(memories)}]
        log.info("[%s] session started, %d memories", self.device_id, len(memories))
        await self._send_face(NEUTRAL)
        try:
            await self._mic_pump()
        except WebSocketDisconnect:
            log.info("[%s] esp32 disconnected", self.device_id)
        finally:
            await self._cancel_turn()

    # --------------------------------------------------------- esp i/o

    async def _send_face(self, emotion: str) -> None:
        msg = json.dumps({"type": "emotion", "value": normalize(emotion)})
        async with self._send_lock:
            try:
                await self.esp.send_text(msg)
            except Exception:
                pass

    async def _send_interrupt(self) -> None:
        async with self._send_lock:
            try:
                await self.esp.send_text(json.dumps({"type": "interrupt"}))
            except Exception:
                pass

    async def _send_pcm(self, pcm: bytes) -> None:
        async with self._send_lock:
            await self.esp.send_bytes(pcm)

    async def _request_image(self, question: str) -> None:
        async with self._send_lock:
            await self.esp.send_text(json.dumps({"type": "request_image", "question": question}))

    # public — main.py drops the image bytes here
    def deliver_image(self, image_bytes: bytes) -> bool:
        fut = self._pending_image
        if fut is None or fut.done():
            return False
        fut.set_result(image_bytes)
        return True

    # --------------------------------------------------------- mic loop

    async def _mic_pump(self) -> None:
        while True:
            msg = await self.esp.receive()
            if msg.get("type") == "websocket.disconnect":
                raise WebSocketDisconnect()
            data = msg.get("bytes")
            if data is None:
                continue
            for ev in self.vad.feed(data):
                if ev.type == "start":
                    await self._on_speech_start()
                elif ev.type == "end" and ev.audio:
                    await self._on_speech_end(ev.audio)

    async def _on_speech_start(self) -> None:
        await self._cancel_turn()
        self.state = State.LISTENING
        await self._send_face("curious")

    async def _on_speech_end(self, audio: bytes) -> None:
        self.state = State.THINKING
        self.turn_task = asyncio.create_task(self._do_turn(audio))

    async def _cancel_turn(self) -> None:
        task = self.turn_task
        if task and not task.done():
            task.cancel()
            try:
                await task
            except (asyncio.CancelledError, Exception):
                pass
            await self._send_interrupt()
            await self._send_face(NEUTRAL)
        self.turn_task = None

    # --------------------------------------------------------- turn

    async def _do_turn(self, audio: bytes) -> None:
        try:
            await self._send_face("thinking")
            transcript = await stt.transcribe(audio)
            if not transcript:
                self.state = State.IDLE
                await self._send_face(NEUTRAL)
                return
            log.info("[%s] user: %s", self.device_id, transcript)
            self.history.append({"role": "user", "content": transcript})

            self.state = State.SPEAKING

            # Pipeline: producer streams sentences out of the LLM, consumer
            # plays them through Gemini TTS while the next sentence is being
            # generated.
            queue: asyncio.Queue = asyncio.Queue(maxsize=8)
            assistant_full: list[str] = []

            async def producer() -> None:
                try:
                    async for sentence, emotion in llm.stream_response(
                        self.history, self._handle_tool_call
                    ):
                        assistant_full.append(sentence)
                        await queue.put((sentence, emotion))
                finally:
                    await queue.put(None)

            async def consumer() -> None:
                while True:
                    item = await queue.get()
                    if item is None:
                        return
                    sentence, emotion = item
                    await self._send_face(emotion)
                    async for pcm in tts.stream(sentence, emotion):
                        await self._send_pcm(pcm)

            await asyncio.gather(producer(), consumer())

            full = " ".join(assistant_full).strip()
            if full:
                log.info("[%s] assistant: %s", self.device_id, full)
                await asyncio.to_thread(
                    memory.add_messages,
                    self.device_id,
                    [
                        {"role": "user", "content": transcript},
                        {"role": "assistant", "content": full},
                    ],
                )

        except asyncio.CancelledError:
            log.info("[%s] turn cancelled (user interrupted)", self.device_id)
            raise
        except Exception:
            log.exception("[%s] turn failed", self.device_id)
        finally:
            if self.state != State.LISTENING:
                self.state = State.IDLE
                await self._send_face(NEUTRAL)
            self.turn_task = None

    # --------------------------------------------------------- tool

    async def _handle_tool_call(self, name: str, args: dict) -> str:
        if name != "take_picture":
            return f"Unknown tool: {name}"
        question = args.get("question", "") if isinstance(args, dict) else ""
        log.info("[%s] tool take_picture(%r)", self.device_id, question)

        loop = asyncio.get_running_loop()
        self._pending_image = loop.create_future()
        try:
            await self._request_image(question)
        except Exception as e:
            self._pending_image = None
            return f"Could not reach the camera: {e}"

        try:
            image_bytes = await asyncio.wait_for(self._pending_image, timeout=10.0)
        except asyncio.TimeoutError:
            return "The camera did not respond in time."
        finally:
            self._pending_image = None

        return await vision.describe_image(image_bytes, question)
