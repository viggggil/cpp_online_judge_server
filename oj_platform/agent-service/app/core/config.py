import os
from pathlib import Path

from dotenv import load_dotenv
from pydantic import BaseModel


PROJECT_ROOT = Path(__file__).resolve().parents[2]
load_dotenv(PROJECT_ROOT / ".env")


class Settings(BaseModel):
    internal_api_token: str = ""
    oj_server_base_url: str = "http://127.0.0.1:8080"
    oj_internal_api_token: str = ""
    oj_connect_timeout_seconds: float = 5
    oj_read_timeout_seconds: float = 15
    openrouter_api_key: str = ""
    chat_model: str = "deepseek/deepseek-v4-flash"
    planner_model: str = "deepseek/deepseek-v4-flash"
    openrouter_base_url: str = "https://openrouter.ai/api/v1"
    openrouter_read_timeout_seconds: float = 60
    openrouter_provider_sort: str = "throughput"
    planner_provider_sort: str = "latency"
    planner_timeout_seconds: float = 28
    answer_stream_idle_timeout_seconds: float = 35
    answer_stream_total_timeout_seconds: float = 150
    answer_fallback_timeout_seconds: float = 45
    judge_worker_url: str = "http://127.0.0.1:18081/api/judge"
    judge_connect_timeout_seconds: float = 5
    judge_read_timeout_seconds: float = 30
    codegen_model: str = ""
    codegen_provider_sort: str = "latency"
    codegen_max_source_chars: int = 30000
    codegen_max_test_cases: int = 6
    codegen_max_testcase_chars: int = 20000


def get_settings() -> Settings:
    return Settings(
        internal_api_token=os.getenv("INTERNAL_API_TOKEN", ""),
        oj_server_base_url=os.getenv("OJ_SERVER_BASE_URL", "http://127.0.0.1:8080"),
        oj_internal_api_token=os.getenv(
            "OJ_INTERNAL_API_TOKEN",
            os.getenv("INTERNAL_API_TOKEN", ""),
        ),
        oj_connect_timeout_seconds=float(os.getenv("OJ_CONNECT_TIMEOUT_SECONDS", "5")),
        oj_read_timeout_seconds=float(os.getenv("OJ_READ_TIMEOUT_SECONDS", "15")),
        openrouter_api_key=os.getenv("OPENROUTER_API_KEY", ""),
        chat_model=os.getenv("CHAT_MODEL", "deepseek/deepseek-v4-flash"),
        planner_model=os.getenv("PLANNER_MODEL", "deepseek/deepseek-v4-flash"),
        openrouter_base_url=os.getenv(
            "OPENROUTER_BASE_URL",
            "https://openrouter.ai/api/v1",
        ),
        openrouter_read_timeout_seconds=float(
            os.getenv("OPENROUTER_READ_TIMEOUT_SECONDS", "60")
        ),
        openrouter_provider_sort=os.getenv(
            "OPENROUTER_PROVIDER_SORT",
            "throughput",
        ),
        planner_provider_sort=os.getenv("PLANNER_PROVIDER_SORT", "latency"),
        planner_timeout_seconds=float(os.getenv("PLANNER_TIMEOUT_SECONDS", "28")),
        answer_stream_idle_timeout_seconds=float(
            os.getenv("ANSWER_STREAM_IDLE_TIMEOUT_SECONDS", "35")
        ),
        answer_stream_total_timeout_seconds=float(
            os.getenv("ANSWER_STREAM_TOTAL_TIMEOUT_SECONDS", "150")
        ),
        answer_fallback_timeout_seconds=float(
            os.getenv("ANSWER_FALLBACK_TIMEOUT_SECONDS", "45")
        ),
        judge_worker_url=os.getenv(
            "JUDGE_WORKER_URL",
            "http://127.0.0.1:18081/api/judge",
        ),
        judge_connect_timeout_seconds=float(
            os.getenv("JUDGE_CONNECT_TIMEOUT_SECONDS", "5")
        ),
        judge_read_timeout_seconds=float(os.getenv("JUDGE_READ_TIMEOUT_SECONDS", "30")),
        codegen_model=os.getenv("CODEGEN_MODEL", os.getenv("CHAT_MODEL", "")),
        codegen_provider_sort=os.getenv("CODEGEN_PROVIDER_SORT", "latency"),
        codegen_max_source_chars=int(os.getenv("CODEGEN_MAX_SOURCE_CHARS", "30000")),
        codegen_max_test_cases=int(os.getenv("CODEGEN_MAX_TEST_CASES", "6")),
        codegen_max_testcase_chars=int(
            os.getenv("CODEGEN_MAX_TESTCASE_CHARS", "20000")
        ),
    )
