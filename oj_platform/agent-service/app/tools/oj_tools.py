import json

from langchain_core.tools import StructuredTool
from pydantic import BaseModel, Field

from app.clients.oj_client import OjClient
from app.schemas.chat import ExecutedToolResult, ToolCallRecord
from app.schemas.oj import ProblemContext, SubmissionContext


class GetProblemArgs(BaseModel):
    problem_id: int


class SearchProblemArgs(BaseModel):
    query: str = Field(min_length=1, max_length=200)


class GetSubmissionArgs(BaseModel):
    submission_id: str = Field(min_length=1, max_length=128)


class ListProblemSubmissionsArgs(BaseModel):
    problem_id: int
    limit: int = Field(default=20, ge=1, le=50)


class GetConversationHistoryArgs(BaseModel):
    conversation_id: str = Field(min_length=1, max_length=128)
    limit: int = Field(default=8, ge=1, le=20)


class OjTools:
    def __init__(self, *, user_id: int, request_id: str) -> None:
        self.user_id = user_id
        self.client = OjClient(request_id=request_id)

    def build_tools(self) -> list[StructuredTool]:
        return [
            StructuredTool.from_function(
                coroutine=self.get_problem,
                name="get_problem",
                description="查询公开题面、标签、时间和内存限制。",
                args_schema=GetProblemArgs,
            ),
            StructuredTool.from_function(
                coroutine=self.search_problem,
                name="search_problem",
                description="按题号、标题或关键词搜索题目。",
                args_schema=SearchProblemArgs,
            ),
            StructuredTool.from_function(
                coroutine=self.get_submission,
                name="get_submission",
                description="查询当前用户自己的指定提交、源码和判题摘要。",
                args_schema=GetSubmissionArgs,
            ),
            StructuredTool.from_function(
                coroutine=self.list_problem_submissions,
                name="list_problem_submissions",
                description="查询当前用户在某题的提交列表。",
                args_schema=ListProblemSubmissionsArgs,
            ),
            StructuredTool.from_function(
                coroutine=self.get_conversation_history,
                name="get_conversation_history",
                description="查询当前用户指定会话最近消息。",
                args_schema=GetConversationHistoryArgs,
            ),
        ]

    async def get_problem(self, problem_id: int) -> ExecutedToolResult:
        problem = await self.client.get_problem(problem_id)
        return ExecutedToolResult(
            record=ToolCallRecord(
                name="get_problem",
                arguments={"problem_id": problem_id},
                status="ok",
                summary=f"题目 {problem.problem_id}: {problem.title}",
            ),
            content=_format_problem(problem),
            metadata={"problem_id": problem.problem_id, "tags": problem.tags},
        )

    async def search_problem(self, query: str) -> ExecutedToolResult:
        await self.client.search_problem(query)
        return ExecutedToolResult(
            record=ToolCallRecord(
                name="search_problem",
                arguments={"query": query},
                status="ok",
                summary="题目搜索完成",
            )
        )

    async def get_submission(self, submission_id: str) -> ExecutedToolResult:
        submission = await self.client.get_submission(self.user_id, submission_id)
        return ExecutedToolResult(
            record=ToolCallRecord(
                name="get_submission",
                arguments={"submission_id": submission_id},
                status="ok",
                summary=(
                    f"{submission.submission_id}: {submission.judge_status}, "
                    f"{submission.execution_time_ms}ms, {submission.memory_usage_kb}KB"
                ),
            ),
            content=_format_submission(submission),
            metadata={
                "problem_id": submission.problem_id,
                "submission_id": submission.submission_id,
                "judge_status": submission.judge_status,
            },
        )

    async def list_problem_submissions(
        self,
        problem_id: int,
        limit: int = 20,
    ) -> ExecutedToolResult:
        submissions = await self.client.list_problem_submissions(
            self.user_id,
            problem_id,
            limit,
        )
        return ExecutedToolResult(
            record=ToolCallRecord(
                name="list_problem_submissions",
                arguments={"problem_id": problem_id, "limit": limit},
                status="ok",
                summary=f"查询到 {len(submissions)} 条提交",
            ),
            content=json.dumps(submissions, ensure_ascii=False, indent=2),
            metadata={"problem_id": problem_id},
        )

    async def get_conversation_history(
        self,
        conversation_id: str,
        limit: int = 8,
    ) -> ExecutedToolResult:
        history = await self.client.get_conversation_history(
            self.user_id,
            conversation_id,
            limit,
        )
        return ExecutedToolResult(
            record=ToolCallRecord(
                name="get_conversation_history",
                arguments={"conversation_id": conversation_id, "limit": limit},
                status="ok",
                summary=f"读取到 {len(history)} 轮历史消息",
            ),
            content="\n".join(
                f"第{item.round_no}轮 用户：{item.user_content}\n"
                f"第{item.round_no}轮 助手：{item.assistant_content}"
                for item in history
            ),
            metadata={"conversation_id": conversation_id},
        )


def _format_problem(problem: ProblemContext) -> str:
    return "\n".join(
        [
            f"题目 ID: {problem.problem_id}",
            f"标题: {problem.title}",
            f"时间限制: {problem.time_limit_ms} ms",
            f"内存限制: {problem.memory_limit_mb} MB",
            f"标签: {', '.join(problem.tags) or '无'}",
            "题面:",
            problem.description_markdown[:6000],
        ]
    )


def _format_submission(submission: SubmissionContext) -> str:
    return "\n".join(
        [
            f"submission_id: {submission.submission_id}",
            f"problem_id: {submission.problem_id}",
            f"语言: {submission.language}",
            f"判题状态: {submission.judge_status}",
            f"耗时: {submission.execution_time_ms} ms",
            f"内存: {submission.memory_usage_kb} KB",
            "源码:",
            f"```cpp\n{submission.source_code[:20000]}\n```",
            "编译输出:",
            f"```text\n{submission.compiler_output[:3000]}\n```",
            "运行错误:",
            f"```text\n{submission.runtime_stderr[:3000]}\n```",
        ]
    )
