import logging
from typing import Any

from mem0 import Memory

from .config import settings

log = logging.getLogger(__name__)


def _build_memory() -> Memory:
    return Memory.from_config({
        "vector_store": {
            "provider": "qdrant",
            "config": {
                "host": settings.qdrant_host,
                "port": settings.qdrant_port,
                "collection_name": settings.qdrant_collection,
                "embedding_model_dims": 1536,
            },
        },
        "embedder": {
            "provider": "openai",
            "config": {
                "model": settings.openai_embedding_model,
                "api_key": settings.openai_api_key,
            },
        },
        "llm": {
            "provider": "openai",
            "config": {
                "model": settings.openai_llm_model,
                "api_key": settings.openai_api_key,
            },
        },
    })


_mem: Memory | None = None


def get_memory() -> Memory:
    global _mem
    if _mem is None:
        _mem = _build_memory()
    return _mem


def search_memories(user_id: str, query: str, limit: int = 6) -> list[str]:
    try:
        res: dict[str, Any] = get_memory().search(query=query, user_id=user_id, limit=limit)
    except Exception as e:
        log.warning("memory search failed: %s", e)
        return []
    items = res.get("results", []) if isinstance(res, dict) else res
    out: list[str] = []
    for item in items:
        text = item.get("memory") if isinstance(item, dict) else None
        if text:
            out.append(text)
    return out


def add_messages(user_id: str, messages: list[dict[str, str]]) -> None:
    if not messages:
        return
    try:
        get_memory().add(messages, user_id=user_id)
    except Exception as e:
        log.warning("memory add failed: %s", e)
