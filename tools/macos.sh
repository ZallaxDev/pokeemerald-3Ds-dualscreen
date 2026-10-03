#!/bin/bash
# macOS entry point; compatible with Apple's Bash 3.2.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
if [[ "$(uname -s)" != Darwin ]]; then
    echo "This entry point is for macOS." >&2
    exit 1
fi
mode="${1:-builder}"
if [[ $# -gt 0 ]]; then shift; fi
PYTHON_BIN=""
for candidate in "${PYTHON:-}" "$ROOT/.venv/bin/python" /opt/homebrew/bin/python3 /usr/local/bin/python3 python3; do
    if [[ -n "$candidate" ]] && command -v "$candidate" >/dev/null 2>&1 &&
       "$candidate" -c 'import sys; sys.exit(sys.version_info < (3,11))' 2>/dev/null; then
        PYTHON_BIN="$candidate"
        break
    fi
done
if [[ -z "$PYTHON_BIN" ]]; then
    echo "Python 3.11+ is required. Install it with Homebrew: brew install python python-tk" >&2
    exit 1
fi
export DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
export DEVKITARM="${DEVKITARM:-$DEVKITPRO/devkitARM}"
export PATH="$DEVKITARM/bin:$DEVKITPRO/tools/bin:$PATH"
export PYTHONPATH="$ROOT/builder${PYTHONPATH:+:$PYTHONPATH}"
MAKE_BIN=""
if [[ "$mode" == check || "$mode" == tools || "$mode" == build ]]; then
    for candidate in gmake /opt/homebrew/bin/gmake /usr/local/bin/gmake make; do
        if command -v "$candidate" >/dev/null 2>&1 &&
           "$candidate" --version | "$PYTHON_BIN" -c 'import re, sys; m=re.search(r"GNU Make (\d+)\.(\d+)", sys.stdin.read()); sys.exit(not m or tuple(map(int,m.groups())) < (4,3))'; then
            MAKE_BIN="$candidate"
            break
        fi
    done
    if [[ -z "$MAKE_BIN" ]]; then
        echo "GNU Make 4.3+ is required. Install it with: brew install make" >&2
        exit 1
    fi
fi
case "$mode" in
    setup)
        "$PYTHON_BIN" -m venv "$ROOT/.venv"
        "$ROOT/.venv/bin/python" -m pip install -r builder/requirements.txt
        echo "Builder ready. Use: ./tools/macos.sh builder --payload PAYLOAD install --rom ROM --sd SD"
        ;;
    builder)
        if ! "$PYTHON_BIN" -c 'import PIL' 2>/dev/null; then
            echo "Pillow is missing. Run ./tools/macos.sh setup first." >&2
            exit 1
        fi
        if [[ $# -eq 0 ]] && ! "$PYTHON_BIN" -c 'import tkinter' 2>/dev/null; then
            echo "Tk is missing. Install matching python-tk via Homebrew, or use the CLI." >&2
            exit 1
        fi
        exec "$PYTHON_BIN" -m emerald3ds_builder "$@"
        ;;
    check)
        "$PYTHON_BIN" -m unittest discover -s builder/tests
        "$MAKE_BIN" -C 3ds_port verify-input verify-video verify-pak HOSTCC=clang "PYTHON=$PYTHON_BIN"
        ;;
    tools|build)
        for tool in git clang clang++ pkg-config; do
            if ! command -v "$tool" >/dev/null 2>&1; then
                echo "Missing $tool. Install Xcode command-line tools and brew install pkgconf libpng." >&2
                exit 1
            fi
        done
        if ! pkg-config --exists libpng; then
            echo "libpng is missing. Install it with: brew install libpng" >&2
            exit 1
        fi
        if [[ "$mode" == build ]]; then
            for tool in "$DEVKITARM/bin/arm-none-eabi-gcc" "$DEVKITPRO/tools/bin/picasso" "$DEVKITPRO/tools/bin/3dsxtool"; do
                if [[ ! -x "$tool" ]]; then
                    echo "Missing devkitPro tool: $tool. Install devkitPro pacman and the 3ds-dev group; see docs/MACOS.md." >&2
                    exit 1
                fi
            done
            if ! "$PYTHON_BIN" -c 'import PIL' 2>/dev/null; then
                echo "Pillow is missing. Run ./tools/macos.sh setup." >&2
                exit 1
            fi
            exec "$PYTHON_BIN" tools/bootstrap.py --make --make-command "$MAKE_BIN" --host-cc clang --host-cxx clang++ --python "$PYTHON_BIN" "$@"
        fi
        "$PYTHON_BIN" tools/bootstrap.py "$@"
        exec "$MAKE_BIN" -C build/upstream tools -j4 CC=clang CXX=clang++
        ;;
    *)
        echo "Usage: ./tools/macos.sh {setup|builder|check|tools|build} [arguments]" >&2
        exit 2
        ;;
esac
