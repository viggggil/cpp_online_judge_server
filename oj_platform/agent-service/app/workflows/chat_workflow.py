from collections.abc import AsyncIterator

from app.api.dependencies import RequestContext
from app.graphs.chat_graph import build_chat_graph
from app.graphs.utils import fallback_tool_calls
from app.schemas.chat import AgentChatRequest
from app.schemas.chat import PlannerToolCall
from app.schemas.streaming import sse_event


class ChatWorkflow:
    def __init__(self) -> None:
        self.graph = build_chat_graph()

    async def run_stage_stream(
        self,
        request: AgentChatRequest,
        context: RequestContext,
    ) -> AsyncIterator[str]:
        initial_state = {
            "request": request,
            "context": context,
        }
        config = {
            "configurable": {
                "thread_id": request.conversation.conversation_id or context.request_id,
            }
        }
        async for chunk in self.graph.astream(
            initial_state,
            config=config,
            stream_mode="custom",
        ):
            if not isinstance(chunk, dict):
                continue
            event = chunk.get("event")
            data = chunk.get("data")
            if isinstance(event, str) and isinstance(data, dict):
                yield sse_event(event, data)


def _fallback_tool_calls(request: AgentChatRequest) -> list[PlannerToolCall]:
    return fallback_tool_calls(request)
