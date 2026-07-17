from uuid import uuid4
from collections.abc import AsyncIterator

from app.api.dependencies import RequestContext
from app.clients.openrouter_client import OpenRouterClient, unix_now
from app.schemas.diagnosis import (
    DiagnosisRequest,
    DiagnosisResponse,
    SubmissionDiagnosis,
)
from app.rag.retriever import get_knowledge_retriever
from app.services.prompt_service import PromptService
from app.services.safety_service import SafetyService
from app.schemas.streaming import sse_event


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

        retrieved_documents = self._retrieve_knowledge(request)
        messages = self.prompt_service.build_diagnosis_messages(
            request,
            retrieved_documents,
        )
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
            sources=[document.source for document in retrieved_documents],
            model=result.model,
            provider=result.provider,
            generated_at=unix_now(),
        )

    def _retrieve_knowledge(self, request: DiagnosisRequest):
        try:
            return get_knowledge_retriever().retrieve(request)
        except Exception:
            return []

    async def run_stage_stream(
        self,
        request: DiagnosisRequest,
        context: RequestContext,
    ) -> AsyncIterator[str]:
        yield sse_event(
            "status",
            {"stage": "validating", "message": "正在校验请求"},
        )

        if request.submission.problem_id != request.problem.problem_id:
            raise ValueError("submission does not belong to problem")
        if request.submission.owner_user_id != request.user.user_id:
            raise ValueError("submission does not belong to user")

        yield sse_event(
            "status",
            {"stage": "retrieving_knowledge", "message": "正在检索知识库"},
        )
        retrieved_documents = self._retrieve_knowledge(request)
        yield sse_event(
            "sources",
            {
                "message": f"命中 {len(retrieved_documents)} 条知识库资料",
                "sources": [
                    document.source.model_dump()
                    for document in retrieved_documents
                ],
            },
        )

        yield sse_event(
            "status",
            {"stage": "generating", "message": "正在生成诊断"},
        )
        messages = self.prompt_service.build_diagnosis_messages(
            request,
            retrieved_documents,
        )
        result = await self.llm_client.invoke_structured(
            messages=messages,
            response_model=SubmissionDiagnosis,
        )
        diagnosis = self.safety_service.validate(result.data, request.hint_level)

        response = DiagnosisResponse(
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
            sources=[document.source for document in retrieved_documents],
            model=result.model,
            provider=result.provider,
            generated_at=unix_now(),
        )
        yield sse_event("done", response.model_dump())
