import argparse
import json
import os
import sys
import traceback
from pathlib import Path
from typing import Literal, cast

import httpx
from dotenv import load_dotenv
from langchain_openrouter import ChatOpenRouter
from pydantic import BaseModel, ConfigDict, Field


PROJECT_ROOT = Path(__file__).resolve().parents[1]
OPENROUTER_CHAT_URL = "https://openrouter.ai/api/v1/chat/completions"


class SubmissionDiagnosis(BaseModel):
    """Structured diagnosis for an online judge submission."""

    model_config = ConfigDict(extra="forbid")

    error_type: Literal[
        "compile_error",
        "wrong_answer",
        "time_limit_exceeded",
        "runtime_error",
        "accepted",
        "unknown",
    ] = Field(description="The primary type of submission error.")

    summary: str = Field(
        description="A concise Chinese summary of the main problem."
    )

    analysis: str = Field(
        description=(
            "A Chinese explanation grounded in the supplied source code "
            "and judge result."
        )
    )

    evidence: list[str] = Field(
        description="Specific evidence found in the code or compiler output."
    )

    knowledge_points: list[str] = Field(
        description="Relevant C++ or algorithm knowledge points."
    )

    hints: list[str] = Field(
        description="Progressive hints that do not reveal a complete solution.",
        min_length=1,
        max_length=4,
    )

    confidence: float = Field(
        description="Confidence in the diagnosis.",
        ge=0,
        le=1,
    )


def require_env(name: str) -> str:
    value = os.getenv(name, "").strip()
    if not value:
        raise RuntimeError(f"{name} is missing or empty")
    return value


def build_messages(source_code: str, compiler_output: str) -> list[dict[str, str]]:
    system_prompt = """
你是一名在线判题平台的 C++ 编程辅导老师。

规则：
1. 只根据提供的题目、用户代码和判题信息进行分析。
2. 不编造隐藏测试数据。
3. 不提供完整可提交代码。
4. 使用中文解释。
5. 提示必须循序渐进。
6. evidence 中只写能够从输入中直接确认的证据。
7. 只输出一个 JSON 对象，不要输出 Markdown。
""".strip()

    user_prompt = f"""
题目：
输出 Hello。

判题状态：
Compile Error

用户代码：
```cpp
{source_code}
```

编译器输出：
```text
{compiler_output}
```

请诊断本次提交。
""".strip()

    return [
        {"role": "system", "content": system_prompt},
        {"role": "user", "content": user_prompt},
    ]


def sample_submission() -> tuple[str, str]:
    source_code = """
#include <iostream>

int main() {
    std::cout << "Hello" << std::endl
    return 0;
}
""".strip()

    compiler_output = """
main.cpp: In function 'int main()':
main.cpp:5:39: error: expected ';' before 'return'
""".strip()

    return source_code, compiler_output


def check_with_http(api_key: str, model_name: str) -> SubmissionDiagnosis:
    """Call OpenRouter directly and validate the JSON with Pydantic."""

    source_code, compiler_output = sample_submission()

    print("[1/1] Calling OpenRouter directly through HTTP...", flush=True)
    response = httpx.post(
        OPENROUTER_CHAT_URL,
        headers={
            "Authorization": f"Bearer {api_key}",
            "Content-Type": "application/json",
        },
        json={
            "model": model_name,
            "messages": build_messages(source_code, compiler_output),
            "temperature": 0,
            "max_tokens": 1200,
            "response_format": {
                "type": "json_schema",
                "json_schema": {
                    "name": "submission_diagnosis",
                    "strict": True,
                    "schema": SubmissionDiagnosis.model_json_schema(),
                },
            },
        },
        timeout=45,
    )
    response.raise_for_status()

    payload = response.json()
    content = payload["choices"][0]["message"]["content"]
    return SubmissionDiagnosis.model_validate_json(content)


def check_with_langchain(api_key: str, model_name: str) -> SubmissionDiagnosis:
    """Run LangChain structured output for reproducing SDK issues."""

    source_code, compiler_output = sample_submission()

    model = ChatOpenRouter(
        model=model_name,
        api_key=api_key,
        temperature=0,
        max_tokens=1200,
        max_retries=0,
        timeout=20,
        openrouter_provider={
            "require_parameters": True,
            "allow_fallbacks": True,
        },
    )

    print("[1/2] Testing ordinary LangChain model invocation...", flush=True)
    basic_response = model.invoke(
        "请只回复 BASIC_CALL_OK，不要添加其他内容。"
    )
    print(f"Basic response: {basic_response.content}", flush=True)

    print("\n[2/2] Calling LangChain structured model...", flush=True)
    structured_model = model.with_structured_output(
        SubmissionDiagnosis,
        method="json_schema",
        strict=True,
    )
    messages = [
        (message["role"], message["content"])
        for message in build_messages(source_code, compiler_output)
    ]
    result = structured_model.invoke(messages)
    return cast(SubmissionDiagnosis, result)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Run a structured diagnosis smoke test through OpenRouter."
    )
    parser.add_argument(
        "--langchain",
        action="store_true",
        help=(
            "use langchain-openrouter instead of direct HTTP. "
            "This currently reproduces the SDK/network hang on this machine."
        ),
    )
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    load_dotenv(PROJECT_ROOT / ".env")

    api_key = require_env("OPENROUTER_API_KEY")
    model_name = require_env("CHAT_MODEL")

    print(f"Python: {sys.version.split()[0]}", flush=True)
    print(f"Model: {model_name}", flush=True)

    if args.langchain:
        diagnosis = check_with_langchain(api_key, model_name)
    else:
        diagnosis = check_with_http(api_key, model_name)

    print("\nStructured diagnosis created successfully:\n", flush=True)
    print(
        json.dumps(diagnosis.model_dump(), ensure_ascii=False, indent=2),
        flush=True,
    )

    assert diagnosis.error_type == "compile_error"
    assert 0 <= diagnosis.confidence <= 1
    assert diagnosis.hints

    print("\nAll checks passed.", flush=True)


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        print("\nStructured diagnosis test failed.", file=sys.stderr, flush=True)
        print(
            f"Exception: {type(exc).__name__}: {exc}",
            file=sys.stderr,
            flush=True,
        )
        print("\nFull traceback:", file=sys.stderr, flush=True)
        traceback.print_exc()
        raise SystemExit(1) from exc
