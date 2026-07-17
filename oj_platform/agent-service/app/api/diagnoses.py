from fastapi import APIRouter, Depends, HTTPException
from fastapi.responses import StreamingResponse

from app.api.dependencies import RequestContext, require_internal_context
from app.schemas.diagnosis import DiagnosisRequest, DiagnosisResponse
from app.schemas.streaming import error_event_stream
from app.workflows.diagnosis_workflow import DiagnosisWorkflow


router = APIRouter(prefix="/api/v1", tags=["diagnoses"])


@router.post("/diagnoses", response_model=DiagnosisResponse)
async def create_diagnosis(
    request: DiagnosisRequest,
    context: RequestContext = Depends(require_internal_context),
) -> DiagnosisResponse:
    try:
        return await DiagnosisWorkflow().run(request, context)
    except ValueError as exc:
        raise HTTPException(status_code=400, detail=str(exc)) from exc
    except Exception as exc:
        raise HTTPException(status_code=502, detail=str(exc)) from exc


@router.post("/diagnoses/stream")
async def create_diagnosis_stream(
    request: DiagnosisRequest,
    context: RequestContext = Depends(require_internal_context),
) -> StreamingResponse:
    async def stream():
        try:
            async for event in DiagnosisWorkflow().run_stage_stream(request, context):
                yield event
        except ValueError as exc:
            async for event in error_event_stream(str(exc)):
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
