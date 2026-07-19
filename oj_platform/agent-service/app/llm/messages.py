from langchain_core.messages import BaseMessage


def lc_messages_to_openrouter(messages: list[BaseMessage]) -> list[dict[str, str]]:
    converted: list[dict[str, str]] = []
    for message in messages:
        content = message.content
        if isinstance(content, list):
            text = "\n".join(str(part) for part in content)
        else:
            text = str(content)

        role = message.type
        if role == "human":
            role = "user"
        elif role == "ai":
            role = "assistant"
        converted.append({"role": role, "content": text})
    return converted
