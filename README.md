# project-max — ESP32-S3 AI Companion

A real-time, voice-first AI companion with a TFT face, built on:

- **ESP32-S3 (Freenove WROOM CAM)** — mic in, speaker out, camera on demand, **TFT eyes**
- **silero-vad** — server-side voice activity detection (always-on streaming)
- **OpenAI Whisper** — speech-to-text
- **OpenAI Chat (`gpt-4o-mini`)** — LLM, emits `[emotion]` sentence tags + tool calls
- **OpenAI Vision** — invoked on demand when the LLM calls `take_picture`
- **Gemini TTS (`gemini-3.1-flash-tts-preview`)** — emotion-driven streaming TTS at 24 kHz
- **mem0 + Qdrant** — long-term memory across sessions
- **Cloudflare Tunnel** — exposes the local server through your managed network
- **uv** — Python project manager

```
                 ┌───────────────────── server (FastAPI) ──────────────────────┐
                 │                                                              │
ESP32 ──WS PCM──►│  silero VAD  → on end of utterance →                         │
                 │     │                Whisper STT                             │
                 │     │                    │                                   │
                 │     │           OpenAI Chat (streaming, [emotion] tags)      │
                 │     │            │       │       │                          │
                 │     │            │       │       └─► take_picture tool ─┐   │
                 │     │            │       │                              │   │
                 │     │            │       └─► face emotion → ESP32 (TFT) │   │
                 │     │            │                                      │   │
                 │     │            └─► Gemini TTS (24 kHz PCM) → ESP32    │   │
                 │     │                                                   │   │
                 │     └── (interrupt: cancel turn, flush playback) ◄──────┤   │
                 │                                                         │   │
                 └─── HTTP POST /upload_image (JPEG) ◄──── ESP32 ◄─ request_image
                          │                                                    │
                          └──► OpenAI Vision ──► result back into LLM ─────────┘
```

---

## 1. Hardware wiring

All connections share the ESP32-S3 board's `GND` and `3.3V` (mic, TFT) / `5V` (amp).

### INMP441 microphone (I2S0, 16 kHz)

| INMP441 | ESP32-S3 |
|---------|----------|
| VDD     | **3V3**  |
| GND     | **GND**  |
| L/R     | **GND**  *(selects left channel)* |
| WS      | **GPIO 41** |
| SCK     | **GPIO 42** |
| SD      | **GPIO 2**  |

### MAX98357 amplifier (I2S1, 24 kHz)

| MAX98357 | ESP32-S3 |
|----------|----------|
| VIN      | **5V**   |
| GND      | **GND**  |
| LRC      | **GPIO 21** |
| BCLK     | **GPIO 47** |
| DIN      | **GPIO 14** |
| GAIN     | **GND** (12 dB) |
| SD       | floating *(or jumper to 3V3 if module needs it)* |

### Teyleten 1.28" GC9A01 TFT (SPI)

| TFT  | ESP32-S3 | Notes |
|------|----------|-------|
| VCC  | **3V3**  | |
| GND  | **GND**  | |
| SDA  | **GPIO 38** | MOSI |
| SCL  | **GPIO 39** | SCK |
| DC   | **GPIO 40** | data/command |
| CS   | **GPIO 1**  | chip select |
| RST  | **GPIO 3**  | reset (GPIO3 floats high at boot — OK) |

> **⚠️ The TFT pins overlap the onboard microSD slot.** Don't insert an SD card while the TFT is wired — they share GPIOs 38/39/40.

### Speaker

| Speaker | MAX98357 |
|---------|----------|
| +       | **SPK+** |
| –       | **SPK–** |

### Already-wired (don't touch)

OV2640 camera: GPIOs 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 15, 16, 17, 18. RGB LED: 48.

> **Pin budget recap.** After camera + boot strapping (0, 45, 46) + USB (19, 20) + octal-PSRAM (35–37 on N16R8), the only safe free GPIOs are **1, 2, 3, 14, 21, 38, 39, 40, 41, 42, 47** — exactly the 11 we use.

---

## 2. Server setup (your Mac)

### Prereqs

- `uv` (`/Users/augustobatista/.local/bin/uv`)
- `docker` (for Qdrant)
- `cloudflared` (`/opt/homebrew/bin/cloudflared`)

### One-time setup

```bash
cd /Users/augustobatista/Desktop/project-max

# 1) Install Python deps
uv sync

# 2) Copy and edit env
cp .env.example .env
# fill in:
#   OPENAI_API_KEY=...
#   GEMINI_API_KEY=...                  (https://aistudio.google.com/app/apikey)
#   DEVICE_SHARED_SECRET=<long random>

# 3) Start Qdrant in Docker
docker compose up -d
```

### Run the server

In **terminal A**:
```bash
uv run uvicorn server.main:app --host 0.0.0.0 --port 8000
```

In **terminal B**:
```bash
cloudflared tunnel --url http://localhost:8000
```

Note the printed `https://<random>.trycloudflare.com` hostname — the ESP32 needs it. Health check:
```bash
curl https://<your-tunnel>.trycloudflare.com/health
```

---

## 3. Firmware setup

### Arduino IDE prereqs

1. Install **ESP32 by Espressif Systems**, **v3.0.0+** (Boards Manager → search "esp32").
2. In the Library Manager, install:
   - **WebSockets** by Markus Sattler (Links2004)
   - **ArduinoJson** by Benoit Blanchon (v7+)
   - **TFT_eSPI** by Bodmer
3. **Configure TFT_eSPI** — open
   `~/Documents/Arduino/libraries/TFT_eSPI/User_Setup.h` and replace its
   contents with the block in `firmware/ai_companion/User_Setup_Excerpt.h`.
   This is a library-level setting; you only do it once per Arduino install.
4. Board: **ESP32S3 Dev Module**
   - PSRAM: **OPI PSRAM**
   - Flash size: **16MB (128Mb)**
   - Partition Scheme: **16M Flash (3MB APP / 9.9MB FATFS)**
   - USB CDC On Boot: **Enabled**

### Configure and flash

```bash
cd firmware/ai_companion
cp config.h.example config.h
# Edit config.h — Wi-Fi creds, SERVER_HOST (your CF tunnel), DEVICE_SHARED_SECRET.
```

Open `ai_companion.ino` in Arduino IDE → Upload. Serial Monitor at **115200 baud**:
```
[boot] ai_companion starting
[wifi] ip=...
[ws] connecting to wss://...
[ws] connected
[boot] ready
```

The TFT should show sleepy eyes during boot, then open into neutral once the WS link is up. Speak — silero detects start of speech, the eyes shift to **curious**, then to **thinking** while the LLM runs, then to whatever emotion the response carries.

---

## 4. How it works

- **Always-on streaming.** ESP32 fire-hoses 16 kHz / 16-bit / mono PCM. The server runs silero-vad on 32 ms windows and emits `start` / `end` events.
- **Per turn:** Whisper transcribes the captured utterance → OpenAI Chat streams a reply with `[happy] ... [excited] ...` tags → a sentence-by-sentence parser splits the stream → each segment fires `{"type":"emotion","value":"..."}` to the TFT and concurrently calls Gemini TTS, which streams 24 kHz PCM straight into the speaker buffer.
- **Interruption.** silero detecting fresh speech mid-reply cancels the in-flight `asyncio` task; the server sends `{"type":"interrupt"}`, which empties the playback stream buffer and resets the I2S TX channel within ~30 ms.
- **Vision.** The agent has one tool, `take_picture(question)`. When called, the server requests an image from the ESP32, ESP32 POSTs a JPEG to `/upload_image/...`, OpenAI Vision describes it with the user's question, and the description is fed back as the tool result for the next LLM call.
- **Memory.** mem0 + Qdrant. At session start: relevant memories are pulled and injected into the system prompt. After every turn: the user/assistant pair is added to mem0 in a background thread.
- **Two sample rates.** Mic at 16 kHz (Whisper-native, lighter bandwidth). Speaker at 24 kHz (Gemini's PCM output rate, no resampling). The two I2S peripherals (`I2S_NUM_0`, `I2S_NUM_1`) run independent clocks — full duplex.

### Emotions

The LLM is constrained to these tags (defined once in `server/emotions.py`, mirrored in `firmware/ai_companion/faces.h`):

`neutral, happy, sad, angry, curious, tired, excited, surprised, love, confused, sleepy, shocked, skeptical, focused, bored, thinking`

Each has both:
- a **TTS directive** (e.g. `[happy]` → Gemini receives `Say cheerfully and warmly: ...` prepended to the text), and
- a **face mood** (custom eye geometry, plus per-mood overlays — angry brows, heart eyes for love, sparkles for excited, "Z" glyphs for sleepy, raised brow for skeptical).

To add an emotion: add it to `EMOTIONS` + `TTS_DIRECTIVES` in `server/emotions.py`, add a matching `MOOD_*` enum + `setMood` case in `firmware/ai_companion/faces.cpp`, and the LLM will start using it on next session start.

---

## 5. File tour

| File | Role |
|------|------|
| `server/main.py` | FastAPI app: `/health`, `/ws/esp32/{id}`, `/upload_image/{id}` |
| `server/session.py` | Per-device state machine: VAD → STT → LLM → TTS pipeline + interrupt handling |
| `server/vad.py` | silero-vad streaming wrapper with pre-roll buffer |
| `server/stt.py` | Whisper (PCM → WAV → API) |
| `server/llm.py` | OpenAI streaming chat + emotion-tag parser + tool-call loop |
| `server/tts.py` | Gemini async streaming TTS |
| `server/emotions.py` | Single source of truth for emotion vocabulary + TTS directives |
| `server/vision.py` | OpenAI Vision describer used by `take_picture` |
| `server/memory.py` | mem0 + Qdrant adapter |
| `server/config.py` | pydantic-settings, loads `.env` |
| `firmware/ai_companion/ai_companion.ino` | Sketch entry |
| `firmware/ai_companion/audio.cpp` | I2S RX (16 kHz) + TX (24 kHz) + playback ring buffer + flush |
| `firmware/ai_companion/camera.cpp` | OV2640 capture + upload |
| `firmware/ai_companion/network.cpp` | Wi-Fi + WSS + image upload + JSON control parser |
| `firmware/ai_companion/faces.cpp` | TFT eye renderer, 16 moods, dedicated FreeRTOS task on core 1 |
| `firmware/ai_companion/User_Setup_Excerpt.h` | Snippet to paste into TFT_eSPI's `User_Setup.h` |
| `docker-compose.yml` | Qdrant single-node container |

---

## 6. Tuning knobs

- **`MIC_GAIN_SHIFT`** in `config.h` — 11 (default) is loud; raise to 13/14 if Whisper hears clipping.
- **`VAD_THRESHOLD` / `VAD_MIN_SILENCE_MS`** in `.env` — lower threshold = picks up quieter speech (more false starts); shorter silence = snappier turns (more accidental cuts).
- **`GEMINI_TTS_VOICE`** in `.env` — try `Puck`, `Charon`, `Kore`, `Fenrir`, `Aoede`. Each has different default character.
- **System prompt** in `server/llm.py::SYSTEM_PROMPT` — change the personality. Tag rules live there too.
- **Echo / self-interrupt.** Speaker can leak into mic and falsely interrupt. Mitigations: physically separate them, jumper MAX98357 GAIN to a lower value, raise `VAD_THRESHOLD`.
- **TFT flicker / artefacts.** Drop `SPI_FREQUENCY` in `User_Setup_Excerpt.h` from 40 MHz → 27 MHz.

---

## 7. Day-to-day

```bash
uv run uvicorn server.main:app --host 0.0.0.0 --port 8000 --reload
cloudflared tunnel --url http://localhost:8000
docker compose up -d   # start qdrant
docker compose down    # stop
```
