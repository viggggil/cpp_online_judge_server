import os
from pathlib import Path

from dotenv import load_dotenv
from pydantic import BaseModel


PROJECT_ROOT = Path(__file__).resolve().parents[2]
load_dotenv(PROJECT_ROOT / ".env")


class Settings(BaseModel):
    internal_api_token: str = ""
    openrouter_api_key: str = ""
    chat_model: str = "deepseek/deepseek-v4-flash"
    openrouter_base_url: str = "https://openrouter.ai/api/v1"
    openrouter_read_timeout_seconds: float = 60


def get_settings() -> Settings:
    return Settings(
        internal_api_token=os.getenv("INTERNAL_API_TOKEN", ""),
        openrouter_api_key=os.getenv("OPENROUTER_API_KEY", ""),
        chat_model=os.getenv("CHAT_MODEL", "deepseek/deepseek-v4-flash"),
        openrouter_base_url=os.getenv(
            "OPENROUTER_BASE_URL",
            "https://openrouter.ai/api/v1",
        ),
        openrouter_read_timeout_seconds=float(
            os.getenv("OPENROUTER_READ_TIMEOUT_SECONDS", "60")
        ),
    )
