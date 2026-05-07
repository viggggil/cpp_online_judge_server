#!/usr/bin/env bash
set -euo pipefail

TAG="${OJ_IMAGE_TAG:-prod}"
OUTPUT="${1:-oj_images_${TAG}.tar}"

echo "[save] output=${OUTPUT}"

docker save \
  -o "$OUTPUT" \
  "cpp_oj_server:${TAG}" \
  "cpp_oj_judge_dispatcher:${TAG}" \
  "cpp_oj_judge_worker:${TAG}"

echo "[save] done: ${OUTPUT}"
