from collections.abc import AsyncIterator

from app.api.dependencies import RequestContext
from app.clients.openrouter_client import OpenRouterClient, unix_now
from app.schemas.chat import (
    AgentChatRequest,
    AgentChatResponse,
    ExecutedToolResult,
    PlannerPlan,
)
from app.schemas.streaming import sse_event
from app.services.planner_service import PlannerService
from app.services.prompt_service import PromptService
from app.services.safety_service import SafetyService
from app.tools.executor import ToolExecutor
from app.tools.oj_tools import OjTools
from app.tools.rag_tools import build_retrieve_knowledge_tool


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
        yield sse_event("status", {"stage": "planning", "message": "正在理解你的问题"})

        tools = [
            *OjTools(
                user_id=request.user.user_id,
                request_id=context.request_id,
            ).build_tools(),
            build_retrieve_knowledge_tool(),
        ]
        try:
            plan = await self.planner_service.plan(request, tools)
        except Exception as exc:
            plan = PlannerPlan(
                tool_calls=[],
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
        async for delta in self.llm_client.stream_text(messages):
            answer_parts.append(delta)
            yield sse_event("delta", {"content": delta})

        answer = "".join(answer_parts).strip()
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
