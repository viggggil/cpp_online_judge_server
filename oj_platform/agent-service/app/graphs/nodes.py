import asyncio
import logging
import time

from langgraph.config import get_stream_writer

from app.clients.openrouter_client import OpenRouterClient, unix_now
from app.core.config import get_settings
from app.graphs.chat_state import ChatGraphState
from app.graphs.utils import (
    collect_sources,
    fallback_tool_calls,
    resolve_problem_id,
    resolve_submission_id,
    render_plan_markdown,
)
from app.schemas.chat import AgentChatResponse, ExecutedToolResult, PlannerPlan
from app.services.planner_service import PlannerService
from app.services.prompt_service import PromptService
from app.services.safety_service import SafetyService
from app.tools.executor import ToolExecutor
from app.tools.oj_tools import OjTools
from app.tools.rag_tools import build_retrieve_knowledge_tool


logger = logging.getLogger(__name__)
_PLANNER_HEARTBEAT_SECONDS = 8
_ANSWER_HEARTBEAT_SECONDS = 10


def _emit(event: str, data: dict) -> None:
    get_stream_writer()({"event": event, "data": data})


async def prepare_node(state: ChatGraphState) -> dict:
    request = state["request"]
    context = state["context"]
    tools = [
        *OjTools(
            user_id=request.user.user_id,
            request_id=context.request_id,
        ).build_tools(),
        build_retrieve_knowledge_tool(),
    ]
    return {
        "tools": tools,
        "tool_results": [],
        "sources": [],
        "used_non_stream_fallback": False,
        "metadata": {},
    }


async def plan_node(state: ChatGraphState) -> dict:
    request = state["request"]
    context = state["context"]
    tools = state["tools"]
    settings = get_settings()
    planner = PlannerService()
    started_at = time.monotonic()
    planner_task: asyncio.Task[PlannerPlan] | None = None

    _emit("status", {"stage": "planning", "message": "正在理解你的问题"})
    try:
        planner_task = asyncio.create_task(planner.plan(request, tools))
        while True:
            elapsed = time.monotonic() - started_at
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

            _emit(
                "status",
                {
                    "stage": "planning_wait",
                    "message": "还在分析你的问题，我会在超时前自动切换到兜底方案",
                },
            )

        logger.info(
            "chat planner finished request_id=%s elapsed=%.2fs intent=%s tools=%d",
            context.request_id,
            time.monotonic() - started_at,
            plan.intent,
            len(plan.tool_calls),
        )
    except TimeoutError:
        logger.warning(
            "chat planner timeout request_id=%s elapsed=%.2fs",
            context.request_id,
            time.monotonic() - started_at,
        )
        plan = PlannerPlan(
            tool_calls=fallback_tool_calls(request),
            answer_strategy="Planner 超时；根据用户问题和已有上下文直接回答。",
            intent="planner_timeout_fallback",
            rewritten_question=request.message,
        )
        _emit(
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
            time.monotonic() - started_at,
        )
        plan = PlannerPlan(
            tool_calls=fallback_tool_calls(request),
            answer_strategy="Planner 暂时不可用；根据用户问题和已有上下文直接回答。",
            intent="planner_fallback",
            rewritten_question=request.message,
        )
        _emit(
            "status",
            {
                "stage": "planning_fallback",
                "message": f"工具规划暂时不可用，将直接回答：{str(exc)[:160]}",
            },
        )
    finally:
        if planner_task is not None and not planner_task.done():
            planner_task.cancel()

    return {"plan": plan}


async def emit_plan_preview_node(state: ChatGraphState) -> dict:
    plan = state["plan"]
    preview = render_plan_markdown(plan)
    for line in preview.splitlines():
        _emit("plan_delta", {"content": line + "\n"})
    _emit("plan_done", {"intent": plan.intent, "tool_count": len(plan.tool_calls)})
    return {}


async def execute_tools_node(state: ChatGraphState) -> dict:
    context = state["context"]
    plan = state["plan"]
    executor = ToolExecutor(state["tools"])
    tool_results: list[ExecutedToolResult] = []

    for call in plan.tool_calls[:6]:
        _emit(
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
            _emit("tool_result", result.record.model_dump())

    return {"tool_results": tool_results}


async def collect_sources_node(state: ChatGraphState) -> dict:
    sources = collect_sources(state.get("tool_results", []))
    if sources:
        _emit(
            "sources",
            {
                "message": f"命中 {len(sources)} 条资料来源",
                "sources": [source.model_dump() for source in sources],
            },
        )
    return {"sources": sources}


async def build_answer_messages_node(state: ChatGraphState) -> dict:
    messages = PromptService().build_answer_messages(
        state["request"],
        state["plan"],
        state.get("tool_results", []),
    )
    return {"messages": messages}


async def generate_answer_node(state: ChatGraphState) -> dict:
    settings = get_settings()
    context = state["context"]
    request = state["request"]
    llm_client = OpenRouterClient()
    messages = state["messages"]

    _emit("status", {"stage": "generating", "message": "正在生成回答"})
    answer_parts: list[str] = []
    started_at = asyncio.get_running_loop().time()
    stream = llm_client.stream_text(messages).__aiter__()
    pending_next: asyncio.Task[str] | None = None
    delta_count = 0
    last_delta_at = started_at
    used_non_stream_fallback = False

    while True:
        elapsed = asyncio.get_running_loop().time() - started_at
        remaining = settings.answer_stream_total_timeout_seconds - elapsed
        if remaining <= 0:
            if pending_next is not None:
                pending_next.cancel()
            answer_parts = await _fallback_complete_answer(
                llm_client,
                messages,
                answer_parts,
                context.request_id,
                reason="模型回答总耗时过长",
            )
            used_non_stream_fallback = True
            break

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
                _emit(
                    "status",
                    {
                        "stage": "generating_fallback",
                        "message": "流式输出不稳定，正在切换为完整生成",
                    },
                )
                answer_parts = await _fallback_complete_answer(
                    llm_client,
                    messages,
                    answer_parts,
                    context.request_id,
                    reason="模型流式输出长时间没有响应",
                )
                used_non_stream_fallback = True
                break
            _emit(
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
        except Exception as exc:
            pending_next = None
            logger.warning(
                "chat answer stream failed request_id=%s elapsed=%.2fs deltas=%d error=%s",
                context.request_id,
                now - started_at,
                delta_count,
                exc,
            )
            _emit(
                "status",
                {
                    "stage": "generating_fallback",
                    "message": "流式输出中断，正在切换为完整生成",
                },
            )
            answer_parts = await _fallback_complete_answer(
                llm_client,
                messages,
                answer_parts,
                context.request_id,
                reason=str(exc),
            )
            used_non_stream_fallback = True
            break
        pending_next = None

        delta_count += 1
        last_delta_at = now
        answer_parts.append(delta)
        _emit("delta", {"content": delta})

    answer = "".join(answer_parts).strip()
    logger.info(
        "chat answer stream finished request_id=%s elapsed=%.2fs deltas=%d chars=%d",
        context.request_id,
        asyncio.get_running_loop().time() - started_at,
        delta_count,
        len(answer),
    )

    original_answer = answer
    answer, safety_flags = SafetyService().validate_answer(answer, request.hint_level)
    if answer != original_answer:
        _emit("delta", {"content": "\n\n" + answer})

    return {
        "answer": answer,
        "safety_flags": safety_flags,
        "used_non_stream_fallback": used_non_stream_fallback,
    }


async def finalize_node(state: ChatGraphState) -> dict:
    request = state["request"]
    context = state["context"]
    plan = state["plan"]
    tool_results = state.get("tool_results", [])
    sources = state.get("sources", [])
    llm_client = OpenRouterClient()

    response = AgentChatResponse(
        request_id=context.request_id,
        user_id=request.user.user_id,
        title=plan.conversation_title,
        conversation_id=request.conversation.conversation_id,
        problem_id=resolve_problem_id(request, tool_results),
        submission_id=resolve_submission_id(request, tool_results),
        answer=state.get("answer", ""),
        intent=plan.intent,
        knowledge_points=[],
        sources=sources,
        tool_calls=[result.record for result in tool_results],
        metadata={
            "used_non_stream_fallback": state.get("used_non_stream_fallback", False),
            "safety_flags": state.get("safety_flags", []),
            "workflow_engine": "langgraph",
        },
        model=llm_client.settings_chat_model(),
        provider="OpenRouter",
        generated_at=unix_now(),
    )
    _emit("done", response.model_dump())
    return {"metadata": {"done": True}}


async def _fallback_complete_answer(
    llm_client: OpenRouterClient,
    messages: list[dict[str, str]],
    current_parts: list[str],
    request_id: str,
    reason: str,
) -> list[str]:
    settings = get_settings()
    try:
        answer = await asyncio.wait_for(
            llm_client.complete_text(messages),
            timeout=settings.answer_fallback_timeout_seconds,
        )
    except Exception:
        logger.exception(
            "chat non-stream fallback failed request_id=%s partial_chars=%d reason=%s",
            request_id,
            len("".join(current_parts)),
            reason,
        )
        if current_parts:
            current = "".join(current_parts).strip()
            current += "\n\n> 模型流式输出中断，已保留当前已生成内容。"
            return [current]
        raise RuntimeError("模型生成中断，非流式补全也未成功，请稍后重试。")

    logger.info(
        "chat non-stream fallback succeeded request_id=%s chars=%d reason=%s",
        request_id,
        len(answer),
        reason,
    )
    return [answer]
