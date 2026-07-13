import os
from pathlib import Path

from dotenv import load_dotenv
from fastapi import APIRouter
from pydantic import BaseModel


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


def _has_env(name: str) -> bool:
    return bool(os.getenv(name, "").strip())


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
