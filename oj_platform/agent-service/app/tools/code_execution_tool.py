from langchain_core.prompts import ChatPromptTemplate
from langchain_core.tools import StructuredTool
from pydantic import BaseModel, Field

from app.clients.judge_client import JudgeClient
from app.clients.oj_client import OjClient
from app.clients.openrouter_client import OpenRouterClient
from app.core.config import get_settings
from app.llm.messages import lc_messages_to_openrouter
from app.schemas.chat import ExecutedToolResult, ToolCallRecord
from app.schemas.judge import GeneratedCodeCandidate, JudgeResponse, JudgeTestCase
from app.schemas.oj import ProblemContext


class GenerateAndRunCodeArgs(BaseModel):
    problem_id: int = Field(gt=0)
    objective: str = Field(
        default="生成一个候选 C++ 解法并用小样例执行验证。",
        min_length=1,
        max_length=500,
    )
    test_cases: list[JudgeTestCase] = Field(default_factory=list, max_length=6)


class CodeExecutionTool:
    def __init__(self, *, request_id: str) -> None:
        self.request_id = request_id
        self.oj_client = OjClient(request_id=request_id)
        self.llm_client = OpenRouterClient()
        self.judge_client = JudgeClient()

    def build_tool(self) -> StructuredTool:
        return StructuredTool.from_function(
            coroutine=self.generate_and_run_code,
            name="generate_and_run_code",
            description=(
                "让受控 LLM 为指定题目生成一份临时 C++ 候选程序，并调用 judge_worker "
                "在小样例上编译运行。只用于验证思路、构造反例或定位可能问题；"
                "不会创建正式提交，不能证明代码可以 AC。"
            ),
            args_schema=GenerateAndRunCodeArgs,
        )

    async def generate_and_run_code(
        self,
        problem_id: int,
        objective: str = "生成一个候选 C++ 解法并用小样例执行验证。",
        test_cases: list[JudgeTestCase] | None = None,
    ) -> ExecutedToolResult:
        provided_cases = test_cases or []
        problem = await self.oj_client.get_problem(problem_id)
        candidate = await self._generate_candidate(problem, objective, provided_cases)

        effective_cases = provided_cases or candidate.test_cases
        if not effective_cases:
            return ExecutedToolResult(
                record=ToolCallRecord(
                    name="generate_and_run_code",
                    arguments={"problem_id": problem_id, "objective": objective},
                    status="error",
                    summary="未能生成可执行测试点，已跳过 judge_worker 调用",
                ),
                content="Codegen LLM 没有提供测试点，因此没有执行生成代码。",
                metadata={"problem_id": problem_id},
            )

        judge_response = await self.judge_client.judge_generated_code(
            problem_id=problem.problem_id,
            language=candidate.language,
            source_code=candidate.source_code,
            test_cases=effective_cases,
            time_limit_ms=problem.time_limit_ms,
            memory_limit_mb=problem.memory_limit_mb,
        )

        status = "ok" if judge_response.final_status == "OK" else "error"
        return ExecutedToolResult(
            record=ToolCallRecord(
                name="generate_and_run_code",
                arguments={
                    "problem_id": problem_id,
                    "objective": objective,
                    "test_case_count": len(effective_cases),
                },
                status=status,
                summary=_judge_summary(judge_response),
            ),
            content=_format_execution_result(
                problem,
                objective,
                candidate,
                judge_response,
                len(effective_cases),
            ),
            metadata={
                "problem_id": problem.problem_id,
                "generated_language": candidate.language,
                "judge_status": judge_response.final_status,
                "test_case_count": len(effective_cases),
                "compile_success": judge_response.compile_success,
            },
        )

    async def _generate_candidate(
        self,
        problem: ProblemContext,
        objective: str,
        provided_cases: list[JudgeTestCase],
    ) -> GeneratedCodeCandidate:
        settings = get_settings()
        prompt = ChatPromptTemplate.from_messages(
            [
                (
                    "system",
                    """
你是在线判题平台内部的临时代码生成器。

任务：
1. 仅生成一份用于沙盒执行的 C++17 候选程序。
2. 如果用户没有给测试点，请同时给 2 到 4 个小测试点和确定的 expected_output。
3. 测试点必须能从题面或 objective 推导，不能编造隐藏数据。
4. 代码必须从 stdin 读入、向 stdout 输出，不读写文件，不使用网络，不创建线程。
5. 不要解释，不要 Markdown，只输出符合 JSON Schema 的对象。
""".strip(),
                ),
                (
                    "human",
                    """
题目：
ID: {problem_id}
标题: {title}
时间限制: {time_limit_ms} ms
内存限制: {memory_limit_mb} MB
标签: {tags}

题面：
{statement}

目标：
{objective}

外部提供的测试点：
{provided_cases}

请生成候选程序和必要测试点。
""".strip(),
                ),
            ]
        )
        messages = prompt.format_messages(
            problem_id=problem.problem_id,
            title=problem.title,
            time_limit_ms=problem.time_limit_ms or 1000,
            memory_limit_mb=problem.memory_limit_mb or 128,
            tags=", ".join(problem.tags) or "无",
            statement=problem.description_markdown[:8000],
            objective=objective[:500],
            provided_cases="\n".join(
                f"[{index}] input:\n{case.input}\nexpected_output:\n{case.expected_output}"
                for index, case in enumerate(provided_cases, start=1)
            )
            or "无",
        )
        result = await self.llm_client.invoke_structured(
            messages=lc_messages_to_openrouter(messages),
            response_model=GeneratedCodeCandidate,
            model=settings.codegen_model or settings.chat_model,
            provider_sort=settings.codegen_provider_sort,
            max_tokens=3500,
        )
        candidate = result.data
        candidate.language = "cpp"
        candidate.source_code = candidate.source_code[: settings.codegen_max_source_chars]
        candidate.test_cases = candidate.test_cases[: settings.codegen_max_test_cases]
        return candidate


def _judge_summary(response: JudgeResponse) -> str:
    if response.final_status == "OK":
        return (
            f"生成代码编译运行通过 {len(response.test_case_results)} 个小测试点，"
            f"耗时 {response.total_time_used_ms}ms，峰值内存 {response.peak_memory_used_kb}KB"
        )
    if not response.compile_success:
        detail = response.compile_stderr.strip()[:160] or "编译失败"
        return f"生成代码编译失败：{detail}"
    detail = response.system_message.strip()[:160]
    if not detail:
        failed = next(
            (
                case
                for case in response.test_case_results
                if case.status and case.status != "OK"
            ),
            None,
        )
        detail = failed.error_message[:160] if failed else response.final_status
    return f"生成代码执行结果：{response.final_status}，{detail}"


def _format_execution_result(
    problem: ProblemContext,
    objective: str,
    candidate: GeneratedCodeCandidate,
    response: JudgeResponse,
    test_case_count: int,
) -> str:
    lines = [
        "临时代码执行结果",
        f"problem_id: {problem.problem_id}",
        f"objective: {objective}",
        f"codegen_notes: {candidate.notes or '无'}",
        "说明: 生成源码用于内部沙盒验证，未写入正式提交；工具结果不能证明 AC。",
        f"language: {candidate.language}",
        f"test_case_count: {test_case_count}",
        f"final_status: {response.final_status}",
        f"compile_success: {response.compile_success}",
        f"total_time_used_ms: {response.total_time_used_ms}",
        f"peak_memory_used_kb: {response.peak_memory_used_kb}",
    ]
    if response.compile_stdout:
        lines.extend(["compile_stdout:", response.compile_stdout[:1200]])
    if response.compile_stderr:
        lines.extend(["compile_stderr:", response.compile_stderr[:2000]])
    if response.system_message:
        lines.extend(["system_message:", response.system_message[:1200]])
    if response.test_case_results:
        lines.append("test_case_results:")
        for index, case in enumerate(response.test_case_results[:8], start=1):
            lines.extend(
                [
                    f"- case {index}: {case.status}, {case.time_used_ms}ms, {case.memory_used_kb}KB",
                    f"  input: {case.input[:1000]!r}",
                    f"  expected_output: {case.expected_output[:1000]!r}",
                    f"  actual_output: {case.actual_output[:1000]!r}",
                ]
            )
            if case.error_message:
                lines.append(f"  error_message: {case.error_message[:1000]}")
    return "\n".join(lines)
