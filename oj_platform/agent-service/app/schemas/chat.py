from typing import Any, Literal

from pydantic import BaseModel, ConfigDict, Field

from app.schemas.oj import UserContext
from app.schemas.rag import SourceReference


class ConversationHistoryItem(BaseModel):
    model_config = ConfigDict(extra="forbid")

    round_no: int
    user_content: str
    assistant_content: str


class ConversationContext(BaseModel):
    model_config = ConfigDict(extra="forbid")

    conversation_id: str | None = None
    history: list[ConversationHistoryItem] = Field(default_factory=list)


class InitialContext(BaseModel):
    model_config = ConfigDict(extra="forbid")

    problem_id: int | None = None
    submission_id: str | None = None
    extra: dict[str, Any] = Field(default_factory=dict)


class AgentChatRequest(BaseModel):
    model_config = ConfigDict(extra="forbid")

    user: UserContext
    conversation: ConversationContext = Field(default_factory=ConversationContext)
    initial_context: InitialContext = Field(default_factory=InitialContext)
    hint_level: int = Field(default=2, ge=1, le=3)
    message: str = Field(min_length=1, max_length=1000)


class PlannerToolCall(BaseModel):
    model_config = ConfigDict(extra="forbid")

    name: str
    arguments: dict[str, Any] = Field(default_factory=dict)
    reason: str = ""


class PlannerPlan(BaseModel):
    model_config = ConfigDict(extra="forbid")

    tool_calls: list[PlannerToolCall] = Field(default_factory=list)
    answer_strategy: str = ""
    intent: str = ""
    rewritten_question: str = ""


class ToolCallRecord(BaseModel):
    model_config = ConfigDict(extra="forbid")

    name: str
    arguments: dict[str, Any] = Field(default_factory=dict)
    status: Literal["ok", "error", "skipped"] = "ok"
    summary: str = ""


class ExecutedToolResult(BaseModel):
    model_config = ConfigDict(extra="forbid")

    record: ToolCallRecord
    content: str = ""
    sources: list[SourceReference] = Field(default_factory=list)
    metadata: dict[str, Any] = Field(default_factory=dict)


class AgentChatResponse(BaseModel):
    model_config = ConfigDict(extra="forbid")

    request_id: str
    user_id: int
    conversation_id: str | None = None
    problem_id: int | None = None
    submission_id: str | None = None
    answer: str
    intent: str = ""
    knowledge_points: list[str] = Field(default_factory=list)
    sources: list[SourceReference] = Field(default_factory=list)
    tool_calls: list[ToolCallRecord] = Field(default_factory=list)
    metadata: dict[str, Any] = Field(default_factory=dict)
    model: str
    provider: str = ""
    generated_at: int
