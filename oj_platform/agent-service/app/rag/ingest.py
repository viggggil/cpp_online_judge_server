from __future__ import annotations

import hashlib
import json
import os
import re
from dataclasses import dataclass
from pathlib import Path
from typing import Any

import chromadb
import yaml
from chromadb.config import Settings as ChromaSettings
from fastembed.common.model_management import ModelManagement
from fastembed import TextEmbedding
from langchain_text_splitters import RecursiveCharacterTextSplitter


PROJECT_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_KNOWLEDGE_DIR = PROJECT_ROOT / "knowledge"
DEFAULT_CHROMA_DIR = PROJECT_ROOT / "data" / "chroma"
DEFAULT_EMBEDDING_CACHE_DIR = PROJECT_ROOT / "data" / "fastembed"
DEFAULT_COLLECTION_NAME = "oj_agent_knowledge"
DEFAULT_EMBEDDING_MODEL = "BAAI/bge-small-zh-v1.5"

FRONTMATTER_PATTERN = re.compile(r"\A---\s*\n(.*?)\n---\s*\n?", re.DOTALL)


@dataclass(frozen=True)
class KnowledgeChunk:
    chunk_id: str
    text: str
    metadata: dict[str, str | int | float | bool]


@dataclass(frozen=True)
class IngestResult:
    collection_name: str
    embedding_model: str
    documents: int
    chunks: int
    persist_dir: Path
    cache_dir: Path


def resolve_project_path(value: str | os.PathLike[str] | None, default: Path) -> Path:
    if value is None or str(value).strip() == "":
        path = default
    else:
        path = Path(value)
    if not path.is_absolute():
        path = PROJECT_ROOT / path
    return path.resolve()


def parse_markdown(path: Path) -> tuple[dict[str, Any], str]:
    raw = path.read_text(encoding="utf-8")
    match = FRONTMATTER_PATTERN.match(raw)
    if not match:
        return {}, raw.strip()

    metadata = yaml.safe_load(match.group(1)) or {}
    content = raw[match.end() :].strip()
    if not isinstance(metadata, dict):
        metadata = {}
    return metadata, content


def normalize_metadata(
    metadata: dict[str, Any],
    *,
    source: str,
    chunk_index: int,
) -> dict[str, str | int | float | bool]:
    normalized: dict[str, str | int | float | bool] = {
        "source": source,
        "chunk_index": chunk_index,
    }

    for key, value in metadata.items():
        if value is None:
            continue
        if isinstance(value, bool | int | float | str):
            normalized[key] = value
            continue
        if isinstance(value, list | dict):
            normalized[key] = json.dumps(value, ensure_ascii=False)
            continue
        normalized[key] = str(value)

    return normalized


def build_chunk_id(source: str, chunk_index: int, text: str) -> str:
    digest = hashlib.sha1(f"{source}:{chunk_index}:{text}".encode("utf-8")).hexdigest()
    return f"{source.replace('/', '__')}__{chunk_index}__{digest[:12]}"


def load_knowledge_chunks(
    knowledge_dir: Path,
    *,
    chunk_size: int = 900,
    chunk_overlap: int = 150,
) -> list[KnowledgeChunk]:
    splitter = RecursiveCharacterTextSplitter(
        chunk_size=chunk_size,
        chunk_overlap=chunk_overlap,
        separators=["\n## ", "\n### ", "\n\n", "\n", "。", "，", " ", ""],
    )

    chunks: list[KnowledgeChunk] = []
    for path in sorted(knowledge_dir.rglob("*.md")):
        if path.name.upper() == "README.MD":
            continue

        metadata, content = parse_markdown(path)
        if not content:
            continue

        source = path.relative_to(PROJECT_ROOT).as_posix()
        document_id = str(metadata.get("document_id") or path.stem)
        title = str(metadata.get("title") or path.stem)
        document_text = (
            content if content.lstrip().startswith("#") else f"# {title}\n\n{content}"
        ).strip()

        for index, text in enumerate(splitter.split_text(document_text)):
            chunk_metadata = normalize_metadata(
                {
                    **metadata,
                    "document_id": document_id,
                    "title": title,
                },
                source=source,
                chunk_index=index,
            )
            chunks.append(
                KnowledgeChunk(
                    chunk_id=build_chunk_id(source, index, text),
                    text=text,
                    metadata=chunk_metadata,
                )
            )

    return chunks


def build_embedding_model(model_name: str, cache_dir: Path) -> TextEmbedding:
    cache_dir.mkdir(parents=True, exist_ok=True)

    for model in TextEmbedding.list_supported_models():
        if model.get("model") != model_name:
            continue

        sources = model.get("sources", {})
        source_url = sources.get("url")
        if source_url:
            ModelManagement.retrieve_model_gcs(
                model_name=model_name,
                source_url=source_url,
                cache_dir=str(cache_dir),
                deprecated_tar_struct=bool(sources.get("_deprecated_tar_struct")),
            )
            return TextEmbedding(
                model_name=model_name,
                cache_dir=str(cache_dir),
                local_files_only=True,
            )

    return TextEmbedding(model_name=model_name, cache_dir=str(cache_dir))


def rebuild_chroma_collection(
    *,
    knowledge_dir: Path,
    persist_dir: Path,
    cache_dir: Path,
    collection_name: str,
    embedding_model_name: str,
) -> IngestResult:
    chunks = load_knowledge_chunks(knowledge_dir)
    if not chunks:
        raise RuntimeError(f"no markdown knowledge chunks found in {knowledge_dir}")

    persist_dir.mkdir(parents=True, exist_ok=True)
    client = chromadb.PersistentClient(
        path=str(persist_dir),
        settings=ChromaSettings(anonymized_telemetry=False),
    )

    try:
        client.delete_collection(collection_name)
    except Exception:
        pass

    collection = client.create_collection(
        name=collection_name,
        metadata={
            "embedding_model": embedding_model_name,
            "source": "agent-service/knowledge",
        },
    )

    embedding_model = build_embedding_model(embedding_model_name, cache_dir)
    texts = [chunk.text for chunk in chunks]
    embeddings = [embedding.tolist() for embedding in embedding_model.embed(texts)]

    collection.add(
        ids=[chunk.chunk_id for chunk in chunks],
        documents=texts,
        metadatas=[chunk.metadata for chunk in chunks],
        embeddings=embeddings,
    )

    document_sources = {chunk.metadata["source"] for chunk in chunks}
    return IngestResult(
        collection_name=collection_name,
        embedding_model=embedding_model_name,
        documents=len(document_sources),
        chunks=len(chunks),
        persist_dir=persist_dir,
        cache_dir=cache_dir,
    )


def ingest_from_env() -> IngestResult:
    knowledge_dir = resolve_project_path(
        os.getenv("KNOWLEDGE_DIR"),
        DEFAULT_KNOWLEDGE_DIR,
    )
    persist_dir = resolve_project_path(
        os.getenv("CHROMA_PERSIST_DIR"),
        DEFAULT_CHROMA_DIR,
    )
    cache_dir = resolve_project_path(
        os.getenv("EMBEDDING_CACHE_DIR"),
        DEFAULT_EMBEDDING_CACHE_DIR,
    )
    collection_name = os.getenv("CHROMA_COLLECTION", DEFAULT_COLLECTION_NAME)
    embedding_model_name = os.getenv("EMBEDDING_MODEL", DEFAULT_EMBEDDING_MODEL)

    return rebuild_chroma_collection(
        knowledge_dir=knowledge_dir,
        persist_dir=persist_dir,
        cache_dir=cache_dir,
        collection_name=collection_name,
        embedding_model_name=embedding_model_name,
    )
