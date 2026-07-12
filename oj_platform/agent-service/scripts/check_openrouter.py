import argparse
import os
import sys
from pathlib import Path

import httpx
from dotenv import load_dotenv
from langchain_openrouter import ChatOpenRouter


PROJECT_ROOT = Path(__file__).resolve().parents[1]
OPENROUTER_CHAT_URL = "https://openrouter.ai/api/v1/chat/completions"


def check_openrouter_http(api_key: str, model_name: str) -> None:
    print("Calling OpenRouter directly through HTTP...")
    response = httpx.post(
        OPENROUTER_CHAT_URL,
        headers={
            "Authorization": f"Bearer {api_key}",
            "Content-Type": "application/json",
        },
        json={
            "model": model_name,
            "messages": [
                {
                    "role": "user",
                    "content": "Reply with exactly: OPENROUTER_HTTP_OK",
                }
            ],
            "temperature": 0,
            "max_tokens": 100,
        },
        timeout=30,
    )
    response.raise_for_status()

    payload = response.json()
    print("\nHTTP response:")
    print(payload["choices"][0]["message"]["content"])


def check_openrouter_langchain(api_key: str, model_name: str) -> None:
    print("Calling OpenRouter through LangChain...")

    model = ChatOpenRouter(
        api_key=api_key,
        model=model_name,
        temperature=0,
        max_tokens=100,
        max_retries=0,
        timeout=30,
    )

    response = model.invoke(
        [
            (
                "system",
                "You are a connection test program. "
                "Follow the user's output requirement exactly.",
            ),
            (
                "human",
                "Reply with exactly: LANGCHAIN_OPENROUTER_OK",
            ),
        ]
    )

    print("\nModel response:")
    print(response.content)

    print("\nUsage metadata:")
    print(response.usage_metadata)

    print("\nResponse metadata:")
    print(response.response_metadata)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Check OpenRouter connectivity.")
    parser.add_argument(
        "--langchain",
        action="store_true",
        help="also check through langchain-openrouter",
    )
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    load_dotenv(PROJECT_ROOT / ".env")

    api_key = os.getenv("OPENROUTER_API_KEY")
    model_name = os.getenv("CHAT_MODEL")

    if not api_key:
        raise RuntimeError("OPENROUTER_API_KEY 未配置")

    if not model_name:
        raise RuntimeError("CHAT_MODEL 未配置")

    print(f"Python: {sys.version.split()[0]}")
    print(f"Model: {model_name}")

    check_openrouter_http(api_key, model_name)
    if args.langchain:
        print()
        check_openrouter_langchain(api_key, model_name)


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        print(f"\nConnection failed: {type(exc).__name__}")
        print(exc)
        raise SystemExit(1) from exc
