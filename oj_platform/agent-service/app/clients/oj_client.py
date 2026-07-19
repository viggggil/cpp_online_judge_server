from typing import Any

import httpx

from app.core.config import get_settings
from app.schemas.chat import ConversationHistoryItem
from app.schemas.oj import ProblemContext, SubmissionContext


class OjClient:
    def __init__(self, request_id: str) -> None:
        self.request_id = request_id
        self.settings = get_settings()

    async def get_problem(self, problem_id: int) -> ProblemContext:
        data = await self._get(f"/api/ai/problems/{problem_id}")
        return ProblemContext.model_validate(data)

    async def get_submission(self, user_id: int, submission_id: str) -> SubmissionContext:
        data = await self._get(
            f"/api/ai/submissions/{submission_id}",
            user_id=user_id,
        )
        return SubmissionContext.model_validate(data)

    async def get_conversation_history(
        self,
        user_id: int,
        conversation_id: str,
        limit: int = 8,
    ) -> list[ConversationHistoryItem]:
        data = await self._get(
            f"/api/ai/conversations/{conversation_id}",
            user_id=user_id,
        )
        messages = data.get("messages") or []
        if not isinstance(messages, list):
            return []
        return [
            ConversationHistoryItem.model_validate(item)
            for item in messages[-max(1, min(limit, 20)) :]
        ]

    async def search_problem(self, query: str) -> list[dict[str, Any]]:
        raise NotImplementedError("oj_server problem search API is not implemented")

    async def list_problem_submissions(
        self,
        user_id: int,
        problem_id: int,
        limit: int = 20,
    ) -> list[dict[str, Any]]:
        raise NotImplementedError("oj_server problem submissions API is not implemented")

    async def _get(
        self,
        path: str,
        *,
        user_id: int | None = None,
        query: dict[str, Any] | None = None,
    ) -> dict[str, Any]:
        if not self.settings.oj_internal_api_token:
            raise RuntimeError("OJ_INTERNAL_API_TOKEN is missing")
        base_url = self.settings.oj_server_base_url.rstrip("/")
        timeout = httpx.Timeout(
            connect=self.settings.oj_connect_timeout_seconds,
            read=self.settings.oj_read_timeout_seconds,
            write=self.settings.oj_connect_timeout_seconds,
            pool=self.settings.oj_connect_timeout_seconds,
        )
        headers = {
            "X-Internal-Token": self.settings.oj_internal_api_token,
            "X-Request-Id": self.request_id,
        }
        if user_id is not None:
            headers["X-User-Id"] = str(user_id)

        async with httpx.AsyncClient(timeout=timeout) as client:
            try:
                response = await client.get(
                    f"{base_url}{path}",
                    headers=headers,
                    params=query,
                )
                response.raise_for_status()
            except httpx.HTTPStatusError as exc:
                detail = exc.response.text[:500]
                raise RuntimeError(
                    f"oj_server returned HTTP {exc.response.status_code}: {detail}"
                ) from exc
            except httpx.HTTPError as exc:
                raise RuntimeError(
                    f"oj_server request failed: {type(exc).__name__}: {exc}"
                ) from exc
        data = response.json()
        if not isinstance(data, dict):
            raise RuntimeError("oj_server returned non-object JSON")
        return data
