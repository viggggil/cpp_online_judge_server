from collections.abc import Mapping
from typing import Any

from langchain_core.tools import BaseTool
from pydantic import ValidationError

from app.schemas.chat import ExecutedToolResult, PlannerToolCall, ToolCallRecord


class ToolExecutor:
    def __init__(self, tools: list[BaseTool], *, max_calls: int = 6) -> None:
        self.tools: Mapping[str, BaseTool] = {tool.name: tool for tool in tools}
        self.max_calls = max_calls

    async def execute(self, tool_calls: list[PlannerToolCall]) -> list[ExecutedToolResult]:
        results: list[ExecutedToolResult] = []
        seen: set[tuple[str, str]] = set()

        for call in tool_calls[: self.max_calls]:
            arguments = _sanitize_arguments(call.arguments)
            dedupe_key = (call.name, repr(sorted(arguments.items())))
            if dedupe_key in seen:
                results.append(
                    ExecutedToolResult(
                        record=ToolCallRecord(
                            name=call.name,
                            arguments=arguments,
                            status="skipped",
                            summary="重复工具调用已跳过",
                        )
                    )
                )
                continue
            seen.add(dedupe_key)

            tool = self.tools.get(call.name)
            if tool is None:
                results.append(
                    ExecutedToolResult(
                        record=ToolCallRecord(
                            name=call.name,
                            arguments=arguments,
                            status="skipped",
                            summary="未知工具，已跳过",
                        )
                    )
                )
                continue

            missing = _missing_required_arguments(tool, arguments)
            if missing:
                results.append(
                    _error_result(
                        call.name,
                        arguments,
                        _missing_argument_message(call.name, missing),
                    )
                )
                continue

            try:
                raw_result = await tool.ainvoke(arguments)
            except (TypeError, ValueError, ValidationError) as exc:
                results.append(_error_result(call.name, arguments, f"参数无效: {exc}"))
                continue
            except Exception as exc:
                results.append(_error_result(call.name, arguments, str(exc)))
                continue

            if isinstance(raw_result, ExecutedToolResult):
                results.append(raw_result)
            else:
                results.append(
                    ExecutedToolResult(
                        record=ToolCallRecord(
                            name=call.name,
                            arguments=arguments,
                            status="ok",
                            summary="工具执行完成",
                        ),
                        content=str(raw_result),
                    )
                )

        return results


def _sanitize_arguments(arguments: dict[str, Any]) -> dict[str, Any]:
    sanitized = dict(arguments)
    sanitized.pop("user_id", None)
    sanitized.pop("owner_user_id", None)
    return sanitized


def _missing_required_arguments(tool: BaseTool, arguments: dict[str, Any]) -> list[str]:
    schema = getattr(tool, "args_schema", None)
    if schema is None:
        return []
    try:
        required = schema.model_json_schema().get("required") or []
    except Exception:
        return []
    return [
        name
        for name in required
        if name not in arguments or arguments[name] is None or arguments[name] == ""
    ]


def _missing_argument_message(tool_name: str, missing: list[str]) -> str:
    if tool_name in {"get_problem", "list_problem_submissions"} and "problem_id" in missing:
        return "缺少 problem_id，无法查询题目信息；请在问题中提供题号，或先选择题目/提交作为上下文。"
    if tool_name == "get_submission" and "submission_id" in missing:
        return "缺少 submission_id，无法查询提交记录；请提供提交编号，或先选择一次提交作为上下文。"
    if tool_name == "get_conversation_history" and "conversation_id" in missing:
        return "缺少 conversation_id，无法查询历史会话。"
    return "缺少必要参数：" + ", ".join(missing)


def _error_result(
    name: str,
    arguments: dict[str, Any],
    summary: str,
) -> ExecutedToolResult:
    return ExecutedToolResult(
        record=ToolCallRecord(
            name=name,
            arguments=arguments,
            status="error",
            summary=summary[:500],
        ),
        content=f"工具 {name} 执行失败：{summary[:1000]}",
    )
