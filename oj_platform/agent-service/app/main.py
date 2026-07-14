from fastapi import FastAPI

from app.api.diagnoses import router as diagnoses_router
from app.api.health import router as health_router


app = FastAPI(
    title="OJ Programming Tutor",
    version="0.1.0",
)

app.include_router(health_router)
app.include_router(diagnoses_router)
