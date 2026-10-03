#!/usr/bin/env python3
"""Build a complete source tree: pinned upstream + Pokémon Emerald 3Ds Dual Screen patches + port.

    python tools/bootstrap.py                 # -> build/upstream
    python tools/bootstrap.py --make -j8      # and build the 3DSX there
    python tools/bootstrap.py --clean         # start again from the pinned commit

1. Reads upstream.lock and fetches exactly that commit of pret/pokeemerald into
   build/upstream (a shallow fetch of one commit).
2. Applies patches/pokeemerald/*.patch in order. The tree is reset to the
   pinned commit first whenever the patch set changed since the last run.
3. Places 3ds_port/, builder/ and tools/ inside it, so the Makefile's relative
   paths (../tools/port_common, ../builder) resolve as in development.
4. With --make: builds the decomp tools (`make tools`), the generated
   includes (`make generated`) and the 3DSX (`make -C 3ds_port`). Run it from
   a shell where devkitPro, a host compiler and Python are available (on
   Windows, devkitPro's MSYS2 shell with MinGW64 on PATH).
"""

from __future__ import annotations

import argparse
import hashlib
import os
import shutil
import subprocess
import sys
import tomllib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OVERLAY = ["3ds_port", "builder", "tools"]


def run(cmd, cwd=None, env=None):
    print("+ " + " ".join(str(c) for c in cmd), flush=True)
    subprocess.run([str(c) for c in cmd], cwd=cwd, env=env, check=True)


def patch_digest(patches: list[Path]) -> str:
    digest = hashlib.sha256()
    for p in patches:
        digest.update(p.name.encode())
        digest.update(p.read_bytes())
    return digest.hexdigest()


def fetch(tree: Path, repo: str, commit: str) -> None:
    tree.mkdir(parents=True, exist_ok=True)
    if not (tree / ".git").exists():
        run(["git", "init", "-q"], cwd=tree)
        run(["git", "remote", "add", "origin", repo], cwd=tree)
    run(["git", "fetch", "-q", "--depth", "1", "origin", commit], cwd=tree)
    run(["git", "checkout", "-q", "--force", "--detach", commit], cwd=tree)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--dir", type=Path, default=ROOT / "build" / "upstream")
    ap.add_argument("--clean", action="store_true", help="reset the tree to the pinned commit first")
    ap.add_argument("--make-command", default="make", help="make executable (gmake on macOS)")
    ap.add_argument("--host-cc", default="cc", help="native host C compiler")
    ap.add_argument("--host-cxx", default="c++", help="native host C++ compiler")
    ap.add_argument("--make", action="store_true", help="build the tools and the 3DSX afterwards")
    ap.add_argument("-j", "--jobs", type=int, default=4)
    ap.add_argument("--python", default=sys.executable, help="Python the build calls (PYTHON=)")
    args = ap.parse_args()

    lock = tomllib.loads((ROOT / "upstream.lock").read_text(encoding="utf-8"))
    repo, commit = lock["pokeemerald"]["repository"], lock["pokeemerald"]["commit"]
    tree = args.dir.resolve()
    patches = sorted((ROOT / "patches" / "pokeemerald").glob("*.patch"))
    marker = tree / ".emerald3ds-patches"
    digest = patch_digest(patches)

    head = ""
    if (tree / ".git").exists():
        head = subprocess.run(["git", "rev-parse", "HEAD"], cwd=tree, capture_output=True,
                              text=True).stdout.strip()
    stale = not marker.exists() or marker.read_text().strip() != digest
    if args.clean or head != commit or stale:
        if head != commit:
            fetch(tree, repo, commit)
        run(["git", "reset", "-q", "--hard", commit], cwd=tree)
        if args.clean:
            run(["git", "clean", "-q", "-fdx"], cwd=tree)
        # Files a previous patch set created are untracked after the reset;
        # remove the ones these patches create so they apply again.
        for patch in patches:
            for line in patch.read_text(encoding="utf-8", errors="replace").splitlines():
                if line.startswith("+++ b/"):
                    created = tree / line[6:]
                    if created.exists() and subprocess.run(
                            ["git", "ls-files", "--error-unmatch", line[6:]], cwd=tree,
                            capture_output=True).returncode != 0:
                        created.unlink()
        for patch in patches:
            run(["git", "apply", "--whitespace=nowarn", patch], cwd=tree)
        marker.write_text(digest + "\n")
    else:
        print("bootstrap: upstream %s with %d patches already in place" % (commit[:12], len(patches)))

    for name in OVERLAY:
        src = ROOT / name
        if src.exists():
            shutil.copytree(src, tree / name, dirs_exist_ok=True,
                            ignore=shutil.ignore_patterns("__pycache__", "*.pyc", "build", "dist"))
    for name in ("upstream.lock", "LICENSE-PORT.md", "NOTICE.md", "AI_DISCLOSURE.md"):
        source = ROOT / name
        if source.exists():
            shutil.copy2(source, tree / name)
    print("bootstrap: tree ready at %s" % tree)

    if args.make:
        build_env = dict(os.environ, EMERALD3DS_MAKE=args.make_command)
        run([args.make_command, "tools", "-j%d" % args.jobs, "CC=" + args.host_cc, "CXX=" + args.host_cxx], cwd=tree, env=build_env)
        run([args.make_command, "generated", "-j%d" % args.jobs], cwd=tree, env=build_env)
        run([args.make_command, "-C", "3ds_port", "-j%d" % args.jobs, "PYTHON=%s" % args.python, "HOSTCC=" + args.host_cc], cwd=tree, env=build_env)
    return 0


if __name__ == "__main__":
    sys.exit(main())
