from typing import Any, TypedDict

from langchain_core.tools import BaseTool

from app.api.dependencies import RequestContext
from app.schemas.chat import AgentChatRequest, ExecutedToolResult, PlannerPlan
from app.schemas.rag import SourceReference


class ChatGraphState(TypedDict, total=False):
    request: AgentChatRequest
    context: RequestContext
    tools: list[BaseTool]
    plan: PlannerPlan
    tool_results: list[ExecutedToolResult]
    sources: list[SourceReference]
    messages: list[dict[str, str]]
    answer: str
    safety_flags: list[str]
    used_non_stream_fallback: bool
    metadata: dict[str, Any]

