import asyncio
import logging
import re
import time
from collections.abc import AsyncIterator

from app.api.dependencies import RequestContext
from app.clients.openrouter_client import OpenRouterClient, unix_now
from app.core.config import get_settings
from app.schemas.chat import (
    AgentChatRequest,
    AgentChatResponse,
    ExecutedToolResult,
    PlannerPlan,
    PlannerToolCall,
)
from app.schemas.streaming import sse_event
from app.services.planner_service import PlannerService
from app.services.prompt_service import PromptService
from app.services.safety_service import SafetyService
from app.tools.executor import ToolExecutor
from app.tools.oj_tools import OjTools
from app.tools.rag_tools import build_retrieve_knowledge_tool


logger = logging.getLogger(__name__)
_PLANNER_HEARTBEAT_SECONDS = 8
_ANSWER_HEARTBEAT_SECONDS = 10


class ChatWorkflow:
    def __init__(self) -> None:
        self.prompt_service = PromptService()
        self.planner_service = PlannerService()
        self.llm_client = OpenRouterClient()
        self.safety_service = SafetyService()

    async def run_stage_stream(
        self,
        request: AgentChatRequest,
        context: RequestContext,
    ) -> AsyncIterator[str]:
        settings = get_settings()
        yield sse_event("status", {"stage": "planning", "message": "正在理解你的问题"})

        tools = [
            *OjTools(
                user_id=request.user.user_id,
                request_id=context.request_id,
            ).build_tools(),
            build_retrieve_knowledge_tool(),
        ]
        planning_started_at = time.monotonic()
        planner_task: asyncio.Task[PlannerPlan] | None = None
        try:
            planner_task = asyncio.create_task(self.planner_service.plan(request, tools))
            while True:
                elapsed = time.monotonic() - planning_started_at
                remaining = settings.planner_timeout_seconds - elapsed
                if remaining <= 0:
                    planner_task.cancel()
                    raise TimeoutError

                done, _ = await asyncio.wait(
                    {planner_task},
                    timeout=min(_PLANNER_HEARTBEAT_SECONDS, remaining),
                    return_when=asyncio.FIRST_COMPLETED,
                )
                if done:
                    plan = planner_task.result()
                    break

                yield sse_event(
                    "status",
                    {
                        "stage": "planning_wait",
                        "message": "还在分析你的问题，我会在超时前自动切换到兜底方案",
                    },
                )
            logger.info(
                "chat planner finished request_id=%s elapsed=%.2fs intent=%s tools=%d",
                context.request_id,
                time.monotonic() - planning_started_at,
                plan.intent,
                len(plan.tool_calls),
            )
        except TimeoutError:
            logger.warning(
                "chat planner timeout request_id=%s elapsed=%.2fs",
                context.request_id,
                time.monotonic() - planning_started_at,
            )
            plan = PlannerPlan(
                tool_calls=_fallback_tool_calls(request),
                answer_strategy="Planner 超时；根据用户问题和已有上下文直接回答。",
                intent="planner_timeout_fallback",
            )
            yield sse_event(
                "status",
                {
                    "stage": "planning_fallback",
                    "message": "工具规划耗时较久，将先根据已有上下文直接回答",
                },
            )
        except Exception as exc:
            logger.exception(
                "chat planner failed request_id=%s elapsed=%.2fs",
                context.request_id,
                time.monotonic() - planning_started_at,
            )
            plan = PlannerPlan(
                tool_calls=_fallback_tool_calls(request),
                answer_strategy="Planner 暂时不可用；根据已有上下文直接回答。",
                intent="planner_fallback",
            )
            yield sse_event(
                "status",
                {
                    "stage": "planning_fallback",
                    "message": f"工具规划暂时不可用，将直接回答：{str(exc)[:160]}",
                },
            )
        finally:
            if planner_task is not None and not planner_task.done():
                planner_task.cancel()

        executor = ToolExecutor(tools)
        tool_results: list[ExecutedToolResult] = []
        for call in plan.tool_calls[:6]:
            yield sse_event(
                "tool_call",
                {
                    "name": call.name,
                    "arguments": call.arguments,
                    "reason": call.reason,
                },
            )
            results = await executor.execute([call])
            logger.info(
                "chat tool executed request_id=%s name=%s statuses=%s",
                context.request_id,
                call.name,
                ",".join(result.record.status for result in results),
            )
            tool_results.extend(results)
            for result in results:
                yield sse_event(
                    "tool_result",
                    result.record.model_dump(),
                )

        sources = _collect_sources(tool_results)
        if sources:
            yield sse_event(
                "sources",
                {
                    "message": f"命中 {len(sources)} 条资料来源",
                    "sources": [source.model_dump() for source in sources],
                },
            )

        yield sse_event("status", {"stage": "generating", "message": "正在生成回答"})
        messages = self.prompt_service.build_answer_messages(
            request,
            plan,
            tool_results,
        )
        answer_parts: list[str] = []
        started_at = asyncio.get_running_loop().time()
        stream = self.llm_client.stream_text(messages).__aiter__()
        pending_next: asyncio.Task[str] | None = None
        delta_count = 0
        last_delta_at = started_at
        while True:
            elapsed = asyncio.get_running_loop().time() - started_at
            remaining = settings.answer_stream_total_timeout_seconds - elapsed
            if remaining <= 0:
                if pending_next is not None:
                    pending_next.cancel()
                raise RuntimeError("模型回答总耗时过长，请稍后重试或缩短问题。")

            if pending_next is None:
                pending_next = asyncio.create_task(stream.__anext__())

            wait_timeout = min(_ANSWER_HEARTBEAT_SECONDS, remaining)
            try:
                done, _ = await asyncio.wait(
                    {pending_next},
                    timeout=wait_timeout,
                    return_when=asyncio.FIRST_COMPLETED,
                )
            except asyncio.CancelledError:
                pending_next.cancel()
                raise

            now = asyncio.get_running_loop().time()
            if not done:
                if now - last_delta_at >= settings.answer_stream_idle_timeout_seconds:
                    pending_next.cancel()
                    logger.warning(
                        "chat answer idle timeout request_id=%s elapsed=%.2fs deltas=%d",
                        context.request_id,
                        now - started_at,
                        delta_count,
                    )
                    raise RuntimeError("模型流式输出长时间没有响应，请稍后重试。")
                yield sse_event(
                    "status",
                    {
                        "stage": "generating_wait",
                        "message": "模型还在生成，我继续等待响应",
                    },
                )
                continue

            try:
                delta = pending_next.result()
            except StopAsyncIteration:
                pending_next = None
                break
            pending_next = None

            delta_count += 1
            last_delta_at = now
            answer_parts.append(delta)
            yield sse_event("delta", {"content": delta})

        answer = "".join(answer_parts).strip()
        logger.info(
            "chat answer stream finished request_id=%s elapsed=%.2fs deltas=%d chars=%d",
            context.request_id,
            asyncio.get_running_loop().time() - started_at,
            delta_count,
            len(answer),
        )
        answer, safety_flags = self.safety_service.validate_answer(
            answer,
            request.hint_level,
        )
        if answer != "".join(answer_parts).strip():
            yield sse_event("delta", {"content": "\n\n" + answer})

        response = AgentChatResponse(
            request_id=context.request_id,
            user_id=request.user.user_id,
            conversation_id=request.conversation.conversation_id,
            problem_id=_resolve_problem_id(request, tool_results),
            submission_id=_resolve_submission_id(request, tool_results),
            answer=answer,
            intent=plan.intent,
            knowledge_points=[],
            sources=sources,
            tool_calls=[result.record for result in tool_results],
            metadata={
                "answer_strategy": plan.answer_strategy,
                "safety_flags": safety_flags,
            },
            model=self.llm_client.settings_chat_model(),
            provider="OpenRouter",
            generated_at=unix_now(),
        )
        yield sse_event("done", response.model_dump())


def _collect_sources(tool_results: list[ExecutedToolResult]):
    sources = []
    seen: set[tuple[str, int | None]] = set()
    for result in tool_results:
        for source in result.sources:
            key = (source.source, source.chunk_index)
            if key in seen:
                continue
            seen.add(key)
            sources.append(source)
    return sources


def _fallback_tool_calls(request: AgentChatRequest) -> list[PlannerToolCall]:
    calls: list[PlannerToolCall] = []
    problem_id = request.initial_context.problem_id
    if problem_id is None:
        problem_id = _extract_problem_id(request.message)
    if problem_id is not None:
        calls.append(
            PlannerToolCall(
                name="get_problem",
                arguments={"problem_id": problem_id},
                reason="使用初始题目上下文作为 Planner fallback。",
            )
        )
    submission_id = request.initial_context.submission_id or _extract_submission_id(
        request.message
    )
    if submission_id:
        calls.append(
            PlannerToolCall(
                name="get_submission",
                arguments={"submission_id": submission_id},
                reason="使用初始提交上下文作为 Planner fallback。",
            )
        )
    return calls


def _extract_problem_id(message: str) -> int | None:
    for pattern in [
        r"(?:题目|题号|problem|pid)\s*#?\s*(\d{1,10})",
        r"#\s*(\d{1,10})",
        r"(?<!\d)(\d{3,10})(?!\d)",
    ]:
        match = re.search(pattern, message, flags=re.IGNORECASE)
        if not match:
            continue
        try:
            value = int(match.group(1))
        except ValueError:
            continue
        if value > 0:
            return value
    return None


def _extract_submission_id(message: str) -> str:
    match = re.search(r"\b(sub[_\-A-Za-z0-9]+)\b", message, flags=re.IGNORECASE)
    return match.group(1) if match else ""


def _resolve_problem_id(
    request: AgentChatRequest,
    tool_results: list[ExecutedToolResult],
) -> int | None:
    if request.initial_context.problem_id is not None:
        return request.initial_context.problem_id
    for result in tool_results:
        value = result.metadata.get("problem_id")
        if isinstance(value, int):
            return value
    return None


def _resolve_submission_id(
    request: AgentChatRequest,
    tool_results: list[ExecutedToolResult],
) -> str | None:
    if request.initial_context.submission_id:
        return request.initial_context.submission_id
    for result in tool_results:
        value = result.metadata.get("submission_id")
        if isinstance(value, str) and value:
            return value
    return None
