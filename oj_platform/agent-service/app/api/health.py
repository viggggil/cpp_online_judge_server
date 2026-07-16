import os
from pathlib import Path

import chromadb
from chromadb.config import Settings as ChromaSettings
from dotenv import load_dotenv
from fastapi import APIRouter
from pydantic import BaseModel

from app.rag.ingest import DEFAULT_EMBEDDING_CACHE_DIR, DEFAULT_EMBEDDING_MODEL


PROJECT_ROOT = Path(__file__).resolve().parents[2]

load_dotenv(PROJECT_ROOT / ".env")

router = APIRouter(tags=["health"])


class HealthResponse(BaseModel):
    status: str
    service: str
    environment: str
    openrouter_configured: bool
    chat_model_configured: bool
    oj_server_configured: bool


class ReadyResponse(BaseModel):
    status: str
    checks: dict[str, str]


def _has_env(name: str) -> bool:
    return bool(os.getenv(name, "").strip())


def _check_config() -> str:
    required = [
        "OPENROUTER_API_KEY",
        "CHAT_MODEL",
        "OJ_SERVER_BASE_URL",
    ]
    return "ok" if all(_has_env(name) for name in required) else "missing"


def _check_chroma() -> str:
    persist_dir = Path(os.getenv("CHROMA_PERSIST_DIR", PROJECT_ROOT / "data" / "chroma"))
    if not persist_dir.is_absolute():
        persist_dir = PROJECT_ROOT / persist_dir
    if not persist_dir.exists():
        return "not_initialized"

    try:
        client = chromadb.PersistentClient(
            path=str(persist_dir),
            settings=ChromaSettings(anonymized_telemetry=False),
        )
        collection = client.get_collection(
            os.getenv("CHROMA_COLLECTION", "oj_agent_knowledge")
        )
        return "ok" if collection.count() > 0 else "empty"
    except Exception:
        return "not_initialized"


def _check_embedding() -> str:
    model = os.getenv("EMBEDDING_MODEL", DEFAULT_EMBEDDING_MODEL).strip()
    cache_dir = Path(os.getenv("EMBEDDING_CACHE_DIR", DEFAULT_EMBEDDING_CACHE_DIR))
    if not cache_dir.is_absolute():
        cache_dir = PROJECT_ROOT / cache_dir
    return "configured" if model and cache_dir.exists() else "missing"


def _check_oj_client() -> str:
    return "configured" if _has_env("OJ_SERVER_BASE_URL") else "missing"


def _check_llm_client() -> str:
    return (
        "configured"
        if _has_env("OPENROUTER_API_KEY") and _has_env("CHAT_MODEL")
        else "missing"
    )


@router.get("/health", response_model=HealthResponse)
async def health_check() -> HealthResponse:
    openrouter_configured = _has_env("OPENROUTER_API_KEY")
    chat_model_configured = _has_env("CHAT_MODEL")
    oj_server_configured = _has_env("OJ_SERVER_BASE_URL")

    required_config_present = (
        openrouter_configured
        and chat_model_configured
        and oj_server_configured
    )

    return HealthResponse(
        status="ok" if required_config_present else "degraded",
        service="oj-programming-tutor",
        environment=os.getenv("APP_ENV", "development"),
        openrouter_configured=openrouter_configured,
        chat_model_configured=chat_model_configured,
        oj_server_configured=oj_server_configured,
    )


@router.get("/ready", response_model=ReadyResponse)
async def readiness_check() -> ReadyResponse:
    checks = {
        "config": _check_config(),
        "vector_store": _check_chroma(),
        "embedding": _check_embedding(),
        "oj_client": _check_oj_client(),
        "llm_client": _check_llm_client(),
    }

    ready_values = {"ok", "configured"}
    status = (
        "ready"
        if all(value in ready_values for value in checks.values())
        else "degraded"
    )

    return ReadyResponse(status=status, checks=checks)
