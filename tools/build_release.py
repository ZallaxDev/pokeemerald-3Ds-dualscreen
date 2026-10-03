#!/usr/bin/env python3
"""Assemble a Pokémon Emerald 3Ds Dual Screen release.

    python tools/build_release.py --version 0.1.0 --rom baserom.gba \\
        --gba-elf path/to/pokeemerald.elf

Steps (each can be skipped when its output is already there):

1. `make release` in 3ds_port: dist/Emerald3DS.3dsx and .smdh, engine files only;
2. the recipe (tools/gen_recipe.py) from the build's staging and the ROM;
3. the payload: executable, recipe and the voxel generator scripts;
4. the standalone builder (PyInstaller, one folder, no UPX);
5. dist/Emerald3DS-v<version>-Windows.zip with the builder, the payload,
   README.txt and LICENSES/;
6. the standalone dist/Emerald3DS.3dsx and dist/Emerald3DS.smdh (quick update
   of the executable when the data ABI did not change);
7. dist/Emerald3DS-WebPayload.zip and dist/web-manifest.json for the web
   builder (tools/build_web_payload.py);
8. tools/release_audit.py over every ZIP (and, with --rom, a scan for any run
   of the ROM's bytes), then SHA256SUMS.txt over every release asset.

Attach everything in dist/ listed in SHA256SUMS.txt, and SHA256SUMS.txt itself,
to the GitHub release; see docs/RELEASING.md.

The ROM is read to write the recipe and to audit; it is never copied into the
release. The release never contains a data pack.
"""

from __future__ import annotations

import argparse
import ast
import hashlib
import os
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path

import build_web_payload

ROOT = Path(__file__).resolve().parents[1]
PORT = ROOT / "3ds_port"
DIST = ROOT / "dist"
GENERATORS = ["gen_voxel_regions.py", "gen_voxel_sign_masks.py",
              "gen_voxel_relief.py", "gen_voxel_buildings.py", "gen_intro_margins.py"]
VOXELGEN_FILES = ["src/voxel/voxel_regions.h"]


def run(cmd, cwd=None, env=None):
    print("+ " + " ".join(str(c) for c in cmd), flush=True)
    subprocess.run([str(c) for c in cmd], cwd=cwd, env=env, check=True)


def local_closure(scripts_dir: Path, names: list[str]) -> tuple[list[str], set[str]]:
    """The generator scripts plus every sibling module they import, and the
    external modules they need bundled."""
    todo = [n for n in names if (scripts_dir / n).exists()]
    seen, external = set(), set()
    while todo:
        name = todo.pop()
        if name in seen:
            continue
        seen.add(name)
        tree = ast.parse((scripts_dir / name).read_text(encoding="utf-8"))
        for node in ast.walk(tree):
            mods = []
            if isinstance(node, ast.Import):
                mods = [a.name for a in node.names]
            elif isinstance(node, ast.ImportFrom) and node.module and node.level == 0:
                mods = [node.module]
            for mod in mods:
                top = mod.split(".")[0]
                if (scripts_dir / (top + ".py")).exists():
                    todo.append(top + ".py")
                else:
                    external.add(mod)
    return sorted(seen), external


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--version", required=True)
    ap.add_argument("--rom", type=Path, required=True)
    ap.add_argument("--gba-elf", type=Path, required=True, help="the original game's ELF (symbol names)")
    ap.add_argument("--nm", default=os.environ.get("NM", "arm-none-eabi-nm"))
    ap.add_argument("--make", default=None, help="command that runs make in 3ds_port (default: make)")
    ap.add_argument("--skip-make", action="store_true")
    ap.add_argument("--skip-recipe", action="store_true")
    ap.add_argument("--skip-exe", action="store_true")
    args = ap.parse_args()

    tag = "v" + args.version
    release = DIST / ("Emerald3DS-%s-Windows" % tag)
    payload = release / "payload"

    if not args.skip_make:
        run((args.make.split() if args.make else ["make"]) + ["release"], cwd=PORT)
    DIST.mkdir(parents=True, exist_ok=True)
    recipe = DIST / "emerald3ds.recipe"
    if not args.skip_recipe:
        run([sys.executable, ROOT / "tools/gen_recipe.py", "--romfs", PORT / "romfs", "--rom", args.rom,
             "--out", recipe, "--release", tag, "--elf", PORT / "emerald3ds.elf",
             "--gba-elf", args.gba_elf, "--image-elf", PORT / "build/gamedata_image.elf",
             "--image-map", PORT / "build/gamedata_image.map", "--nm", args.nm,
             "--report", DIST / "recipe-literal-report.txt"])

    if release.exists():
        shutil.rmtree(release)
    payload.mkdir(parents=True)
    for name in ("Emerald3DS.3dsx", "Emerald3DS.smdh"):
        shutil.copy2(PORT / "dist" / name, payload / name)
    shutil.copy2(recipe, payload / "emerald3ds.recipe")
    scripts, external = local_closure(PORT / "scripts", GENERATORS)
    (payload / "voxelgen" / "scripts").mkdir(parents=True)
    for name in scripts:
        shutil.copy2(PORT / "scripts" / name, payload / "voxelgen" / "scripts" / name)
    for rel in VOXELGEN_FILES:
        dst = payload / "voxelgen" / rel
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(PORT / rel, dst)

    if not args.skip_exe:
        build = ROOT / "builder" / "build"
        build.mkdir(parents=True, exist_ok=True)
        (build / "hidden-imports.txt").write_text("\n".join(sorted(external)) + "\n", encoding="utf-8")
        run([sys.executable, "-m", "PyInstaller", "--noconfirm", "--clean",
             "--distpath", build / "dist", "--workpath", build / "pyi", "emerald3ds-builder.spec"],
            cwd=ROOT / "builder")
        shutil.copytree(build / "dist" / "Emerald3DS-Builder", release, dirs_exist_ok=True)

    shutil.copy2(ROOT / "builder" / "README-release.txt", release / "README.txt")
    licenses = release / "LICENSES"
    licenses.mkdir()
    for rel in ("LICENSE-PORT.md", "NOTICE.md", "AI_DISCLOSURE.md"):
        src = ROOT / rel
        if src.exists():
            shutil.copy2(src, licenses / rel)
    for rel in ("3ds_port/src/voxel/NOTICE.md",):
        shutil.copy2(ROOT / rel, licenses / "voxel-NOTICE.md")

    archive = DIST / ("Emerald3DS-%s-Windows.zip" % tag)
    if archive.exists():
        archive.unlink()
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as zf:
        for path in sorted(release.rglob("*")):
            if path.is_file():
                zf.write(path, path.relative_to(DIST).as_posix())

    standalone = []
    for name in ("Emerald3DS.3dsx", "Emerald3DS.smdh"):
        shutil.copy2(payload / name, DIST / name)
        standalone.append(DIST / name)
    web_zip, web_manifest = build_web_payload.build(payload, args.version, DIST)

    run([sys.executable, ROOT / "tools/release_audit.py", "--zip", archive, "--strict", "--rom", args.rom])
    run([sys.executable, ROOT / "tools/release_audit.py", "--zip", web_zip, "--strict", "--rom", args.rom,
         "--web-payload"])
    sums = DIST / "SHA256SUMS.txt"
    assets = [archive] + standalone + [web_zip, web_manifest]
    lines = ["%s  %s" % (sha256(path), path.name) for path in assets]
    for path in sorted(payload.glob("*")):
        if path.is_file():
            lines.append("%s  payload/%s" % (sha256(path), path.name))
    sums.write_text("\n".join(lines) + "\n", encoding="ascii")
    for path in assets:
        print("release asset: %s (%.1f MiB)" % (path, path.stat().st_size / 1048576))
    print("release asset: %s" % sums)


if __name__ == "__main__":
    main()
