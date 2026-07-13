import os
import sys

from dotenv import load_dotenv
from langchain_openrouter import ChatOpenRouter


def main() -> None:
    """Test OpenRouter through LangChain's ChatOpenRouter integration."""

    load_dotenv()

    api_key = os.getenv("OPENROUTER_API_KEY")
    model_name = os.getenv("CHAT_MODEL")

    if not api_key:
        raise RuntimeError("OPENROUTER_API_KEY is missing")

    if not model_name:
        raise RuntimeError("CHAT_MODEL is missing")

    print(f"Python: {sys.version.split()[0]}")
    print(f"Model: {model_name}")
    print("Calling OpenRouter through LangChain...")

    # ChatOpenRouter automatically reads OPENROUTER_API_KEY
    model = ChatOpenRouter(
        model=model_name,
        temperature=0,
        max_tokens=100,
        max_retries=2,
    )

    response = model.invoke(
        [
            (
                "system",
                "You are a connection test program. "
                "Follow the requested output format exactly.",
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

    if "LANGCHAIN_OPENROUTER_OK" not in str(response.content):
        raise RuntimeError(
            "The model responded, but the expected text was not found."
        )


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        print(f"\nTest failed: {type(exc).__name__}")
        print(exc)
        raise SystemExit(1) from exc
