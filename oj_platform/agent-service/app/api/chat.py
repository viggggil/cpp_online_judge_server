from fastapi import APIRouter, Depends
from fastapi.responses import StreamingResponse

from app.api.dependencies import RequestContext, require_internal_context
from app.schemas.chat import AgentChatRequest
from app.schemas.streaming import error_event_stream
from app.workflows.chat_workflow import ChatWorkflow


router = APIRouter(prefix="/api/v1", tags=["chat"])


@router.post("/chat/stream")
async def create_chat_stream(
    request: AgentChatRequest,
    context: RequestContext = Depends(require_internal_context),
) -> StreamingResponse:
    async def stream():
        try:
            async for event in ChatWorkflow().run_stage_stream(request, context):
                yield event
        except Exception as exc:
            async for event in error_event_stream(str(exc)):
                yield event

    return StreamingResponse(
        stream(),
        media_type="text/event-stream",
        headers={
            "Cache-Control": "no-cache",
            "X-Accel-Buffering": "no",
        },
    )
