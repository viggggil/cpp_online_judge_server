from typing import Literal

from pydantic import BaseModel, ConfigDict, Field


class UserContext(BaseModel):
    model_config = ConfigDict(extra="forbid")

    user_id: int


class ProblemContext(BaseModel):
    model_config = ConfigDict(extra="forbid")

    problem_id: int
    title: str
    description_markdown: str
    input_description: str = ""
    output_description: str = ""
    public_examples: list[dict[str, str]] = Field(default_factory=list)
    tags: list[str] = Field(default_factory=list)
    difficulty: str = ""
    time_limit_ms: int
    memory_limit_mb: int


class SubmissionContext(BaseModel):
    model_config = ConfigDict(extra="forbid")

    submission_id: str
    problem_id: int
    owner_user_id: int
    language: str
    source_code: str
    judge_status: str
    compiler_output: str = ""
    runtime_stderr: str = ""
    execution_time_ms: int = 0
    memory_usage_kb: int = 0
    submitted_at: int = 0


class ConversationHistoryItem(BaseModel):
    model_config = ConfigDict(extra="forbid")

    round_no: int
    user_content: str
    assistant_content: str


class ConversationContext(BaseModel):
    model_config = ConfigDict(extra="forbid")

    conversation_id: str | None = None
    history: list[ConversationHistoryItem] = Field(default_factory=list)


class DiagnosisRequest(BaseModel):
    model_config = ConfigDict(extra="forbid")

    user: UserContext
    problem: ProblemContext
    submission: SubmissionContext
    conversation: ConversationContext = Field(default_factory=ConversationContext)
    hint_level: int = Field(default=2, ge=1, le=3)
    question: str = Field(default="", max_length=1000)


class SubmissionDiagnosis(BaseModel):
    model_config = ConfigDict(extra="forbid")

    error_type: Literal[
        "compile_error",
        "wrong_answer",
        "time_limit_exceeded",
        "runtime_error",
        "accepted",
        "unknown",
    ]
    summary: str
    analysis: str
    evidence: list[str]
    knowledge_points: list[str]
    hints: list[str] = Field(min_length=1, max_length=4)
    confidence: float = Field(ge=0, le=1)


class SourceReference(BaseModel):
    model_config = ConfigDict(extra="forbid")

    document_id: str
    source: str
    title: str | None = None
    knowledge_point: str | None = None
    chunk_index: int | None = None
    score: float | None = None


class DiagnosisResponse(BaseModel):
    model_config = ConfigDict(extra="forbid")

    request_id: str
    diagnosis_id: str
    user_id: int
    problem_id: int
    submission_id: str
    judge_status: str
    hint_level: int
    error_type: str
    summary: str
    analysis: str
    evidence: list[str]
    knowledge_points: list[str]
    hints: list[str]
    confidence: float
    sources: list[SourceReference] = Field(default_factory=list)
    model: str
    provider: str = ""
    generated_at: int
