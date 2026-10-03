#!/bin/bash
# Docker Desktop on macOS; also usable on Linux.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
IMAGE="emerald3ds-build:local"
if ! command -v docker >/dev/null 2>&1; then
    echo "Install and start Docker Desktop first." >&2
    exit 1
fi
docker build -t "$IMAGE" -f "$ROOT/tools/Dockerfile.build" "$ROOT"
# Separate output tree: never reuse native macOS host tools inside Linux.
docker run --rm --mount "type=bind,source=$ROOT,target=/workspace" "$IMAGE" \
    python3 tools/bootstrap.py --dir build/upstream-docker --make "$@"
