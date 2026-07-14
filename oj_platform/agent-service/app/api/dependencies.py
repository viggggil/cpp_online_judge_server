from dataclasses import dataclass
from uuid import uuid4

from fastapi import Header, HTTPException

from app.core.config import get_settings


@dataclass(frozen=True)
class RequestContext:
    request_id: str


async def require_internal_context(
    x_internal_token: str = Header(default="", alias="X-Internal-Token"),
    x_request_id: str = Header(default="", alias="X-Request-Id"),
) -> RequestContext:
    settings = get_settings()
    if not settings.internal_api_token or x_internal_token != settings.internal_api_token:
        raise HTTPException(status_code=401, detail="invalid internal token")

    return RequestContext(request_id=x_request_id or f"req_{uuid4().hex}")
