from pydantic import BaseModel, ConfigDict


class SourceReference(BaseModel):
    model_config = ConfigDict(extra="forbid")

    document_id: str
    source: str
    title: str | None = None
    knowledge_point: str | None = None
    chunk_index: int | None = None
    score: float | None = None


class RetrievedKnowledge(BaseModel):
    model_config = ConfigDict(extra="forbid")

    content: str
    source: SourceReference
    distance: float = 0
