import re

from app.schemas.chat import AgentChatRequest, ExecutedToolResult, PlannerToolCall
from app.schemas.chat import PlannerPlan


def collect_sources(tool_results: list[ExecutedToolResult]):
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


def fallback_tool_calls(request: AgentChatRequest) -> list[PlannerToolCall]:
    calls: list[PlannerToolCall] = []
    problem_id = request.initial_context.problem_id
    if problem_id is None:
        problem_id = extract_problem_id(request.message)
    if problem_id is not None:
        calls.append(
            PlannerToolCall(
                name="get_problem",
                arguments={"problem_id": problem_id},
                reason="使用初始题目上下文作为 Planner fallback。",
            )
        )
    submission_id = request.initial_context.submission_id or extract_submission_id(
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


def extract_problem_id(message: str) -> int | None:
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


def extract_submission_id(message: str) -> str:
    match = re.search(r"\b(sub[_\-A-Za-z0-9]+)\b", message, flags=re.IGNORECASE)
    return match.group(1) if match else ""


def resolve_problem_id(
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


def resolve_submission_id(
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


def render_plan_markdown(plan: PlannerPlan) -> str:
    lines = [
        "### 本轮计划",
        "",
        f"- 意图：{plan.intent or 'chat'}",
        f"- 策略：{plan.answer_strategy or '根据已知上下文直接回答'}",
    ]
    if plan.conversation_title.strip():
        lines.append(f"- 会话标题：{plan.conversation_title.strip()}")
    if plan.rewritten_question.strip():
        lines.extend(
            [
                f"- 重述问题：{plan.rewritten_question.strip()}",
            ]
        )
    if plan.tool_calls:
        lines.extend(
            [
                "",
                "#### 工具调用",
            ]
        )
        for index, call in enumerate(plan.tool_calls, start=1):
            reason = call.reason.strip() or "无"
            lines.extend(
                [
                    f"{index}. `{call.name}`",
                    f"   - 原因：{reason}",
                    f"   - 参数：`{call.arguments}`",
                ]
            )
    else:
        lines.extend(["", "- 本轮不需要调用外部工具。"])
    return "\n".join(lines).strip()
