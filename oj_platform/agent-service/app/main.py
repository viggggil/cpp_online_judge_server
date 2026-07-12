from fastapi import FastAPI

app = FastAPI(
    title="OJ Programming Tutor",
    version="0.1.0",
)


@app.get("/health")
async def health_check() -> dict[str, str]:
    return {
        "status": "ok",
        "service": "oj-programming-tutor",
    }