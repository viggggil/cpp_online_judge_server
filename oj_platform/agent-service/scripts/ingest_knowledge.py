import sys
from pathlib import Path

from dotenv import load_dotenv


PROJECT_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PROJECT_ROOT))

from app.rag.ingest import ingest_from_env  # noqa: E402


def main() -> None:
    load_dotenv(PROJECT_ROOT / ".env")
    result = ingest_from_env()

    print("Knowledge ingest completed")
    print(f"collection: {result.collection_name}")
    print(f"embedding_model: {result.embedding_model}")
    print(f"documents: {result.documents}")
    print(f"chunks: {result.chunks}")
    print(f"chroma_dir: {result.persist_dir}")
    print(f"embedding_cache_dir: {result.cache_dir}")


if __name__ == "__main__":
    main()
