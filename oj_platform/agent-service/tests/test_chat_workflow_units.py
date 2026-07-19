import pytest
from langchain_core.tools import StructuredTool
from pydantic import BaseModel

from app.schemas.chat import AgentChatRequest, ExecutedToolResult, PlannerToolCall
from app.schemas.oj import UserContext
from app.services.prompt_service import PromptService
from app.tools.executor import ToolExecutor


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
