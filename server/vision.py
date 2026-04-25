import base64
import logging

from openai import AsyncOpenAI

from .config import settings

log = logging.getLogger(__name__)
_client = AsyncOpenAI(api_key=settings.openai_api_key)


async def describe_image(image_bytes: bytes, question: str) -> str:
    if not image_bytes:
        return "No image was captured."
    b64 = base64.b64encode(image_bytes).decode()
    prompt = question.strip() or "Describe what you see in detail."
    try:
        resp = await _client.chat.completions.create(
            model=settings.openai_vision_model,
            messages=[{
                "role": "user",
                "content": [
                    {"type": "text", "text": prompt},
                    {
                        "type": "image_url",
                        "image_url": {"url": f"data:image/jpeg;base64,{b64}"},
                    },
                ],
            }],
            max_tokens=400,
        )
        return (resp.choices[0].message.content or "").strip() or "I couldn't make sense of the image."
    except Exception as e:
        log.exception("vision call failed")
        return f"Vision call failed: {e}"
