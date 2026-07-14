from uuid import uuid4

from app.api.dependencies import RequestContext
from app.clients.openrouter_client import OpenRouterClient, unix_now
from app.schemas.diagnosis import (
    DiagnosisRequest,
    DiagnosisResponse,
    SubmissionDiagnosis,
)
from app.services.prompt_service import PromptService
from app.services.safety_service import SafetyService


class DiagnosisWorkflow:
    def __init__(self) -> None:
        self.prompt_service = PromptService()
        self.llm_client = OpenRouterClient()
        self.safety_service = SafetyService()

    async def run(
        self,
        request: DiagnosisRequest,
        context: RequestContext,
    ) -> DiagnosisResponse:
        if request.submission.problem_id != request.problem.problem_id:
            raise ValueError("submission does not belong to problem")
        if request.submission.owner_user_id != request.user.user_id:
            raise ValueError("submission does not belong to user")

        messages = self.prompt_service.build_diagnosis_messages(request)
        result = await self.llm_client.invoke_structured(
            messages=messages,
            response_model=SubmissionDiagnosis,
        )
        diagnosis = self.safety_service.validate(result.data, request.hint_level)

        return DiagnosisResponse(
            request_id=context.request_id,
            diagnosis_id=f"diag_{uuid4().hex}",
            user_id=request.user.user_id,
            problem_id=request.problem.problem_id,
            submission_id=request.submission.submission_id,
            judge_status=request.submission.judge_status,
            hint_level=request.hint_level,
            error_type=diagnosis.error_type,
            summary=diagnosis.summary,
            analysis=diagnosis.analysis,
            evidence=diagnosis.evidence,
            knowledge_points=diagnosis.knowledge_points,
            hints=diagnosis.hints,
            confidence=diagnosis.confidence,
            sources=[],
            model=result.model,
            provider=result.provider,
            generated_at=unix_now(),
        )
