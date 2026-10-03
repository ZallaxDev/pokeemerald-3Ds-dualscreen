#!/usr/bin/env python3
"""Package a release's payload for the web builder.

    python tools/build_web_payload.py --payload dist/Emerald3DS-v0.1.3-Windows/payload \\
        --version 0.1.3 --out dist

Writes dist/Emerald3DS-WebPayload.zip and dist/web-manifest.json (the same
manifest as inside the ZIP, attached to the release on its own so the website
can read it without downloading the payload). tools/build_release.py calls this
for every release; it can also be run on the payload/ folder of an already
published Windows ZIP.

The contents and the manifest are described in
builder/emerald3ds_builder/webmanifest.py. The ZIP is deterministic: sorted
entries and fixed timestamps, so the same inputs give the same bytes.

    python tools/build_web_payload.py --synthetic OUT

writes a synthetic payload and a synthetic "ROM" for exercising the web
builder without any game data (see make_synthetic below).
"""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
import zipfile
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "builder"))

from emerald3ds_builder import __version__ as BUILDER_VERSION, pak  # noqa: E402
from emerald3ds_builder.recipe import Recipe  # noqa: E402
from emerald3ds_builder.rom import KNOWN_CODES, ROM_SIZE  # noqa: E402
from emerald3ds_builder.webmanifest import (ENTRYPOINT, INSTALL_DIR, INSTALL_FILES, KIND,  # noqa: E402
                                            MANIFEST_NAME, PAYLOAD_NAME, SCHEMA_VERSION, validate)

PACKAGE = ROOT / "builder" / "emerald3ds_builder"
# The desktop-only modules are left out: the browser has no window and no SD card.
PACKAGE_EXCLUDE = {"gui.py", "install.py", "cli.py", "__main__.py"}
LICENSES = [("LICENSE-PORT.md", "LICENSE-PORT.md"), ("NOTICE.md", "NOTICE.md"),
            ("AI_DISCLOSURE.md", "AI_DISCLOSURE.md"), ("3ds_port/src/voxel/NOTICE.md", "voxel-NOTICE.md")]
FIXED_TIME = (2020, 1, 1, 0, 0, 0)
ROM_REGIONS = {"BPEE": ("USA, Europe", "en")}


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def collect(payload: Path, package: Path = PACKAGE, licenses_root: Path = ROOT) -> dict[str, bytes]:
    """Every file of the web payload except the manifest: {zip path: bytes}."""
    files: dict[str, bytes] = {}
    for name in ("Emerald3DS.3dsx", "Emerald3DS.smdh", "emerald3ds.recipe"):
        src = payload / name
        if not src.is_file():
            raise SystemExit("build_web_payload: %s is missing" % src)
        files["payload/" + name] = src.read_bytes()
    voxelgen = payload / "voxelgen"
    if voxelgen.is_dir():
        for path in sorted(voxelgen.rglob("*")):
            if path.is_file() and "__pycache__" not in path.parts and path.suffix not in (".pyc", ".pyo"):
                files["payload/voxelgen/" + path.relative_to(voxelgen).as_posix()] = path.read_bytes()
    for path in sorted(package.glob("*.py")):
        if path.name not in PACKAGE_EXCLUDE:
            files["python/emerald3ds_builder/" + path.name] = path.read_bytes()
    for src, dst in LICENSES:
        path = licenses_root / src
        if path.is_file():
            files["licenses/" + dst] = path.read_bytes()
    return files


def make_manifest(files: dict[str, bytes], version: str, tag: str, cia_forwarder: str | None = None,
                  synthetic: bool = False) -> dict:
    recipe = Recipe.from_bytes(files["payload/emerald3ds.recipe"])
    code = "BPEE"
    region, language = ROM_REGIONS.get(code, ("", ""))

    def ref(path: str, release_asset: str | None = None) -> dict:
        out = {"path": path, "sha256": sha256(files[path]), "size": len(files[path])}
        if release_asset is not None:
            out["releaseAsset"] = release_asset
        return out

    manifest = {
        "schemaVersion": SCHEMA_VERSION,
        "kind": KIND,
        "releaseVersion": version,
        "releaseTag": tag,
        "recipeRelease": recipe.release,
        "builderVersion": BUILDER_VERSION,
        "packSchema": pak.SCHEMA,
        "dataAbi": "%08x" % recipe.engine_abi,
        "supportedRoms": [{"sha1": recipe.rom_sha1, "name": KNOWN_CODES[code] if not synthetic else
                           "Synthetic test ROM (not a game)", "code": code if not synthetic else "TEST",
                           "region": region, "language": language}],
        "pythonRuntime": {"minimumVersion": "3.11", "packages": ["pillow"], "stdlib": ["lzma", "zlib"]},
        "pyodideCompatibility": {"inprocessGenerators": True, "subprocess": False},
        "entrypoint": dict(ENTRYPOINT),
        "install": {"directory": INSTALL_DIR, "files": list(INSTALL_FILES)},
        "assets": {
            "threeDsx": ref("payload/Emerald3DS.3dsx", "Emerald3DS.3dsx"),
            "smdh": ref("payload/Emerald3DS.smdh", "Emerald3DS.smdh"),
            "recipe": ref("payload/emerald3ds.recipe"),
            "ciaForwarder": ({"releaseAsset": cia_forwarder,
                              "target": "sdmc:/3ds/emerald3ds/Emerald3DS.3dsx"} if cia_forwarder else None),
        },
        "files": [{"path": p, "sha256": sha256(d), "size": len(d)} for p, d in sorted(files.items())],
    }
    if synthetic:
        manifest["synthetic"] = True
    return validate(manifest)


def write_zip(out: Path, manifest: dict, files: dict[str, bytes]) -> None:
    out.parent.mkdir(parents=True, exist_ok=True)
    entries = dict(files)
    entries[MANIFEST_NAME] = (json.dumps(manifest, indent=2, sort_keys=True) + "\n").encode("utf-8")
    tmp = out.with_name(out.name + ".tmp")
    with zipfile.ZipFile(tmp, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as zf:
        for name in sorted(entries):
            info = zipfile.ZipInfo(name, FIXED_TIME)
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o644 << 16
            zf.writestr(info, entries[name])
    tmp.replace(out)


def build(payload: Path, version: str, out_dir: Path, cia_forwarder: str | None = None) -> tuple[Path, Path]:
    files = collect(payload)
    manifest = make_manifest(files, version, "v" + version, cia_forwarder)
    archive = out_dir / PAYLOAD_NAME
    write_zip(archive, manifest, files)
    manifest_path = out_dir / MANIFEST_NAME
    manifest_path.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return archive, manifest_path


SYNTHETIC_GENERATOR = '''"""Synthetic stand-in for gen_intro_margins.py (web builder tests only).

It keeps module state on purpose: a runner that leaks modules between runs
would see CALLS > 1."""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import synthetic_state  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--output", required=True)
    args = ap.parse_args()
    synthetic_state.CALLS += 1
    if synthetic_state.CALLS != 1:
        raise SystemExit("state leaked between runs")
    with open(os.path.join(ROOT, "data", "synthetic", "input.bin"), "rb") as f:
        data = f.read()
    os.makedirs(os.path.dirname(args.output), exist_ok=True)
    with open(args.output, "wb") as f:
        f.write(bytes(reversed(data)) * 4)


if __name__ == "__main__":
    main()
'''


def make_synthetic(out_dir: Path) -> dict[str, Path]:
    """A complete synthetic web payload, built through the real recipe, pack and
    manifest code: a fake 64 KiB "ROM" (code TEST, no game data), a recipe with
    one file of every operation kind, one in-process generator, and placeholder
    executables. Only for tests and local development of the web builder."""
    out_dir.mkdir(parents=True, exist_ok=True)
    rom = bytearray((i * 7 + 3) & 0xFF for i in range(64 * 1024))
    rom[0xA0:0xB0] = b"SYNTHETIC\0\0\0TEST"
    padded = bytes(rom) + b"\xff" * (ROM_SIZE - len(rom))
    rom_sha1 = hashlib.sha1(padded).hexdigest()
    gen_input = bytes(rom[0x1000:0x1100])
    gen_output = bytes(reversed(gen_input)) * 4

    def entry(path: str, data: bytes, ops: list) -> dict:
        return {"path": path, "size": len(data), "crc": zlib.crc32(data) & 0xFFFFFFFF, "ops": ops}

    entries = [entry("data/synthetic_copy.bin", bytes(rom[0x200:0x600]), [["C", 0x200, 0x400]]),
               entry("data/synthetic_fill.bin", b"\x5a" * 512, [["F", 0x5A, 512]])]
    generated = [{"path": "stage/leaves.bin", "size": len(gen_output),
                  "crc": zlib.crc32(gen_output) & 0xFFFFFFFF, "generator": "gen_intro_margins.py"}]
    inputs = [entry("data/synthetic/input.bin", gen_input, [["C", 0x1000, 0x100]])]
    items = [(e["path"], e["size"], e["crc"]) for e in entries + generated]
    vtree = {"tilesets": {}, "layouts_table_label": "gMapLayouts", "layouts": [], "maps": [],
             "map_types": {}, "connection_directions": {}, "metatiles_h": [], "headers_h": [],
             "metatile_behaviors": [["MB_NORMAL", 0]]}
    recipe = Recipe(engine_abi=pak.engine_abi(items), rom_sha1=rom_sha1, release="v0.0.0-synthetic",
                    entries=entries, generated=generated, inputs=inputs, vtree=vtree)

    payload = out_dir / "payload"
    (payload / "voxelgen" / "scripts").mkdir(parents=True, exist_ok=True)
    (payload / "Emerald3DS.3dsx").write_bytes(b"3DSX" + b"SYNTHETIC EXECUTABLE PLACEHOLDER\n")
    (payload / "Emerald3DS.smdh").write_bytes(b"SMDH" + b"SYNTHETIC ICON PLACEHOLDER\n")
    recipe.save(payload / "emerald3ds.recipe")
    (payload / "voxelgen" / "scripts" / "gen_intro_margins.py").write_text(SYNTHETIC_GENERATOR, encoding="utf-8")
    (payload / "voxelgen" / "scripts" / "synthetic_state.py").write_text("CALLS = 0\n", encoding="utf-8")

    files = collect(payload)
    manifest = make_manifest(files, "0.0.0-synthetic", "v0.0.0-synthetic", synthetic=True)
    archive = out_dir / PAYLOAD_NAME
    write_zip(archive, manifest, files)
    (out_dir / MANIFEST_NAME).write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    rom_path = out_dir / "synthetic-rom.bin"
    rom_path.write_bytes(bytes(rom))
    return {"payload": payload, "zip": archive, "manifest": out_dir / MANIFEST_NAME, "rom": rom_path}


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--payload", type=Path, help="a release's payload/ folder")
    ap.add_argument("--version", help="release version without the v, e.g. 0.1.3")
    ap.add_argument("--out", type=Path, default=ROOT / "dist")
    ap.add_argument("--cia-forwarder", default=None, help="release asset name of the CIA forwarder, if any")
    ap.add_argument("--synthetic", type=Path, help="write a synthetic test payload into this folder instead")
    args = ap.parse_args()
    if args.synthetic:
        paths = make_synthetic(args.synthetic)
        for key, path in paths.items():
            print("synthetic %s: %s" % (key, path))
        return
    if not args.payload or not args.version:
        ap.error("--payload and --version are required (or --synthetic)")
    archive, manifest = build(args.payload, args.version, args.out, args.cia_forwarder)
    print("web payload: %s (%.1f MiB)" % (archive, archive.stat().st_size / 1048576))
    print("web manifest: %s" % manifest)


if __name__ == "__main__":
    main()
