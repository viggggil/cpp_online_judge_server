from langchain_core.tools import StructuredTool
from pydantic import BaseModel, Field

from app.rag.retriever import get_knowledge_retriever
from app.schemas.chat import ExecutedToolResult, ToolCallRecord
from app.schemas.rag import SourceReference


class RetrieveKnowledgeArgs(BaseModel):
    query: str = Field(min_length=1, max_length=1000)
    problem_id: int | None = None


def build_retrieve_knowledge_tool() -> StructuredTool:
    return StructuredTool.from_function(
        coroutine=_retrieve_knowledge,
        name="retrieve_knowledge",
        description="检索本地 Markdown 知识库，适合算法原理、复杂度、常见 C++ 错误和题目提示。",
        args_schema=RetrieveKnowledgeArgs,
    )


async def _retrieve_knowledge(
    query: str,
    problem_id: int | None = None,
) -> ExecutedToolResult:
    documents = get_knowledge_retriever().retrieve_query(
        query,
        problem_id=problem_id,
    )
    sources = [
        SourceReference.model_validate(document.source.model_dump())
        for document in documents
    ]
    content = "\n\n---\n\n".join(
        "\n".join(
            [
                f"[{index}] {document.source.title or document.source.document_id}",
                f"source: {document.source.source}",
                f"score: {document.source.score}",
                document.content[:1800],
            ]
        )
        for index, document in enumerate(documents, start=1)
    )
    return ExecutedToolResult(
        record=ToolCallRecord(
            name="retrieve_knowledge",
            arguments={"query": query, "problem_id": problem_id},
            status="ok",
            summary=f"命中 {len(documents)} 条知识库资料",
        ),
        content=content or "知识库未返回匹配资料。",
        sources=sources,
    )
