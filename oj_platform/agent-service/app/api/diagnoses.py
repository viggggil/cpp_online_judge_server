from fastapi import APIRouter, Depends, HTTPException

from app.api.dependencies import RequestContext, require_internal_context
from app.schemas.diagnosis import DiagnosisRequest, DiagnosisResponse
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
