#!/usr/bin/env python3
"""List the data-pack files that come from outside the ROM, for gen_recipe.

    python tools/gen_externals.py --decomp build/upstream --out build/externals.json \\
        --source aarant/pokeemerald <commit> <git dir> <placed list> [--source ...]

Each --source is a repository at a commit whose files a build placed in the
decomp tree (tools/bootstrap.py's .emerald3ds-followers for [followers] in
upstream.lock), with a git directory that has that commit. A later --source
wins for a file both placed.

For every placed file, its converted outputs in the tree (what pret's
Makefile made from it with gbagfx: tiles, palettes, LZ) and the file itself
when used raw are listed with the steps that made them. Each is converted again
here from the bytes GitHub serves for that commit (the git blob, not the
checkout, whose line endings may differ) with the builder's own converter, and
must come out identical: a release recipe only names files the builder can
reproduce. gen_recipe then copies from them ("X") whatever the ROM does not have.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
import sys
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "builder"))
from emerald3ds_builder import externals as ext  # noqa: E402

OUTPUT_EXTS = ("1bpp", "4bpp", "8bpp", "gbapal")


def git_blob(git_dir: Path, commit: str, path: str) -> bytes:
    return subprocess.run(["git", "-C", str(git_dir), "show", "%s:%s" % (commit, path)],
                          capture_output=True, check=True).stdout


def gbagfx_rules(decomp: Path, outputs: list[str]) -> dict[str, tuple[str, list[str]]]:
    """output -> (input, options), from make's own rules (dry run, forced)."""
    rules = {}
    for i in range(0, len(outputs), 400):
        dry = subprocess.run(["make", "-n", "-B", "--no-print-directory"] + outputs[i:i + 400],
                             cwd=decomp, capture_output=True, text=True).stdout
        for line in dry.splitlines():
            m = re.match(r"\S*gbagfx\S*\s+(\S+)\s+(\S+)(.*)", line.strip())
            if m:
                rules[m.group(2)] = (m.group(1), m.group(3).split())
    return rules


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--decomp", type=Path, required=True)
    ap.add_argument("--romfs", type=Path, default=None, help="only outputs the port stages (default: <decomp>/3ds_port/romfs)")
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--source", nargs=4, action="append", required=True,
                    metavar=("REPO", "COMMIT", "GIT_DIR", "PLACED_LIST"))
    args = ap.parse_args()
    decomp = args.decomp.resolve()
    romfs = (args.romfs or decomp / "3ds_port" / "romfs").resolve()

    owner = {}   # placed path -> (repo, commit, git dir)
    for repo, commit, git_dir, listed in args.source:
        for path in Path(listed).read_text(encoding="utf-8").split():
            owner[path] = (repo, commit, Path(git_dir))

    # Candidate outputs of each placed file that the tree has.
    candidates = []
    for path in sorted(owner):
        stem = path.rsplit(".", 1)[0]
        for out in [path] + ["%s.%s" % (stem, e) for e in OUTPUT_EXTS]:
            for o in (out, out + ".lz"):
                if (decomp / o).is_file():
                    candidates.append(o)
    rules = gbagfx_rules(decomp, sorted(set(c for c in candidates if c not in owner)))

    listed, failed = [], []
    blobs = {}
    for out in sorted(set(candidates)):
        # The chain of gbagfx steps back to a placed file.
        steps, cur = [], out
        while cur not in owner and cur in rules:
            src, opts = rules[cur]
            steps.insert(0, [src.rsplit(".", 1)[-1], cur.rsplit(".", 1)[-1], opts])
            cur = src
        if cur not in owner:
            continue
        if not steps and out.rsplit(".", 1)[-1] in ("png", "pal", "pla", "c", "h", "s", "inc", "json", "txt"):
            continue   # sources used raw only matter when the port stages them
        # Raw: what the port stages, and binary data (map layouts, metatiles)
        # the builder's 3D scenery generators read as inputs.
        if not steps and not (romfs / out).is_file() and not out.endswith(".bin"):
            continue
        repo, commit, git_dir = owner[cur]
        key = (repo, commit, cur)
        if key not in blobs:
            blobs[key] = git_blob(git_dir, commit, cur)
        source = blobs[key]
        want = (decomp / out).read_bytes()
        try:
            got = ext.convert({"path": cur, "steps": steps}, source)
        except Exception as exc:          # noqa: BLE001 - reported below
            failed.append("%s: %s" % (out, exc))
            continue
        if got != want:
            failed.append("%s: the builder's conversion differs" % out)
            continue
        listed.append({"repo": repo, "commit": commit, "path": cur,
                       "sha256": hashlib.sha256(source).hexdigest(), "steps": steps,
                       "size": len(want), "crc": zlib.crc32(want) & 0xFFFFFFFF, "tree": out})
    if failed:
        print("gen_externals: %d outputs the builder cannot reproduce:" % len(failed))
        for f in failed[:20]:
            print("   " + f)
        raise SystemExit(1)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(listed, indent=1), encoding="utf-8")
    print("gen_externals: %d externals from %d sources, %d bytes -> %s"
          % (len(listed), len(blobs), sum(e["size"] for e in listed), args.out))


if __name__ == "__main__":
    main()
