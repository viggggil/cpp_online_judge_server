from pydantic import BaseModel, ConfigDict, Field


class UserContext(BaseModel):
    model_config = ConfigDict(extra="forbid")

    user_id: int


class ProblemContext(BaseModel):
    model_config = ConfigDict(extra="ignore")

    problem_id: int
    title: str
    description_markdown: str = ""
    input_description: str = ""
    output_description: str = ""
    public_examples: list[dict[str, str]] = Field(default_factory=list)
    tags: list[str] = Field(default_factory=list)
    difficulty: str = ""
    time_limit_ms: int = 0
    memory_limit_mb: int = 0


class ProblemSummary(BaseModel):
    model_config = ConfigDict(extra="ignore")

    problem_id: int
    title: str = ""
    tags: list[str] = Field(default_factory=list)
    difficulty: str = ""


class SubmissionContext(BaseModel):
    model_config = ConfigDict(extra="ignore")

    submission_id: str
    problem_id: int
    owner_user_id: int
    language: str = ""
    source_code: str = ""
    judge_status: str = ""
    compiler_output: str = ""
    runtime_stderr: str = ""
    execution_time_ms: int = 0
    memory_usage_kb: int = 0
    submitted_at: int = 0


class SubmissionSummary(BaseModel):
    model_config = ConfigDict(extra="ignore")

    submission_id: str
    problem_id: int | None = None
    status: str = ""
    judge_status: str = ""
    execution_time_ms: int = 0
    memory_usage_kb: int = 0
    language: str = ""
    submitted_at: int = 0
