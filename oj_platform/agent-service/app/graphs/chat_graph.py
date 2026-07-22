from typing import Literal

from langgraph.graph import END, START, StateGraph

from app.graphs.chat_state import ChatGraphState
from app.graphs.nodes import (
    build_answer_messages_node,
    collect_sources_node,
    execute_tools_node,
    finalize_node,
    generate_answer_node,
    emit_plan_preview_node,
    plan_node,
    prepare_node,
)


def build_chat_graph():
    builder = StateGraph(ChatGraphState)

    builder.add_node("prepare", prepare_node)
    builder.add_node("plan", plan_node)
    builder.add_node("emit_plan_preview", emit_plan_preview_node)
    builder.add_node("execute_tools", execute_tools_node)
    builder.add_node("collect_sources", collect_sources_node)
    builder.add_node("build_answer_messages", build_answer_messages_node)
    builder.add_node("generate_answer", generate_answer_node)
    builder.add_node("finalize", finalize_node)

    builder.add_edge(START, "prepare")
    builder.add_edge("prepare", "plan")
    builder.add_edge("plan", "emit_plan_preview")
    builder.add_conditional_edges(
        "emit_plan_preview",
        _route_after_plan,
        {
            "execute_tools": "execute_tools",
            "collect_sources": "collect_sources",
        },
    )
    builder.add_edge("execute_tools", "collect_sources")
    builder.add_edge("collect_sources", "build_answer_messages")
    builder.add_edge("build_answer_messages", "generate_answer")
    builder.add_edge("generate_answer", "finalize")
    builder.add_edge("finalize", END)

    return builder.compile()


def _route_after_plan(state: ChatGraphState) -> Literal["execute_tools", "collect_sources"]:
    plan = state.get("plan")
    if plan and plan.tool_calls:
        return "execute_tools"
    return "collect_sources"
