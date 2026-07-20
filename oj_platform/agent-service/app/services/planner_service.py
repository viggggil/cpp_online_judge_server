import re

from langchain_core.tools import BaseTool

from app.clients.openrouter_client import OpenRouterClient
from app.schemas.chat import AgentChatRequest, PlannerPlan, PlannerToolCall
from app.services.prompt_service import PromptService


class PlannerService:
    def __init__(self) -> None:
        self.prompt_service = PromptService()
        self.llm_client = OpenRouterClient()

    async def plan(
        self,
        request: AgentChatRequest,
        tools: list[BaseTool],
    ) -> PlannerPlan:
        tool_descriptions = [
            f"- {tool.name}: {tool.description or ''}; args_schema={_schema_text(tool)}"
            for tool in tools
        ]
        messages = self.prompt_service.build_planner_messages(
            request,
            tool_descriptions,
        )
        result = await self.llm_client.invoke_structured(
            messages=messages,
            response_model=PlannerPlan,
        )
        return self._normalize_plan(request, result.data, tools)

    def _normalize_plan(
        self,
        request: AgentChatRequest,
        plan: PlannerPlan,
        tools: list[BaseTool],
    ) -> PlannerPlan:
        allowed = {tool.name for tool in tools}
        normalized_calls: list[PlannerToolCall] = []
        for call in plan.tool_calls[:6]:
            arguments = _normalize_tool_arguments(
                call.name,
                call.arguments,
                request,
            )
            normalized_calls.append(
                PlannerToolCall(
                    name=call.name if call.name in allowed else call.name,
                    arguments=arguments,
                    reason=call.reason[:300],
                )
            )
        return PlannerPlan(
            tool_calls=normalized_calls,
            answer_strategy=plan.answer_strategy[:500],
            intent=plan.intent[:64],
        )


def _schema_text(tool: BaseTool) -> str:
    schema = getattr(tool, "args_schema", None)
    if schema is None:
        return "{}"
    try:
        return schema.model_json_schema()
    except Exception:
        return str(schema)


def _normalize_tool_arguments(
    tool_name: str,
    arguments: dict,
    request: AgentChatRequest,
) -> dict:
    normalized = dict(arguments or {})
    normalized.pop("user_id", None)
    normalized.pop("owner_user_id", None)

    if tool_name in {"get_problem", "list_problem_submissions"}:
        _rename_first_present(
            normalized,
            "problem_id",
            ["problemId", "problemID", "id", "pid", "problem"],
        )
        if "problem_id" not in normalized and request.initial_context.problem_id is not None:
            normalized["problem_id"] = request.initial_context.problem_id
        if "problem_id" not in normalized:
            problem_id = _extract_problem_id(request.message)
            if problem_id is not None:
                normalized["problem_id"] = problem_id

    if tool_name == "get_submission":
        _rename_first_present(
            normalized,
            "submission_id",
            ["submissionId", "submissionID", "id", "sid", "submission"],
        )
        if "submission_id" not in normalized and request.initial_context.submission_id:
            normalized["submission_id"] = request.initial_context.submission_id
        if "submission_id" not in normalized:
            submission_id = _extract_submission_id(request.message)
            if submission_id:
                normalized["submission_id"] = submission_id

    if tool_name == "get_conversation_history":
        _rename_first_present(
            normalized,
            "conversation_id",
            ["conversationId", "conversationID", "id", "conversation"],
        )
        if "conversation_id" not in normalized and request.conversation.conversation_id:
            normalized["conversation_id"] = request.conversation.conversation_id

    if tool_name == "retrieve_knowledge":
        _rename_first_present(
            normalized,
            "problem_id",
            ["problemId", "problemID", "pid", "problem"],
        )
        if "problem_id" not in normalized and request.initial_context.problem_id is not None:
            normalized["problem_id"] = request.initial_context.problem_id
        if "problem_id" not in normalized:
            problem_id = _extract_problem_id(request.message)
            if problem_id is not None:
                normalized["problem_id"] = problem_id

    return {
        key: value
        for key, value in normalized.items()
        if value is not None and value != ""
    }


def _rename_first_present(
    arguments: dict,
    canonical_name: str,
    aliases: list[str],
) -> None:
    if canonical_name in arguments:
        return
    for alias in aliases:
        if alias not in arguments:
            continue
        arguments[canonical_name] = arguments.pop(alias)
        return


def _extract_problem_id(message: str) -> int | None:
    patterns = [
        r"(?:题目|题号|problem|pid)\s*#?\s*(\d{1,10})",
        r"#\s*(\d{1,10})",
        r"(?<!\d)(\d{3,10})(?!\d)",
    ]
    for pattern in patterns:
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
