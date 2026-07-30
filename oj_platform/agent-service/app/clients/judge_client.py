import time

import httpx

from app.core.config import get_settings
from app.schemas.judge import JudgeResponse, JudgeTestCase


class JudgeClient:
    async def judge_generated_code(
        self,
        *,
        problem_id: int,
        language: str,
        source_code: str,
        test_cases: list[JudgeTestCase],
        time_limit_ms: int,
        memory_limit_mb: int,
    ) -> JudgeResponse:
        settings = get_settings()
        if not settings.judge_worker_url:
            raise RuntimeError("JUDGE_WORKER_URL is missing")

        submission_id = time.time_ns() % 9_000_000_000_000_000_000
        payload = {
            "submission_id": submission_id,
            "problem_id": problem_id,
            "language": _normalize_language(language),
            "source_code": source_code[: settings.codegen_max_source_chars],
            "time_limit_ms": _positive_or_default(time_limit_ms, 1000),
            "memory_limit_mb": _positive_or_default(memory_limit_mb, 128),
            "test_cases": [
                {
                    "input": case.input[: settings.codegen_max_testcase_chars],
                    "expected_output": case.expected_output[
                        : settings.codegen_max_testcase_chars
                    ],
                }
                for case in test_cases[: settings.codegen_max_test_cases]
            ],
        }

        timeout = httpx.Timeout(
            connect=settings.judge_connect_timeout_seconds,
            read=settings.judge_read_timeout_seconds,
            write=settings.judge_connect_timeout_seconds,
            pool=settings.judge_connect_timeout_seconds,
        )
        async with httpx.AsyncClient(timeout=timeout) as client:
            try:
                response = await client.post(settings.judge_worker_url, json=payload)
                response.raise_for_status()
            except httpx.HTTPStatusError as exc:
                detail = exc.response.text[:500]
                raise RuntimeError(
                    f"judge_worker returned HTTP {exc.response.status_code}: {detail}"
                ) from exc
            except httpx.HTTPError as exc:
                raise RuntimeError(
                    f"judge_worker request failed: {type(exc).__name__}: {exc}"
                ) from exc

        data = response.json()
        if not isinstance(data, dict):
            raise RuntimeError("judge_worker returned non-object JSON")
        return JudgeResponse.model_validate(data)


def _normalize_language(language: str) -> str:
    lowered = (language or "cpp").strip().lower()
    if lowered in {"cpp", "c++", "cpp17", "gnu++17"}:
        return "cpp"
    raise RuntimeError(f"unsupported generated-code language: {language}")


def _positive_or_default(value: int, default: int) -> int:
    return value if value > 0 else default
