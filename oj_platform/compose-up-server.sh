#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT_DIR"

echo "[oj_platform] 使用预构建镜像启动服务器环境..."
docker compose -f docker-compose.server.yml up -d "$@"

echo "[oj_platform] 当前服务状态："
docker compose -f docker-compose.server.yml ps
