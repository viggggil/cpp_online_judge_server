FROM ubuntu:24.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    pkg-config \
    libssl-dev \
    libcrypt-dev \
    libhiredis-dev \
    libredis++-dev \
    libmysqlcppconn-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . /src

RUN cmake -S /src -B /src/build -DCMAKE_BUILD_TYPE=Release \
    && cmake --build /src/build -j"$(nproc)"

FROM ubuntu:24.04 AS runtime-base

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    libssl3 \
    libcrypt1 \
    libhiredis1.1.0 \
    libredis++1 \
    libmysqlcppconn7v5 \
    g++ \
    && rm -rf /var/lib/apt/lists/*

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