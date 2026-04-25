from pydantic_settings import BaseSettings, SettingsConfigDict


class Settings(BaseSettings):
    model_config = SettingsConfigDict(env_file=".env", extra="ignore")

    # OpenAI: LLM, Whisper, vision, embeddings.
    openai_api_key: str
    openai_llm_model: str = "gpt-4o-mini"
    openai_stt_model: str = "whisper-1"
    openai_vision_model: str = "gpt-4o-mini"
    openai_embedding_model: str = "text-embedding-3-small"

    # Gemini TTS.
    gemini_api_key: str
    gemini_tts_model: str = "gemini-3.1-flash-tts-preview"
    gemini_tts_voice: str = "Zephyr"

    # Qdrant (mem0 backing store).
    qdrant_host: str = "localhost"
    qdrant_port: int = 6333
    qdrant_collection: str = "ai_companion"

    # Server.
    server_host: str = "0.0.0.0"
    server_port: int = 8000
    device_shared_secret: str = "change-me-please"

    # VAD tuning.
    vad_threshold: float = 0.5
    vad_min_silence_ms: int = 500


settings = Settings()
