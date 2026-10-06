#!/usr/bin/env python3
"""Re-pin a locale manifest (tools/locales/spanish.json.gz) after port patches
change files it lists.

The manifest pins each source file by the SHA-256 of the patched tree and
replaces text at character positions in it (tools/localize_spanish.py). A new
patch that edits such a file elsewhere (a follower script in a map's
scripts.inc, for example) moves those positions and changes the hash, and the
localization stops with "Source differs from the pinned patched tree".

Given the tree the manifest was pinned against and the new patched tree, this
moves every edit along with the unchanged lines around it, refuses an edit on
a line the new patches changed (that one needs a new mapping by hand), checks
that each edit covers the same text as before, and writes the new positions
and hashes. Nothing else in the manifest changes, and it never reads a ROM.

    python tools/repin_locale.py --pinned PINNED_TREE --tree NEW_TREE
    python tools/repin_locale.py --check --tree NEW_TREE
"""
from __future__ import annotations

import argparse
import difflib
import gzip
import hashlib
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_MANIFEST = ROOT / "tools/locales/spanish.json.gz"


def load(path: Path) -> dict:
    return json.loads(gzip.decompress(path.read_bytes()))


def save(path: Path, manifest: dict) -> None:
    # The checked-in form: compact JSON, gzip with no timestamp (reproducible).
    data = json.dumps(manifest, separators=(",", ":"), ensure_ascii=False).encode()
    path.write_bytes(gzip.compress(data, mtime=0))


def sha256(text: str) -> str:
    return hashlib.sha256(text.encode()).hexdigest()


def line_starts(lines: list[str]) -> list[int]:
    starts, offset = [], 0
    for line in lines:
        starts.append(offset)
        offset += len(line)
    starts.append(offset)
    return starts


def move_edits(relative: str, old: str, new: str, edits: list) -> list:
    """The edits' positions in `new`, through the lines both texts share."""
    old_lines, new_lines = old.splitlines(keepends=True), new.splitlines(keepends=True)
    old_starts, new_starts = line_starts(old_lines), line_starts(new_lines)
    # Old line -> new line, for lines in unchanged blocks only.
    same = {}
    for tag, i1, i2, j1, _ in difflib.SequenceMatcher(None, old_lines, new_lines, autojunk=False).get_opcodes():
        if tag == "equal":
            for k in range(i2 - i1):
                same[i1 + k] = j1 + k

    def line_of(offset: int) -> int:
        lo, hi = 0, len(old_lines) - 1
        while lo < hi:
            mid = (lo + hi + 1) // 2
            if old_starts[mid] <= offset:
                lo = mid
            else:
                hi = mid - 1
        return lo

    moved = []
    for start, end, *rest in edits:
        first, last = line_of(start), line_of(end - 1)
        if any(k not in same for k in range(first, last + 1)) or same[last] - same[first] != last - first:
            raise SystemExit("repin_locale: %s: the edit at %d-%d is on a line the patches changed"
                             % (relative, start, end))
        new_start = new_starts[same[first]] + start - old_starts[first]
        new_end = new_starts[same[last]] + end - old_starts[last]
        if new[new_start:new_end] != old[start:end]:
            raise SystemExit("repin_locale: %s: the edit at %d-%d moved onto other text" % (relative, start, end))
        moved.append([new_start, new_end, *rest])
    return moved


def check(manifest: dict, tree: Path) -> int:
    """What localize_spanish.py checks before it reads the ROM."""
    problems = 0
    for relative, file in manifest["files"].items():
        text = (tree / relative).read_text(encoding="utf-8")
        if sha256(text) != file["sha256"]:
            print("differs from the manifest: " + relative)
            problems += 1
            continue
        previous_end = 0
        for start, end, *_ in sorted(file["edits"]):
            if not previous_end <= start < end <= len(text):
                print("overlapping or out-of-range edit: %s %d-%d" % (relative, start, end))
                problems += 1
            previous_end = end
    print("repin_locale: %d files checked, %d problems" % (len(manifest["files"]), problems))
    return 1 if problems else 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST)
    ap.add_argument("--pinned", type=Path, help="the patched tree the manifest was pinned against")
    ap.add_argument("--tree", type=Path, required=True, help="the patched tree to pin it to")
    ap.add_argument("--check", action="store_true", help="only check the manifest against --tree")
    args = ap.parse_args()

    manifest = load(args.manifest)
    if args.check:
        return check(manifest, args.tree)
    if args.pinned is None:
        ap.error("--pinned is required unless --check")
    changed = 0
    for relative, file in manifest["files"].items():
        old = (args.pinned / relative).read_text(encoding="utf-8")
        if sha256(old) != file["sha256"]:
            raise SystemExit("repin_locale: %s does not match the manifest in --pinned" % relative)
        new = (args.tree / relative).read_text(encoding="utf-8")
        if new == old:
            continue
        file["edits"] = move_edits(relative, old, new, file["edits"])
        file["sha256"] = sha256(new)
        changed += 1
        print("re-pinned %s (%d edits)" % (relative, len(file["edits"])))
    save(args.manifest, manifest)
    print("repin_locale: %d of %d files re-pinned" % (changed, len(manifest["files"])))
    return check(manifest, args.tree)


if __name__ == "__main__":
    sys.exit(main())
