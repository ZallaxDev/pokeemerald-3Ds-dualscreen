#!/bin/bash
# Docker Desktop on macOS; also usable on Linux.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
IMAGE="emerald3ds-build:local"
args=("$@")
ROM_INPUT=""
trap 'if [[ -n "$ROM_INPUT" ]]; then rm -rf "$ROM_INPUT"; fi' EXIT
if ! command -v docker >/dev/null 2>&1; then
    echo "Install and start Docker Desktop first." >&2
    exit 1
fi
docker build -t "$IMAGE" -f "$ROOT/tools/Dockerfile.build" "$ROOT"
for ((i=0; i<${#args[@]}; i++)); do
    if [[ "${args[$i]}" == --spanish-rom ]]; then
        if ((i+1 >= ${#args[@]})); then
            echo "--spanish-rom requires a ROM file." >&2
            exit 1
        fi
        mkdir -p "$ROOT/build"
        ROM_INPUT="$(mktemp -d "$ROOT/build/rom-input.XXXXXX")"
        cp "${args[$((i+1))]}" "$ROM_INPUT/spanish.gba"
        args[$((i+1))]="/workspace/build/$(basename "$ROM_INPUT")/spanish.gba"
        ((i+=1))
    fi
done
# Separate output tree: never reuse native macOS host tools inside Linux.
docker run --rm --mount "type=bind,source=$ROOT,target=/workspace" "$IMAGE" \
    python3 tools/bootstrap.py --dir build/upstream-docker --make "${args[@]}"
