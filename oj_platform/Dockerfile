ARG AGENT_PYTHON_IMAGE=python:3.11-slim

FROM ubuntu:24.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    ca-certificates \
    cmake \
    git \
    pkg-config \
    libssl-dev \
    libcrypt-dev \
    libhiredis-dev \
    libasio-dev \
    libmysqlcppconn-dev \
    librabbitmq-dev \
    && rm -rf /var/lib/apt/lists/*

RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates \
    curl \
    unzip \
    libssl3 \
    libcrypt1 \
    libhiredis1.1.0 \
    libmysqlcppconn7t64 \
    librabbitmq4 \
    g++ \
    && curl -L https://dl.min.io/client/mc/release/linux-amd64/mc -o /usr/local/bin/mc \
    && chmod +x /usr/local/bin/mc \
    && rm -rf /var/lib/apt/lists/*

RUN git clone --depth 1 https://github.com/sewenew/redis-plus-plus.git /tmp/redis-plus-plus \
    && cmake -S /tmp/redis-plus-plus -B /tmp/redis-plus-plus/build \
        -DCMAKE_BUILD_TYPE=Release \
        -DREDIS_PLUS_PLUS_BUILD_TEST=OFF \
        -DREDIS_PLUS_PLUS_BUILD_STATIC=OFF \
        -DREDIS_PLUS_PLUS_BUILD_SHARED=ON \
    && cmake --build /tmp/redis-plus-plus/build -j"$(nproc)" \
    && cmake --install /tmp/redis-plus-plus/build \
    && ldconfig \
    && rm -rf /tmp/redis-plus-plus

WORKDIR /src
COPY oj_platform /src
COPY Crow /Crow
RUN rm -rf /src/third_party/crow && ln -s /Crow /src/third_party/crow

RUN cmake -S /src -B /src/build -DCMAKE_BUILD_TYPE=Release \
    && cmake --build /src/build -j"$(nproc)"

FROM ubuntu:24.04 AS runtime-base

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    libssl3 \
    libcrypt1 \
    libhiredis1.1.0 \
    libmysqlcppconn7t64 \
    librabbitmq4 \
    g++ \
    curl \
    unzip \
    && rm -rf /var/lib/apt/lists/*

COPY --from=builder /usr/local/lib/libredis++.so* /usr/local/lib/
COPY --from=builder /usr/local/bin/mc /usr/local/bin/mc
RUN ldconfig

WORKDIR /app

COPY --from=builder /src/problems /app/problems
COPY --from=builder /src/sql /app/sql
COPY --from=builder /src/web /app/web

FROM runtime-base AS oj_server
COPY --from=builder /src/build/oj_server /app/bin/oj_server
EXPOSE 18080
CMD ["/app/bin/oj_server"]

FROM runtime-base AS judge_dispatcher
COPY --from=builder /src/build/judge_dispatcher /app/bin/judge_dispatcher
CMD ["/app/bin/judge_dispatcher"]

FROM runtime-base AS judge_worker
COPY --from=builder /src/build/judge_worker /app/bin/judge_worker
EXPOSE 18081
CMD ["/app/bin/judge_worker"]

FROM ${AGENT_PYTHON_IMAGE} AS agent_service

ARG PIP_INDEX_URL
ARG PIP_TRUSTED_HOST
ARG PIP_DEFAULT_TIMEOUT=300
ARG PIP_RETRIES=3

ENV PYTHONDONTWRITEBYTECODE=1 \
    PYTHONUNBUFFERED=1 \
    PIP_DISABLE_PIP_VERSION_CHECK=1 \
    PIP_DEFAULT_TIMEOUT=${PIP_DEFAULT_TIMEOUT} \
    PIP_RETRIES=${PIP_RETRIES}

WORKDIR /app

RUN if [ -n "$PIP_INDEX_URL" ]; then pip config set global.index-url "$PIP_INDEX_URL"; fi \
    && if [ -n "$PIP_TRUSTED_HOST" ]; then pip config set global.trusted-host "$PIP_TRUSTED_HOST"; fi

COPY oj_platform/agent-service/pyproject.toml oj_platform/agent-service/uv.lock oj_platform/agent-service/README.md oj_platform/agent-service/main.py ./
COPY oj_platform/agent-service/vendor/wheels ./vendor/wheels
RUN python - <<'PY'
import tomllib

deps = tomllib.load(open("pyproject.toml", "rb"))["project"]["dependencies"]
embedding_deps = [dep for dep in deps if dep.split(">=", 1)[0] == "fastembed"]
base_deps = [dep for dep in deps if dep not in embedding_deps]

open("/tmp/agent-service-requirements-base.txt", "w").write(
    "\n".join(base_deps) + "\n"
)
open("/tmp/agent-service-requirements-embedding.txt", "w").write(
    "\n".join(embedding_deps) + "\n"
)
PY

RUN echo "Base Python requirements:" \
    && cat /tmp/agent-service-requirements-base.txt \
    && if find /app/vendor/wheels -type f -name '*.whl' | grep -q .; then \
        python -m pip install --no-index --find-links /app/vendor/wheels -r /tmp/agent-service-requirements-base.txt; \
    else \
        python -m pip install --no-cache-dir --prefer-binary --retries "$PIP_RETRIES" --timeout "$PIP_DEFAULT_TIMEOUT" -r /tmp/agent-service-requirements-base.txt; \
    fi \
    && echo "Embedding Python requirements:" \
    && cat /tmp/agent-service-requirements-embedding.txt \
    && if find /app/vendor/wheels -type f -name '*.whl' | grep -q .; then \
        python -m pip install --no-index --find-links /app/vendor/wheels -r /tmp/agent-service-requirements-embedding.txt; \
    else \
        python -m pip install --no-cache-dir --prefer-binary --retries "$PIP_RETRIES" --timeout "$PIP_DEFAULT_TIMEOUT" -r /tmp/agent-service-requirements-embedding.txt; \
    fi

COPY oj_platform/agent-service/app ./app
COPY oj_platform/agent-service/knowledge ./knowledge
COPY oj_platform/agent-service/data ./data

EXPOSE 8001
CMD ["uvicorn", "app.main:app", "--host", "0.0.0.0", "--port", "8001"]
