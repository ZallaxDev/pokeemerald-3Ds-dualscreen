#!/usr/bin/env python3

"""Build the external-asset index for a port whose INCBINs are link-time stubs.

Every INCBIN in the game tree stays in the executable as a stub symbol; this
script maps each stub address the linker produced to the file that holds its
bytes, copies (or concatenates) those files into the data directory, and writes
the sorted index the runtime resolver searches (3ds_port/src/3ds_assets.c).
Grouped INCBINs also get `base + offset` rows so a pointer to any member
resolves.
"""

from __future__ import annotations

import argparse
import os
import re
import shutil
import struct
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
PORT_DIR = ROOT / "3ds_port"
# Optional: a separate decomp build tree to take generated assets from when the
# main tree has not generated them.
BUILD_EMERALD = ROOT / "build-emerald"
MAP_FILE = PORT_DIR / "build" / "emerald3ds.map"
FS_DIR = PORT_DIR / "romfs"
URI_PREFIX = ""
OUT_MAP = FS_DIR / "assets" / "asset_map.txt"
OUT_PTR_MAP = FS_DIR / "assets" / "asset_ptr_map.txt"
MAKE_ASSET_TIMEOUT_SECONDS = 120
GENERATE_ON_DEMAND_PREFIXES = (
    "graphics/",
    "data/tilesets/",
)


def parse_incbins_by_source() -> list[tuple[Path, str, list[str], bool]]:
    # `static` must be part of the match: several files declare a private symbol
    # with the same name (sPokeball_Gfx, sCursor_Gfx, ...). Without it they look
    # like one external symbol, and when the linker keeps only the object that is
    # actually used, every one of them is mapped to that survivor's address --
    # four different files then claim the same asset.
    direct_pattern = re.compile(
        r"((?:static\s+)?const\s+[^;=]+?\s+([A-Za-z_][A-Za-z0-9_]*)\s*(?:\[[^\]]*\]\s*)*=\s*INCBIN_U(?:8|16|32)\s*\((.*?)\)\s*;)",
        re.S,
    )
    grouped_pattern = re.compile(
        r"((?:static\s+)?const\s+[^;=]+?\s+([A-Za-z_][A-Za-z0-9_]*)\s*(?:\[[^\]]*\]\s*)*=\s*\{(.*?)\};)",
        re.S,
    )
    path_pattern = re.compile(r'"([^"]+)"')

    result: list[tuple[Path, str, list[str], bool]] = []
    src_root = ROOT / "src"
    files = sorted(list(src_root.rglob("*.c")) + list(src_root.rglob("*.h")))
    for src_path in files:
        text = src_path.read_text(encoding="utf-8", errors="ignore")
        seen: set[str] = set()

        for m in direct_pattern.finditer(text):
            decl = m.group(1)
            sym = m.group(2)
            paths = path_pattern.findall(m.group(3))
            if paths:
                is_static = "static" in decl.split("=")[0]
                result.append((src_path.relative_to(ROOT), sym, paths, is_static))
                seen.add(sym)

        for m in grouped_pattern.finditer(text):
            decl = m.group(1)
            sym = m.group(2)
            init = m.group(3)
            paths = path_pattern.findall(init)

            if sym in seen:
                continue
            if not paths or "INCBIN_" not in init:
                continue

            is_static = "static" in decl.split("=")[0]
            result.append((src_path.relative_to(ROOT), sym, paths, is_static))
            seen.add(sym)

    return result


def parse_symbol_addresses_from_map() -> tuple[dict[tuple[str, str], int], dict[str, list[tuple[str, int]]]]:
    lines = MAP_FILE.read_text(encoding="utf-8", errors="ignore").splitlines()
    symbols: dict[tuple[str, str], int] = {}
    symbols_by_name: dict[str, list[tuple[str, int]]] = {}

    # Skip the "Discarded input sections" block to avoid 0x00000000 entries.
    start = 0
    for idx, line in enumerate(lines):
        if line.strip() == "Linker script and memory map":
            start = idx + 1
            break

    sec_pat = re.compile(r"^\s*\.rodata\.([A-Za-z_][A-Za-z0-9_]*)\s*$")
    addr_pat = re.compile(r"^\s*0x([0-9a-fA-F]+)\s+0x[0-9a-fA-F]+\s+(build/root/\S+\.o)\s*$")

    i = start
    while i < len(lines):
        m_sec = sec_pat.match(lines[i])
        if not m_sec:
            i += 1
            continue

        if i + 1 >= len(lines):
            break

        m_addr = addr_pat.match(lines[i + 1])
        if not m_addr:
            i += 1
            continue

        addr = int(m_addr.group(1), 16)
        obj = m_addr.group(2).replace("\\", "/")
        sym = m_sec.group(1)

        if addr != 0:
            symbols[(obj, sym)] = addr
            symbols_by_name.setdefault(sym, []).append((obj, addr))

        i += 2

    return symbols, symbols_by_name


def generate_missing(incbins) -> None:
    """Generate every missing asset with one parallel make, not one per file."""
    missing = sorted({rel for _, _, paths, _ in incbins for rel in paths
                      if rel.startswith(GENERATE_ON_DEMAND_PREFIXES) and not (ROOT / rel).exists()
                      and not (BUILD_EMERALD / rel).exists()})
    if not missing:
        return
    print(f"gen_asset_table: generating {len(missing)} missing assets", flush=True)
    jobs = str(max(2, (os.cpu_count() or 2)))
    for start in range(0, len(missing), 400):
        subprocess.run([os.environ.get("EMERALD3DS_MAKE", "make"), "-k", "-j" + jobs] + missing[start:start + 400], cwd=ROOT, check=False)


def ensure_asset(rel: str) -> Path | None:
    src = ROOT / rel
    if src.exists():
        return src

    if rel.startswith(GENERATE_ON_DEMAND_PREFIXES):
        print(f"gen_asset_table: generating {rel}", flush=True)
        try:
            subprocess.run([os.environ.get("EMERALD3DS_MAKE", "make"), rel], cwd=ROOT, check=False, timeout=MAKE_ASSET_TIMEOUT_SECONDS)
        except subprocess.TimeoutExpired:
            print(f"gen_asset_table: timeout generating {rel}", flush=True)
        if src.exists():
            return src

    fallback = BUILD_EMERALD / rel
    if fallback.exists():
        return fallback

    return None


def copy_or_concat_asset(obj_rel: str, symbol: str, rel_paths: list[str]) -> tuple[str, int, list[int]] | None:
    if len(rel_paths) == 1:
        rel = rel_paths[0]
        src = ensure_asset(rel)
        if src is None:
            return None
        dst = FS_DIR / rel
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, dst)
        return (URI_PREFIX + rel.replace('\\', '/'), src.stat().st_size, [0])

    obj_tag = obj_rel.replace("/", "_").replace(".o", "")
    out_rel = Path("generated") / "assets" / f"{obj_tag}__{symbol}.bin"
    out_abs = FS_DIR / out_rel
    out_abs.parent.mkdir(parents=True, exist_ok=True)

    total_size = 0
    offsets: list[int] = []
    with out_abs.open("wb") as fout:
        for rel in rel_paths:
            src = ensure_asset(rel)
            if src is None:
                return None
            data = src.read_bytes()
            offsets.append(total_size)
            fout.write(data)
            total_size += len(data)

    return (URI_PREFIX + str(out_rel).replace('\\', '/'), total_size, offsets)


def configure(argv: list[str] | None = None) -> None:
    global ROOT, BUILD_EMERALD, PORT_DIR, MAP_FILE, FS_DIR, URI_PREFIX, OUT_MAP, OUT_PTR_MAP

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port-dir", default=str(PORT_DIR))
    parser.add_argument("--map", default=None, help="linker map of the port executable")
    parser.add_argument("--fs-dir", default=None, help="root of the packaged filesystem")
    parser.add_argument("--uri-prefix", default=URI_PREFIX, help="prefix written before every path")
    args = parser.parse_args(argv)

    PORT_DIR = Path(args.port_dir).resolve()
    ROOT = PORT_DIR.parent
    BUILD_EMERALD = ROOT / "build-emerald"
    MAP_FILE = Path(args.map).resolve() if args.map else PORT_DIR / "build" / "emerald3ds.map"
    FS_DIR = Path(args.fs_dir).resolve() if args.fs_dir else PORT_DIR / "romfs"
    URI_PREFIX = args.uri_prefix
    OUT_MAP = FS_DIR / "assets" / "asset_map.txt"
    OUT_PTR_MAP = FS_DIR / "assets" / "asset_ptr_map.txt"


def main() -> None:
    if not MAP_FILE.exists():
        raise SystemExit(f"Missing map file: {MAP_FILE}")

    incbins = parse_incbins_by_source()
    generate_missing(incbins)
    sym_addrs, sym_addrs_by_name = parse_symbol_addresses_from_map()

    entries: list[tuple[int, int, str]] = []
    ptr_entries: list[tuple[int, int, int]] = []
    missing_assets: list[str] = []
    # Symbols that a source file's own INCBIN already accounts for.
    claimed = {(f"build/root/{src_rel.with_suffix('.o').as_posix()}", sym)
               for src_rel, sym, _, _ in incbins if src_rel.suffix == ".c"}

    for index, (src_rel, sym, rel_paths, is_static) in enumerate(incbins, start=1):
        if (index % 100) == 0:
            print(f"gen_asset_table: processed {index}/{len(incbins)} incbins", flush=True)

        obj_rel = f"build/root/{src_rel.with_suffix('.o').as_posix()}"
        addr = sym_addrs.get((obj_rel, sym))
        if addr is None:
            candidates = sym_addrs_by_name.get(sym, [])
            # A header has no object of its own: its INCBINs, static ones
            # included (src/data/wallpapers.h), are in the file that includes it.
            if src_rel.suffix == ".h":
                candidates = [c for c in candidates if (c[0], sym) not in claimed]
            if len(candidates) == 1 and (not is_static or src_rel.suffix == ".h"):
                obj_rel, addr = candidates[0]
            else:
                continue

        asset_info = copy_or_concat_asset(obj_rel, sym, rel_paths)
        if asset_info is None:
            missing_assets.append(f"{src_rel}:{sym}")
            continue

        fs_path, byte_size, member_offsets = asset_info
        entries.append((addr, byte_size, fs_path))
        if len(member_offsets) > 1:
            for offset in member_offsets:
                ptr_entries.append((addr + offset, addr, offset))

    # Sprite descriptors now retain base and offset separately. Synthetic frame
    # addresses can alias unrelated constants and must not enter the pointer map.

    entries.sort(key=lambda x: x[0])
    ptr_entries.sort(key=lambda x: (x[0], x[1], x[2]))
    OUT_MAP.parent.mkdir(parents=True, exist_ok=True)
    with OUT_MAP.open("wb") as f, OUT_MAP.with_name("asset_index.bin").open("wb") as index:
        f.write(b"# addr size path\n")
        for addr, size, path in entries:
            prefix = f"{addr:08X} {size:08X} ".encode("ascii")
            index.write(struct.pack("<III", addr, size, f.tell() + len(prefix)))
            f.write(prefix + path.encode("ascii") + b"\n")

    with OUT_PTR_MAP.open("w", encoding="ascii") as f, OUT_PTR_MAP.with_name("asset_ptr_index.bin").open("wb") as index:
        f.write("# ptr base offset\n")
        last = None
        for ptr, base, offset in ptr_entries:
            row = (ptr, base, offset)
            if row == last:
                continue
            f.write(f"{ptr:08X} {base:08X} {offset:08X}\n")
            index.write(struct.pack("<III", ptr, base, offset))
            last = row

    print(f"gen_asset_table: wrote {len(entries)} entries to {OUT_MAP}")
    print(f"gen_asset_table: wrote {len(ptr_entries)} pointer entries to {OUT_PTR_MAP}")
    if missing_assets:
        print(f"gen_asset_table: missing assets for {len(missing_assets)} symbols")
        for name in missing_assets[:20]:
            print(f"  - {name}")


if __name__ == "__main__":
    configure()
    main()
