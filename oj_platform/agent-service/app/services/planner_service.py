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
        return self._normalize_plan(result.data, tools)

    def _normalize_plan(
        self,
        plan: PlannerPlan,
        tools: list[BaseTool],
    ) -> PlannerPlan:
        allowed = {tool.name for tool in tools}
        normalized_calls: list[PlannerToolCall] = []
        for call in plan.tool_calls[:6]:
            arguments = dict(call.arguments)
            arguments.pop("user_id", None)
            arguments.pop("owner_user_id", None)
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
