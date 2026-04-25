"""
OpenAI chat orchestration.

Streams the assistant's response, parsing inline `[emotion]` tags into
`(sentence, emotion)` segments. Handles the `take_picture` tool call by
delegating to a callback supplied by the session.

Tag grammar the LLM is instructed to follow:
    [emotion] sentence one. [emotion] sentence two.

`emotion` must be one of `server.emotions.EMOTIONS`. Tags can change
between sentences; the parser carries the last seen emotion forward.
"""

from __future__ import annotations

import json
import logging
from typing import AsyncIterator, Awaitable, Callable

from openai import AsyncOpenAI

from .config import settings
from .emotions import EMOTION_SET, NEUTRAL, normalize

log = logging.getLogger(__name__)
_client = AsyncOpenAI(api_key=settings.openai_api_key)

ToolHandler = Callable[[str, dict], Awaitable[str]]


SYSTEM_PROMPT = (
    "You are a warm, witty AI companion who speaks naturally and concisely, "
    "the way a friend would over voice. Keep replies short — usually 1 to 3 "
    "sentences. Never read URLs, code, or markdown out loud.\n\n"
    "EMOTION TAGS — REQUIRED.\n"
    "Begin every sentence of your reply with exactly one tag in square brackets, "
    "chosen from this list:\n"
    "  {emotions}\n"
    "Pick the tag that best matches the feeling of that sentence. The tag both "
    "drives the speaker's tone of voice and the robot's facial expression, so "
    "vary it as the mood of your reply shifts. Example:\n"
    "  [excited] Wait, that sounds amazing! [curious] What made you decide to do it?\n"
    "Do not put tags anywhere except at the start of a sentence. Do not invent "
    "tag names. If unsure, use [neutral].\n\n"
    "VISION.\n"
    "You can see through the user's camera by calling the `take_picture` tool. "
    "Call it whenever the user asks you to look at something, identify an object, "
    "read text in front of them, or asks any visual question. Pass a clear "
    "`question` describing what to look for."
)


TOOLS = [
    {
        "type": "function",
        "function": {
            "name": "take_picture",
            "description": (
                "Capture a photo from the user's camera and return a description of "
                "what's in view. Use this for any visual question."
            ),
            "parameters": {
                "type": "object",
                "properties": {
                    "question": {
                        "type": "string",
                        "description": "What the user wants to know about the scene.",
                    }
                },
                "required": ["question"],
            },
        },
    }
]


def build_system_prompt(memories: list[str]) -> str:
    base = SYSTEM_PROMPT.format(emotions=", ".join(sorted(EMOTION_SET)))
    if memories:
        bullets = "\n".join(f"  - {m}" for m in memories)
        base += (
            "\n\nWhat you remember about this user from previous chats — use it "
            f"naturally; don't list it back to them:\n{bullets}"
        )
    return base


# ---------------------------------------------------------------------------
# Streaming emotion-tag parser

class EmotionParser:
    """
    Stateful, push-based parser. Feed it text deltas; it emits
    `(sentence, emotion)` tuples whenever a sentence completes or the emotion
    tag changes.
    """

    def __init__(self) -> None:
        self.buffer: str = ""
        self.last_emit: int = 0
        self.current_emotion: str = NEUTRAL

    def feed(self, text: str) -> list[tuple[str, str]]:
        self.buffer += text
        return self._scan()

    def flush(self) -> list[tuple[str, str]]:
        out: list[tuple[str, str]] = []
        remainder = self.buffer[self.last_emit:].strip()
        if remainder:
            out.append((remainder, self.current_emotion))
        self.last_emit = len(self.buffer)
        return out

    def full_text(self) -> str:
        return self.buffer

    def _scan(self) -> list[tuple[str, str]]:
        emitted: list[tuple[str, str]] = []
        i = self.last_emit
        n = len(self.buffer)
        while i < n:
            ch = self.buffer[i]
            if ch == "[":
                close = self.buffer.find("]", i)
                if close == -1:
                    break  # tag not yet complete
                pre = self.buffer[self.last_emit:i].strip()
                if pre:
                    emitted.append((pre, self.current_emotion))
                tag = self.buffer[i + 1:close].strip().lower()
                if tag in EMOTION_SET:
                    self.current_emotion = tag
                self.last_emit = close + 1
                i = self.last_emit
                continue
            if ch in ".!?":
                # Sentence boundary only if next char is whitespace (or end).
                next_i = i + 1
                if next_i >= n:
                    break  # might be more punctuation coming
                if self.buffer[next_i] in " \n\t":
                    sentence = self.buffer[self.last_emit:i + 1].strip()
                    if sentence:
                        emitted.append((sentence, self.current_emotion))
                    self.last_emit = i + 1
                    i = self.last_emit
                    continue
            i += 1
        return emitted


# ---------------------------------------------------------------------------
# Tool-call accumulator (OpenAI streams tool args as deltas)

def _accumulate_tool_calls(acc: dict[int, dict], deltas) -> None:
    for tc in deltas:
        idx = tc.index
        slot = acc.setdefault(idx, {"id": "", "name": "", "arguments": ""})
        if tc.id:
            slot["id"] = tc.id
        if tc.function:
            if tc.function.name:
                slot["name"] += tc.function.name
            if tc.function.arguments:
                slot["arguments"] += tc.function.arguments


# ---------------------------------------------------------------------------
# Main streaming entry point

async def stream_response(
    history: list[dict],
    tool_handler: ToolHandler,
) -> AsyncIterator[tuple[str, str]]:
    """
    Stream the assistant's reply. Yields `(sentence, emotion)` per segment.
    May call `tool_handler(name, args) -> str` zero or more times before
    producing the final user-visible text. The full assistant message is
    appended to `history` after streaming completes.
    """
    safety_iters = 4  # max round-trips of tool calling
    while safety_iters > 0:
        safety_iters -= 1

        parser = EmotionParser()
        tool_calls: dict[int, dict] = {}
        finish_reason: str | None = None

        stream = await _client.chat.completions.create(
            model=settings.openai_llm_model,
            messages=history,
            tools=TOOLS,
            stream=True,
            temperature=0.8,
        )

        async for chunk in stream:
            if not chunk.choices:
                continue
            choice = chunk.choices[0]
            delta = choice.delta

            if delta and delta.content:
                for seg in parser.feed(delta.content):
                    yield (seg[0], normalize(seg[1]))
            if delta and delta.tool_calls:
                _accumulate_tool_calls(tool_calls, delta.tool_calls)

            if choice.finish_reason:
                finish_reason = choice.finish_reason

        if finish_reason == "tool_calls" and tool_calls:
            # Flush any leading text the model emitted before the tool call.
            for seg in parser.flush():
                yield (seg[0], normalize(seg[1]))

            tc_msg_list = [
                {
                    "id": tc["id"],
                    "type": "function",
                    "function": {"name": tc["name"], "arguments": tc["arguments"] or "{}"},
                }
                for tc in tool_calls.values()
            ]
            history.append({
                "role": "assistant",
                "content": parser.full_text() or None,
                "tool_calls": tc_msg_list,
            })

            for tc in tool_calls.values():
                try:
                    args = json.loads(tc["arguments"]) if tc["arguments"] else {}
                except json.JSONDecodeError:
                    args = {}
                try:
                    result = await tool_handler(tc["name"], args)
                except Exception as e:
                    log.exception("tool handler failed")
                    result = f"Tool {tc['name']} failed: {e}"
                history.append({
                    "role": "tool",
                    "tool_call_id": tc["id"],
                    "content": result or "",
                })
            continue  # re-call LLM with tool results

        # Plain text response — flush remainder and finish.
        for seg in parser.flush():
            yield (seg[0], normalize(seg[1]))
        if parser.full_text():
            history.append({"role": "assistant", "content": parser.full_text()})
        return

    log.warning("LLM tool-call loop hit safety limit; bailing")
