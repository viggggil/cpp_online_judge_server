#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
OJ_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
REPO_ROOT="$(cd "$OJ_DIR/.." && pwd)"

TAG="${OJ_IMAGE_TAG:-prod}"
PLATFORM="${OJ_IMAGE_PLATFORM:-}"

cd "$REPO_ROOT"

build_one() {
  local target="$1"
  local image="$2"

  echo "[build] target=${target}, image=${image}:${TAG}"

  if [[ -n "$PLATFORM" ]]; then
    docker buildx build \
      --platform "$PLATFORM" \
      -f oj_platform/Dockerfile \
      --target "$target" \
      -t "${image}:${TAG}" \
      --load \
      .
  else
    docker build \
      -f oj_platform/Dockerfile \
      --target "$target" \
      -t "${image}:${TAG}" \
      .
  fi
}

build_one oj_server cpp_oj_server
build_one judge_dispatcher cpp_oj_judge_dispatcher
build_one judge_worker cpp_oj_judge_worker

echo "[build] done"
