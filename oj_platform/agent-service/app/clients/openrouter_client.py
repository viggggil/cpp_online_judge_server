import time
import json
from collections.abc import AsyncIterator
from typing import Generic, TypeVar

import httpx
from pydantic import BaseModel, ValidationError

from app.core.config import get_settings


T = TypeVar("T", bound=BaseModel)


class StructuredLLMResult(BaseModel, Generic[T]):
    data: T
    model: str
    provider: str = ""


class OpenRouterClient:
    def settings_chat_model(self) -> str:
        return get_settings().chat_model

    async def stream_text(
        self,
        messages: list[dict[str, str]],
    ) -> AsyncIterator[str]:
        settings = get_settings()
        if not settings.openrouter_api_key:
            raise RuntimeError("OPENROUTER_API_KEY is missing")

        url = settings.openrouter_base_url.rstrip("/") + "/chat/completions"
        async with httpx.AsyncClient(
            timeout=settings.openrouter_read_timeout_seconds
        ) as client:
            try:
                async with client.stream(
                    "POST",
                    url,
                    headers={
                        "Authorization": f"Bearer {settings.openrouter_api_key}",
                        "Content-Type": "application/json",
                    },
                    json={
                        "model": settings.chat_model,
                        "messages": messages,
                        "temperature": 0.2,
                        "max_tokens": 1400,
                        "stream": True,
                        "reasoning": {
                            "effort": "none",
                            "exclude": True,
                        },
                    },
                ) as response:
                    response.raise_for_status()
                    async for line in response.aiter_lines():
                        if not line.startswith("data:"):
                            continue

                        payload_text = line.removeprefix("data:").strip()
                        if not payload_text or payload_text == "[DONE]":
                            continue

                        try:
                            payload = json.loads(payload_text)
                        except json.JSONDecodeError:
                            continue

                        for choice in payload.get("choices") or []:
                            delta = choice.get("delta") or {}
                            content = delta.get("content")
                            if isinstance(content, str) and content:
                                yield content
            except httpx.HTTPStatusError as exc:
                response_text = exc.response.text[:1000]
                raise RuntimeError(
                    f"OpenRouter returned HTTP {exc.response.status_code}: {response_text}"
                ) from exc
            except httpx.HTTPError as exc:
                raise RuntimeError(f"OpenRouter request failed: {type(exc).__name__}: {exc}") from exc

    async def invoke_structured(
        self,
        messages: list[dict[str, str]],
        response_model: type[T],
    ) -> StructuredLLMResult[T]:
        settings = get_settings()
        if not settings.openrouter_api_key:
            raise RuntimeError("OPENROUTER_API_KEY is missing")

        url = settings.openrouter_base_url.rstrip("/") + "/chat/completions"
        async with httpx.AsyncClient(
            timeout=settings.openrouter_read_timeout_seconds
        ) as client:
            try:
                response = await client.post(
                    url,
                    headers={
                        "Authorization": f"Bearer {settings.openrouter_api_key}",
                        "Content-Type": "application/json",
                    },
                    json={
                        "model": settings.chat_model,
                        "messages": messages,
                        "temperature": 0,
                        "max_tokens": 2000,
                        "reasoning": {
                            "effort": "none",
                            "exclude": True,
                        },
                        "response_format": {
                            "type": "json_schema",
                            "json_schema": {
                                "name": response_model.__name__,
                                "strict": True,
                                "schema": response_model.model_json_schema(),
                            },
                        },
                    },
                )
                response.raise_for_status()
            except httpx.HTTPStatusError as exc:
                response_text = exc.response.text[:1000]
                raise RuntimeError(
                    f"OpenRouter returned HTTP {exc.response.status_code}: {response_text}"
                ) from exc
            except httpx.HTTPError as exc:
                raise RuntimeError(f"OpenRouter request failed: {type(exc).__name__}: {exc}") from exc

        payload = response.json()
        choice = payload["choices"][0]
        message = choice.get("message") or {}
        content = message.get("content")
        provider = (
            payload.get("provider")
            or payload.get("provider_name")
            or choice.get("provider")
            or ""
        )
        if not isinstance(content, str) or not content.strip():
            finish_reason = choice.get("finish_reason") or ""
            refusal = message.get("refusal") or ""
            reasoning_preview = str(message.get("reasoning") or "")[:200]
            raise RuntimeError(
                "OpenRouter returned empty assistant content "
                f"(finish_reason={finish_reason}, refusal={refusal}, "
                f"reasoning_preview={reasoning_preview})"
            )

        try:
            data = response_model.model_validate_json(content)
        except ValidationError as exc:
            raise RuntimeError(
                f"OpenRouter returned invalid structured JSON: {exc}"
            ) from exc

        return StructuredLLMResult(
            data=data,
            model=payload.get("model") or settings.chat_model,
            provider=str(provider),
        )


def unix_now() -> int:
    return int(time.time())
