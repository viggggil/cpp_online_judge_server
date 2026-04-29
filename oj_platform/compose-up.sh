#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT_DIR"

echo "[oj_platform] 检查并清理可能导致 docker-compose v1 失败的旧容器..."

mapfile -t containers < <(
  docker ps -a --format '{{.Names}}' | grep -E '(^|_)oj_platform_(mysql|redis|oj_server|judge_dispatcher|judge_worker_[123])$|^(mysql|redis|oj_server|judge_dispatcher|judge_worker_[123])$' || true
)

if ((${#containers[@]} > 0)); then
  echo "[oj_platform] 将删除以下旧容器：${containers[*]}"
  docker rm -f "${containers[@]}" >/dev/null
else
  echo "[oj_platform] 未发现需要清理的旧容器"
fi

echo "[oj_platform] 重新构建并启动服务..."
docker-compose up -d --build "$@"

echo "[oj_platform] 启动命令已执行，可使用 docker-compose ps 查看状态"