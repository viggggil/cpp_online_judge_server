from __future__ import annotations

import os
from dataclasses import dataclass
from functools import lru_cache
from pathlib import Path

import chromadb
from chromadb.config import Settings as ChromaSettings

from app.rag.ingest import (
    DEFAULT_CHROMA_DIR,
    DEFAULT_COLLECTION_NAME,
    DEFAULT_EMBEDDING_CACHE_DIR,
    DEFAULT_EMBEDDING_MODEL,
    build_embedding_model,
    resolve_project_path,
)
from app.schemas.diagnosis import DiagnosisRequest, SourceReference


@dataclass(frozen=True)
class RetrievedDocument:
    content: str
    source: SourceReference
    distance: float


def build_retrieval_query(request: DiagnosisRequest) -> str:
    problem = request.problem
    submission = request.submission
    parts = [
        f"题目 {problem.problem_id} {problem.title}",
        f"标签 {' '.join(problem.tags)}",
        f"判题状态 {submission.judge_status}",
        request.question or "请诊断这次提交。",
    ]

    if submission.compiler_output:
        parts.append(f"编译输出 {submission.compiler_output[:1200]}")
    if submission.runtime_stderr:
        parts.append(f"运行错误 {submission.runtime_stderr[:1200]}")
    if problem.description_markdown:
        parts.append(problem.description_markdown[:1800])

    return "\n".join(part for part in parts if part.strip())


class KnowledgeRetriever:
    def __init__(
        self,
        *,
        persist_dir: Path,
        cache_dir: Path,
        collection_name: str,
        embedding_model_name: str,
        top_k: int,
    ) -> None:
        self.persist_dir = persist_dir
        self.cache_dir = cache_dir
        self.collection_name = collection_name
        self.embedding_model_name = embedding_model_name
        self.top_k = top_k

    @classmethod
    def from_env(cls) -> KnowledgeRetriever:
        return cls(
            persist_dir=resolve_project_path(
                os.getenv("CHROMA_PERSIST_DIR"),
                DEFAULT_CHROMA_DIR,
            ),
            cache_dir=resolve_project_path(
                os.getenv("EMBEDDING_CACHE_DIR"),
                DEFAULT_EMBEDDING_CACHE_DIR,
            ),
            collection_name=os.getenv("CHROMA_COLLECTION", DEFAULT_COLLECTION_NAME),
            embedding_model_name=os.getenv("EMBEDDING_MODEL", DEFAULT_EMBEDDING_MODEL),
            top_k=int(os.getenv("RAG_TOP_K", "5")),
        )

    def retrieve(self, request: DiagnosisRequest) -> list[RetrievedDocument]:
        if not self.persist_dir.exists():
            return []

        client = chromadb.PersistentClient(
            path=str(self.persist_dir),
            settings=ChromaSettings(anonymized_telemetry=False),
        )
        collection = client.get_collection(self.collection_name)
        if collection.count() <= 0:
            return []

        query = build_retrieval_query(request)
        embedding_model = build_embedding_model(self.embedding_model_name, self.cache_dir)
        query_embedding = next(embedding_model.embed([query])).tolist()

        retrieved: list[RetrievedDocument] = []
        seen_ids: set[str] = set()

        def add_results(result: dict) -> None:
            ids = result.get("ids", [[]])[0]
            documents = result.get("documents", [[]])[0]
            metadatas = result.get("metadatas", [[]])[0]
            distances = result.get("distances", [[]])[0]
            for chunk_id, document, metadata, distance in zip(
                ids,
                documents,
                metadatas,
                distances,
                strict=False,
            ):
                if chunk_id in seen_ids:
                    continue
                seen_ids.add(chunk_id)

                score = 1.0 / (1.0 + float(distance))
                source = SourceReference(
                    document_id=str(metadata.get("document_id", "")),
                    source=str(metadata.get("source", "")),
                    title=str(metadata.get("title", "")),
                    knowledge_point=str(
                        metadata.get("knowledge_point")
                        or metadata.get("category")
                        or ""
                    ),
                    chunk_index=int(metadata.get("chunk_index", 0)),
                    score=round(score, 6),
                )
                retrieved.append(
                    RetrievedDocument(
                        content=str(document),
                        source=source,
                        distance=float(distance),
                    )
                )

        problem_result = collection.query(
            query_embeddings=[query_embedding],
            n_results=min(2, self.top_k),
            where={"problem_id": request.problem.problem_id},
            include=["documents", "metadatas", "distances"],
        )
        add_results(problem_result)

        general_result = collection.query(
            query_embeddings=[query_embedding],
            n_results=max(self.top_k, 1),
            include=["documents", "metadatas", "distances"],
        )
        add_results(general_result)

        retrieved.sort(key=lambda item: item.distance)
        return retrieved[: self.top_k]


@lru_cache(maxsize=1)
def get_knowledge_retriever() -> KnowledgeRetriever:
    return KnowledgeRetriever.from_env()
