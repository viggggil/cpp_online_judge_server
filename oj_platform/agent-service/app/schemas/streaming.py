import json
from collections.abc import AsyncIterator
from typing import Any


def sse_event(event: str, data: dict[str, Any]) -> str:
    return (
        f"event: {event}\n"
        f"data: {json.dumps(data, ensure_ascii=False)}\n\n"
    )


async def error_event_stream(message: str) -> AsyncIterator[str]:
    yield sse_event("error", {"message": message})
