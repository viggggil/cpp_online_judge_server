from pydantic import BaseModel, ConfigDict, Field


class JudgeTestCase(BaseModel):
    model_config = ConfigDict(extra="forbid")

    input: str = Field(default="", max_length=20000)
    expected_output: str = Field(default="", max_length=20000)


class JudgeTestCaseResult(BaseModel):
    model_config = ConfigDict(extra="ignore")

    status: str = ""
    input: str = ""
    time_used_ms: int = 0
    memory_used_kb: int = 0
    actual_output: str = ""
    expected_output: str = ""
    error_message: str = ""


class JudgeResponse(BaseModel):
    model_config = ConfigDict(extra="ignore")

    submission_id: int = 0
    final_status: str = ""
    compile_success: bool = False
    compile_stdout: str = ""
    compile_stderr: str = ""
    total_time_used_ms: int = 0
    peak_memory_used_kb: int = 0
    system_message: str = ""
    test_case_results: list[JudgeTestCaseResult] = Field(default_factory=list)


class GeneratedCodeCandidate(BaseModel):
    model_config = ConfigDict(extra="forbid")

    language: str = "cpp"
    source_code: str = Field(min_length=1, max_length=30000)
    test_cases: list[JudgeTestCase] = Field(default_factory=list, max_length=8)
    notes: str = Field(default="", max_length=1000)
