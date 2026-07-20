import pytest
from langchain_core.tools import StructuredTool
from pydantic import BaseModel

from app.schemas.chat import AgentChatRequest, ExecutedToolResult, PlannerToolCall
from app.schemas.chat import InitialContext, PlannerPlan
from app.schemas.oj import UserContext
from app.services.planner_service import PlannerService
from app.services.prompt_service import PromptService
from app.tools.executor import ToolExecutor
from app.workflows.chat_workflow import _fallback_tool_calls


class EchoArgs(BaseModel):
    value: str


async def echo_tool(value: str) -> ExecutedToolResult:
    return ExecutedToolResult(
        record={
            "name": "echo",
            "arguments": {"value": value},
            "status": "ok",
            "summary": f"echo {value}",
        },
        content=value,
    )


def test_agent_chat_request_allows_new_conversation_without_problem_or_submission():
    request = AgentChatRequest(
        user=UserContext(user_id=1001),
        message="哈希表为什么平均是 O(1)？",
    )

    assert request.conversation.conversation_id is None
    assert request.initial_context.problem_id is None
    assert request.initial_context.submission_id is None


def test_prompt_service_builds_langchain_backed_planner_messages():
    request = AgentChatRequest(
        user=UserContext(user_id=1001),
        message="1007 这道题怎么思考？",
    )

    messages = PromptService().build_planner_messages(
        request,
        ["- get_problem: 查询公开题面"],
    )

    assert messages[0]["role"] == "system"
    assert messages[1]["role"] == "user"
    assert "get_problem" in messages[1]["content"]
    assert "1007 这道题怎么思考" in messages[1]["content"]


@pytest.mark.asyncio
async def test_tool_executor_skips_unknown_tool_and_removes_user_id():
    executor = ToolExecutor([])

    results = await executor.execute(
        [
            PlannerToolCall(
                name="get_submission",
                arguments={"submission_id": "sub_1", "user_id": 999},
            )
        ]
    )

    assert results[0].record.status == "skipped"
    assert "user_id" not in results[0].record.arguments


@pytest.mark.asyncio
async def test_tool_executor_reports_missing_required_arguments_cleanly():
    tool = StructuredTool.from_function(
        coroutine=echo_tool,
        name="echo",
        description="echo",
        args_schema=EchoArgs,
    )
    executor = ToolExecutor([tool])

    results = await executor.execute([PlannerToolCall(name="echo", arguments={})])

    assert results[0].record.status == "error"
    assert "缺少必要参数" in results[0].record.summary
    assert "value" in results[0].record.summary


@pytest.mark.asyncio
async def test_tool_executor_deduplicates_calls():
    tool = StructuredTool.from_function(
        coroutine=echo_tool,
        name="echo",
        description="echo",
        args_schema=EchoArgs,
    )
    executor = ToolExecutor([tool])

    results = await executor.execute(
        [
            PlannerToolCall(name="echo", arguments={"value": "x"}),
            PlannerToolCall(name="echo", arguments={"value": "x"}),
        ]
    )

    assert results[0].record.status == "ok"
    assert results[1].record.status == "skipped"


def test_planner_normalizes_common_tool_argument_aliases():
    request = AgentChatRequest(
        user=UserContext(user_id=1001),
        initial_context=InitialContext(problem_id=1007, submission_id="sub_ctx"),
        message="看看这份提交",
    )
    plan = PlannerPlan(
        tool_calls=[
            PlannerToolCall(name="get_problem", arguments={"problemId": 1007}),
            PlannerToolCall(name="get_submission", arguments={"submissionId": "sub_1"}),
            PlannerToolCall(name="retrieve_knowledge", arguments={"query": "栈", "problemID": 1007}),
        ]
    )

    normalized = PlannerService()._normalize_plan(request, plan, [])

    assert normalized.tool_calls[0].arguments == {"problem_id": 1007}
    assert normalized.tool_calls[1].arguments == {"submission_id": "sub_1"}
    assert normalized.tool_calls[2].arguments == {"query": "栈", "problem_id": 1007}


def test_planner_fills_missing_context_arguments():
    request = AgentChatRequest(
        user=UserContext(user_id=1001),
        initial_context=InitialContext(problem_id=1007, submission_id="sub_ctx"),
        message="这份提交为什么错？",
    )
    plan = PlannerPlan(
        tool_calls=[
            PlannerToolCall(name="get_problem", arguments={}),
            PlannerToolCall(name="get_submission", arguments={}),
        ]
    )

    normalized = PlannerService()._normalize_plan(request, plan, [])

    assert normalized.tool_calls[0].arguments == {"problem_id": 1007}
    assert normalized.tool_calls[1].arguments == {"submission_id": "sub_ctx"}


def test_planner_extracts_missing_problem_id_from_user_message():
    request = AgentChatRequest(
        user=UserContext(user_id=1001),
        message="帮我看看1001这道题的核心思路",
    )
    plan = PlannerPlan(
        tool_calls=[
            PlannerToolCall(name="get_problem", arguments={}),
            PlannerToolCall(name="retrieve_knowledge", arguments={"query": "图论"}),
        ]
    )

    normalized = PlannerService()._normalize_plan(request, plan, [])

    assert normalized.tool_calls[0].arguments == {"problem_id": 1001}
    assert normalized.tool_calls[1].arguments == {"query": "图论", "problem_id": 1001}


def test_planner_extracts_missing_submission_id_from_user_message():
    request = AgentChatRequest(
        user=UserContext(user_id=1001),
        message="sub_abc123 这份提交为什么 WA？",
    )
    plan = PlannerPlan(
        tool_calls=[
            PlannerToolCall(name="get_submission", arguments={}),
        ]
    )

    normalized = PlannerService()._normalize_plan(request, plan, [])

    assert normalized.tool_calls[0].arguments == {"submission_id": "sub_abc123"}


def test_chat_workflow_fallback_tool_calls_extract_problem_id():
    request = AgentChatRequest(
        user=UserContext(user_id=1001),
        message="帮我看看1001这道题",
    )

    calls = _fallback_tool_calls(request)

    assert calls[0].name == "get_problem"
    assert calls[0].arguments == {"problem_id": 1001}
