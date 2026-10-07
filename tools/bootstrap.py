#!/usr/bin/env python3
"""Build a complete source tree: pinned upstream + Pokémon Emerald 3Ds Dual Screen patches + port.

    python tools/bootstrap.py                 # -> build/upstream
    python tools/bootstrap.py --make -j8      # and build the 3DSX there
    python tools/bootstrap.py --clean         # start again from the pinned commit
    python tools/bootstrap.py --make --spanish-rom esmeralda.gba   # Spanish (BPES) build

1. Reads upstream.lock and fetches exactly that commit of pret/pokeemerald into
   build/upstream (a shallow fetch of one commit).
2. With a [followers] section in upstream.lock, fetches that commit of
   aarant/pokeemerald (icons-followers) next to the tree and places the
   files it adds and the graphics it changes (follower sprites, palettes,
   balls, its new code); its changes to existing files are in the patch set.
3. Applies patches/pokeemerald/*.patch in order. The tree is reset to the
   pinned commit first whenever the patch set (or the followers commit)
   changed since the last run.
4. Places 3ds_port/, builder/ and tools/ inside it, so the Makefile's relative
   paths (../tools/port_common, ../builder) resolve as in development.
5. With --make: builds the decomp tools (`make tools`), the generated
   includes (`make generated`) and the 3DSX (`make -C 3ds_port`). Run it from
   a shell where devkitPro, a host compiler and Python are available (on
   Windows, devkitPro's MSYS2 shell with MinGW64 on PATH).
5. With --spanish-rom: after the generated includes, tools/localize_spanish.py
   stages the Spanish texts and graphics from the player's clean BPES ROM
   (docs/SPANISH.md). Switching language back and forth resets the tree.
"""

from __future__ import annotations

import argparse
import hashlib
import re
import shutil
import subprocess
import sys
import tarfile
import tomllib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OVERLAY = ["3ds_port", "builder", "tools"]
# The followers branch's own tooling and repository files (and a stray patch
# file it carries): not game files.
FOLLOWERS_SKIP = re.compile(r"^(\.github/|\.gitignore$|\.git-blame-ignore-revs$|README\.md$|compile_flags\.txt$"
                            r"|remote_build\.sh$|ld_script\.ld$|[a-z_]+\.py$|.*\.patch$)")
FOLLOWERS_PLACED = ".emerald3ds-followers"


def run(cmd, cwd=None):
    print("+ " + " ".join(str(c) for c in cmd), flush=True)
    subprocess.run([str(c) for c in cmd], cwd=cwd, check=True)


def patch_digest(patches: list[Path], extra: str = "") -> str:
    digest = hashlib.sha256()
    for p in patches:
        digest.update(p.name.encode())
        digest.update(p.read_bytes())
    if extra:
        digest.update(extra.encode())
    return digest.hexdigest()


def fetch(tree: Path, repo: str, *commits: str, checkout: bool = True) -> None:
    tree.mkdir(parents=True, exist_ok=True)
    if not (tree / ".git").exists():
        run(["git", "init", "-q"], cwd=tree)
        run(["git", "remote", "add", "origin", repo], cwd=tree)
    run(["git", "fetch", "-q", "--depth", "1", "origin", *commits], cwd=tree)
    if checkout:
        run(["git", "checkout", "-q", "--force", "--detach", commits[-1]], cwd=tree)


def followers_files(src: Path, base: str, commit: str) -> set[str]:
    """What the followers branch adds, and the graphics and binary files it
    changes. Its other changes (code, scripts, data) are in the patch set."""
    git = ["git", "diff", "--no-renames", base, commit]
    status = subprocess.run(git + ["--name-status"], cwd=src, capture_output=True, text=True, check=True).stdout
    numstat = subprocess.run(git + ["--numstat"], cwd=src, capture_output=True, text=True, check=True).stdout
    binary = {line.split("\t", 2)[2] for line in numstat.splitlines() if line.startswith("-\t-\t")}
    files = set()
    for line in status.splitlines():
        st, path = line.split("\t", 1)
        if FOLLOWERS_SKIP.match(path):
            continue
        if st == "A" or (st == "M" and (path.startswith("graphics/") or path in binary)):
            files.add(path)
    return files


def place_followers(tree: Path, spec: dict) -> None:
    src = tree.parent / (tree.name + "-followers")
    fetch(src, spec["repository"], spec["base"], spec["commit"], checkout=False)
    wanted = followers_files(src, spec["base"], spec["commit"])
    # One archive of the commit, streamed: only the wanted members are written.
    archive = subprocess.Popen(["git", "archive", "--format=tar", spec["commit"]], cwd=src,
                               stdout=subprocess.PIPE)
    placed = []
    with tarfile.open(fileobj=archive.stdout, mode="r|") as tar:
        for member in tar:
            if member.isfile() and member.name in wanted:
                out = tree / member.name
                out.parent.mkdir(parents=True, exist_ok=True)
                out.write_bytes(tar.extractfile(member).read())
                placed.append(member.name)
    if archive.wait() != 0 or len(placed) != len(wanted):
        raise SystemExit("bootstrap: followers %s: placed %d of %d files"
                         % (spec["commit"][:12], len(placed), len(wanted)))
    (tree / FOLLOWERS_PLACED).write_text("\n".join(sorted(placed)) + "\n", encoding="utf-8")
    print("bootstrap: placed %d files from followers %s" % (len(placed), spec["commit"][:12]))


def remove_placed_followers(tree: Path) -> None:
    """After a reset: the files a previous run placed that git does not track
    (the reset restored the tracked ones). src/*.c is globbed by the build."""
    record = tree / FOLLOWERS_PLACED
    if not record.exists():
        return
    tracked = set(subprocess.run(["git", "ls-files"], cwd=tree, capture_output=True, text=True,
                                 check=True).stdout.splitlines())
    for name in record.read_text(encoding="utf-8").split():
        if name not in tracked and (tree / name).exists():
            (tree / name).unlink()
    record.unlink()


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--dir", type=Path, default=ROOT / "build" / "upstream")
    ap.add_argument("--clean", action="store_true", help="reset the tree to the pinned commit first")
    ap.add_argument("--spanish-rom", type=Path, help="stage the Spanish data from a clean BPES ROM")
    ap.add_argument("--make", action="store_true", help="build the tools and the 3DSX afterwards")
    ap.add_argument("-j", "--jobs", type=int, default=4)
    ap.add_argument("--python", default=sys.executable, help="Python the build calls (PYTHON=)")
    args = ap.parse_args()

    lock = tomllib.loads((ROOT / "upstream.lock").read_text(encoding="utf-8"))
    repo, commit = lock["pokeemerald"]["repository"], lock["pokeemerald"]["commit"]
    followers = lock.get("followers")
    tree = args.dir.resolve()
    previous_locale = (tree / ".emerald3ds-locale").exists()
    patches = sorted((ROOT / "patches" / "pokeemerald").glob("*.patch"))
    marker = tree / ".emerald3ds-patches"
    digest = patch_digest(patches, followers["commit"] if followers else "")

    head = ""
    if (tree / ".git").exists():
        head = subprocess.run(["git", "rev-parse", "HEAD"], cwd=tree, capture_output=True,
                              text=True).stdout.strip()
    stale = not marker.exists() or marker.read_text().strip() != digest
    if args.clean or args.spanish_rom or previous_locale or head != commit or stale:
        if head != commit:
            fetch(tree, repo, commit)
        run(["git", "reset", "-q", "--hard", commit], cwd=tree)
        (tree / ".emerald3ds-locale").unlink(missing_ok=True)
        if args.spanish_rom or previous_locale:
            # Objects and staged data of the other language must not be
            # reused: rebuild every native object when the language changes.
            shutil.rmtree(tree / "3ds_port/build", ignore_errors=True)
            shutil.rmtree(tree / "3ds_port/romfs", ignore_errors=True)
            (tree / "src/data/region_map/region_map_entries.h").unlink(missing_ok=True)
        if previous_locale:
            import gzip
            import json
            manifest = json.loads(gzip.decompress((ROOT / "tools/locales/spanish.json.gz").read_bytes()))
            for relative, _, _ in manifest["graphics"]:
                for name in (relative, relative[:-3]) if relative.endswith(".lz") else (relative,):
                    # Tracked art was restored by the reset; generated locale
                    # art must be regenerated from the pinned source PNGs.
                    tracked = subprocess.run(["git", "ls-files", "--error-unmatch", name],
                                             cwd=tree, capture_output=True).returncode == 0
                    if not tracked:
                        (tree / name).unlink(missing_ok=True)
        if args.clean:
            run(["git", "clean", "-q", "-fdx"], cwd=tree)
        remove_placed_followers(tree)
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
        if followers:
            place_followers(tree, followers)
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
    shutil.copy2(ROOT / "upstream.lock", tree / "upstream.lock")
    print("bootstrap: tree ready at %s" % tree)

    if args.make or args.spanish_rom:
        run(["make", "tools", "-j%d" % args.jobs], cwd=tree)
        run(["make", "generated", "-j%d" % args.jobs], cwd=tree)
    if args.spanish_rom:
        run([args.python, ROOT / "tools/localize_spanish.py", "--tree", tree,
             "--rom", args.spanish_rom.resolve()])
    if args.make:
        run(["make", "-C", "3ds_port", "-j%d" % args.jobs, "PYTHON=%s" % args.python], cwd=tree)
    return 0


if __name__ == "__main__":
    sys.exit(main())
