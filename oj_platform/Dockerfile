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
    && rm -rf /var/lib/apt/lists/*

RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates \
    curl \
    unzip \
    libssl3 \
    libcrypt1 \
    libhiredis1.1.0 \
    libmysqlcppconn7t64 \
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
