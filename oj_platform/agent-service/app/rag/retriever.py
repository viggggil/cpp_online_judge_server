from __future__ import annotations

import json
import os
from dataclasses import dataclass
from functools import lru_cache
from pathlib import Path
from typing import Any, Callable

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


DEFAULT_GENERAL_MIN_SCORE = 0.56
DEFAULT_GENERAL_MAX_DOCUMENTS = 2


@dataclass(frozen=True)
class RetrievedDocument:
    content: str
    source: SourceReference
    distance: float


def _metadata_problem_id(metadata: dict[str, Any]) -> int | None:
    value = metadata.get("problem_id")
    if value is None or value == "":
        return None
    try:
        return int(value)
    except (TypeError, ValueError):
        return None


def _metadata_tags(metadata: dict[str, Any]) -> set[str]:
    value = metadata.get("tags")
    if value is None:
        return set()

    if isinstance(value, str):
        try:
            parsed = json.loads(value)
        except json.JSONDecodeError:
            parsed = value.replace(",", " ").split()
    elif isinstance(value, list):
        parsed = value
    else:
        parsed = []

    return {
        str(item).strip().lower()
        for item in parsed
        if str(item).strip()
    }


def _problem_tags(request: DiagnosisRequest) -> set[str]:
    return {
        str(tag).strip().lower()
        for tag in request.problem.tags
        if str(tag).strip()
    }


def _normalized_tags(tags: list[str] | set[str] | tuple[str, ...]) -> set[str]:
    return {
        str(tag).strip().lower()
        for tag in tags
        if str(tag).strip()
    }


def _is_problem_editorial(metadata: dict[str, Any]) -> bool:
    category = str(metadata.get("category") or "").strip().lower()
    safe_level = str(metadata.get("safe_level") or "").strip().lower()
    source = str(metadata.get("source") or "").strip().lower()
    return (
        category == "problem_editorial"
        or safe_level == "editorial"
        or "problem_hints/" in source
    )


def _is_same_problem_editorial(
    metadata: dict[str, Any],
    request: DiagnosisRequest,
) -> bool:
    return (
        _is_problem_editorial(metadata)
        and _metadata_problem_id(metadata) == request.problem.problem_id
    )


def _is_relevant_general_document(
    metadata: dict[str, Any],
    request: DiagnosisRequest,
) -> bool:
    if _is_problem_editorial(metadata):
        return False

    tags = _problem_tags(request)
    if not tags:
        return False

    metadata_tags = _metadata_tags(metadata)
    if tags.intersection(metadata_tags):
        return True

    knowledge_point = str(
        metadata.get("knowledge_point")
        or metadata.get("category")
        or metadata.get("title")
        or ""
    ).strip().lower()
    return any(tag and tag in knowledge_point for tag in tags)


def _is_relevant_general_document_for_tags(
    metadata: dict[str, Any],
    tags: set[str],
) -> bool:
    if _is_problem_editorial(metadata):
        return False
    if not tags:
        return True

    metadata_tags = _metadata_tags(metadata)
    if tags.intersection(metadata_tags):
        return True

    knowledge_point = str(
        metadata.get("knowledge_point")
        or metadata.get("category")
        or metadata.get("title")
        or ""
    ).strip().lower()
    return any(tag and tag in knowledge_point for tag in tags)


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
        self.general_min_score = float(
            os.getenv("RAG_GENERAL_MIN_SCORE", str(DEFAULT_GENERAL_MIN_SCORE))
        )
        self.general_max_documents = int(
            os.getenv("RAG_GENERAL_MAX_DOCUMENTS", str(DEFAULT_GENERAL_MAX_DOCUMENTS))
        )

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

        def add_results(
            result: dict,
            *,
            accept: Callable[[dict[str, Any]], bool],
            min_score: float | None = None,
            max_documents: int | None = None,
        ) -> None:
            ids = result.get("ids", [[]])[0]
            documents = result.get("documents", [[]])[0]
            metadatas = result.get("metadatas", [[]])[0]
            distances = result.get("distances", [[]])[0]
            accepted_count = 0
            for chunk_id, document, metadata, distance in zip(
                ids,
                documents,
                metadatas,
                distances,
                strict=False,
            ):
                metadata = metadata or {}
                if not accept(metadata):
                    continue
                if chunk_id in seen_ids:
                    continue

                score = 1.0 / (1.0 + float(distance))
                if min_score is not None and score < min_score:
                    continue
                if max_documents is not None and accepted_count >= max_documents:
                    break

                seen_ids.add(chunk_id)
                accepted_count += 1
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
        add_results(
            problem_result,
            accept=lambda metadata: _is_same_problem_editorial(metadata, request),
        )

        general_result = collection.query(
            query_embeddings=[query_embedding],
            n_results=max(self.top_k * 5, 10),
            include=["documents", "metadatas", "distances"],
        )
        add_results(
            general_result,
            accept=lambda metadata: _is_relevant_general_document(metadata, request),
            min_score=self.general_min_score,
            max_documents=self.general_max_documents,
        )

        retrieved.sort(key=lambda item: item.distance)
        return retrieved[: self.top_k]

    def retrieve_query(
        self,
        query: str,
        *,
        problem_id: int | None = None,
        tags: list[str] | None = None,
    ) -> list[RetrievedDocument]:
        if not self.persist_dir.exists() or not query.strip():
            return []

        client = chromadb.PersistentClient(
            path=str(self.persist_dir),
            settings=ChromaSettings(anonymized_telemetry=False),
        )
        collection = client.get_collection(self.collection_name)
        if collection.count() <= 0:
            return []

        embedding_model = build_embedding_model(self.embedding_model_name, self.cache_dir)
        query_embedding = next(embedding_model.embed([query])).tolist()

        retrieved: list[RetrievedDocument] = []
        seen_ids: set[str] = set()
        normalized_tags = _normalized_tags(tags or [])

        def add_results(
            result: dict,
            *,
            accept: Callable[[dict[str, Any]], bool],
            min_score: float | None = None,
            max_documents: int | None = None,
        ) -> None:
            ids = result.get("ids", [[]])[0]
            documents = result.get("documents", [[]])[0]
            metadatas = result.get("metadatas", [[]])[0]
            distances = result.get("distances", [[]])[0]
            accepted_count = 0
            for chunk_id, document, metadata, distance in zip(
                ids,
                documents,
                metadatas,
                distances,
                strict=False,
            ):
                metadata = metadata or {}
                if not accept(metadata):
                    continue
                if chunk_id in seen_ids:
                    continue

                score = 1.0 / (1.0 + float(distance))
                if min_score is not None and score < min_score:
                    continue
                if max_documents is not None and accepted_count >= max_documents:
                    break

                seen_ids.add(chunk_id)
                accepted_count += 1
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

        if problem_id is not None:
            problem_result = collection.query(
                query_embeddings=[query_embedding],
                n_results=min(2, self.top_k),
                where={"problem_id": problem_id},
                include=["documents", "metadatas", "distances"],
            )
            add_results(
                problem_result,
                accept=lambda metadata: (
                    _is_problem_editorial(metadata)
                    and _metadata_problem_id(metadata) == problem_id
                ),
            )

        general_result = collection.query(
            query_embeddings=[query_embedding],
            n_results=max(self.top_k * 5, 10),
            include=["documents", "metadatas", "distances"],
        )
        add_results(
            general_result,
            accept=lambda metadata: _is_relevant_general_document_for_tags(
                metadata,
                normalized_tags,
            ),
            min_score=self.general_min_score,
            max_documents=self.general_max_documents,
        )

        retrieved.sort(key=lambda item: item.distance)
        return retrieved[: self.top_k]


@lru_cache(maxsize=1)
def get_knowledge_retriever() -> KnowledgeRetriever:
    return KnowledgeRetriever.from_env()
