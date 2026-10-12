#!/usr/bin/env python3
"""Reads what a measured run left on the SD card (phase F0 of the engine plan).

perf.bin is written by 3ds_prof.c: a 128-byte header naming the executable,
then one 64-byte record per frame. port.log has the PERF, GPUMEM and SCENARIO
lines. Commands:

  summary RUN.bin [--warmup N]        per mode: percentiles, missed VBlanks, 3D
  compare A.bin B.bin [--warmup N]    the same, two runs side by side
  csv RUN.bin OUT.csv                 every frame, for a spreadsheet
  identity RUN.bin --elf X.elf        whether the run came from that executable
  sites port.log --elf X.elf          GPUMEM sites named by function

The "ready" time is the frame's CPU work up to presenting it, without the wait
for the display (the plan's p95 <= 12 ms target); "gpuEnd" is when the GPU
finished that frame, from the moment it began. A frame shown twice is a missed
VBlank. --warmup drops the first N frames of every stretch of one screen.
"""
import argparse
import bisect
import os
import struct
import subprocess
import sys

HEADER = struct.Struct("<4I40s12s4I8s24s12x")
RECORD = struct.Struct("<2I8B24H")
MAGIC = 0x46503345
TIMINGS = ["ready", "game", "bottom", "vblank", "present", "prep", "wait", "gpu", "gpuStart",
           "gap", "arrive", "palette", "layers", "draw", "frameEnd", "voxel", "voxelAfter",
           "stereoSample", "stereoEyes", "tasks", "sprites", "mix", "load", "profiler"]
MODES = ["other", "field", "voxel", "battle", "voxel-battle", "stage", "centred", "bottom"]
ROUTES = ["mono", "eyes", "planes", "battle-stereo", "voxel", "voxel-stereo", "battle-world",
          "battle-world-stereo", "transition", "blank", "bottom"]
FLATS = ["blend", "mosaic", "bg-front", "slots", "voxel-off", "no-surface", "no-planes", "other"]
# The components worth a column in the summary, in the order a frame spends them.
SHOWN = ["game", "bottom", "tasks", "sprites", "load", "vblank", "prep", "present", "palette",
         "layers", "draw", "frameEnd", "voxel", "voxelAfter", "stereoSample", "stereoEyes", "gpu"]


class Frame:
    __slots__ = ("frame", "scene", "mode", "route", "slider", "stereo", "flat", "missed", "flags", "t",
                 "gpu_end")


def read_run(path):
    with open(path, "rb") as f:
        data = f.read()
    if len(data) < HEADER.size:
        sys.exit(f"{path}: too short for a perf.bin")
    fields = HEADER.unpack_from(data, 0)
    magic, version, size, timings, commit, diff, dirty, low, high, model, appver, linkid = fields
    if magic != MAGIC or size != RECORD.size or timings != len(TIMINGS):
        sys.exit(f"{path}: not a perf.bin this script reads (magic {magic:#x}, record {size}, timings {timings})")
    header = {
        "version": appver.rstrip(b"\0").decode(),
        "commit": commit.rstrip(b"\0").decode(),
        "diff": diff.rstrip(b"\0").decode(),
        "dirty": dirty,
        "code": (high << 32) | low,
        "model": model,
        "linkid": linkid.rstrip(b"\0").decode(errors="replace"),
    }
    frames = []
    for offset in range(HEADER.size, len(data) - RECORD.size + 1, RECORD.size):
        v = RECORD.unpack_from(data, offset)
        fr = Frame()
        fr.frame, fr.scene, fr.mode, fr.route, fr.slider, fr.stereo, fr.flat, fr.missed, fr.flags = v[:9]
        fr.t = [x / 100.0 for x in v[10:]]
        frames.append(fr)
    # The GPU time read at a FrameBegin is the previous frame's.
    for prev, cur in zip(frames, frames[1:]):
        prev.gpu_end = prev.t[TIMINGS.index("gpuStart")] + cur.t[TIMINGS.index("gpu")]
    if frames:
        frames[-1].gpu_end = None
    return header, frames


def stretches(frames):
    """Runs of frames on one screen in one mode."""
    run = []
    for fr in frames:
        if run and (fr.scene != run[-1].scene or fr.mode != run[-1].mode):
            yield run
            run = []
        run.append(fr)
    if run:
        yield run


def percentile(values, fraction):
    if not values:
        return float("nan")
    values = sorted(values)
    index = min(len(values) - 1, max(0, int(-(-len(values) * fraction // 1)) - 1))
    return values[index]


def groups(frames, warmup):
    by_mode = {}
    for run in stretches(frames):
        by_mode.setdefault(run[0].mode, []).extend(run[warmup:])
    return by_mode


def stats(frames):
    ready = [f.t[0] for f in frames]
    gpu_end = [f.gpu_end for f in frames if f.gpu_end is not None]
    out = {
        "frames": len(frames),
        "ready50": percentile(ready, 0.5), "ready95": percentile(ready, 0.95),
        "ready99": percentile(ready, 0.99), "readyMax": max(ready) if ready else float("nan"),
        "gpuEnd95": percentile(gpu_end, 0.95), "gpuEnd99": percentile(gpu_end, 0.99),
        "missed": sum(f.missed for f in frames),
        "missedFrames": sum(1 for f in frames if f.missed),
        "sliderUp": sum(1 for f in frames if f.slider > 2),
        "stereo": sum(1 for f in frames if f.stereo),
        "flat": {name: sum(1 for f in frames if f.flat >> i & 1) for i, name in enumerate(FLATS)},
        "routes": {},
    }
    for f in frames:
        name = ROUTES[f.route] if f.route < len(ROUTES) else str(f.route)
        out["routes"][name] = out["routes"].get(name, 0) + 1
    for name in SHOWN + ["profiler"]:
        i = TIMINGS.index(name)
        out[name] = sum(f.t[i] for f in frames) / len(frames) if frames else float("nan")
    return out


def describe(header):
    models = ["Old 3DS", "Old 3DS XL", "New 3DS", "Old 2DS", "New 3DS XL", "New 2DS XL"]
    model = models[header["model"]] if header["model"] < len(models) else f"model {header['model']}"
    dirty = f", {header['dirty']} files changed (diff {header['diff']})" if header["dirty"] else ""
    return f"{header['version']} {header['commit'][:12]}{dirty}, code {header['code']:016x}, {model}"


def print_summary(path, warmup):
    header, frames = read_run(path)
    print(f"{path}: {describe(header)}, {len(frames)} frames")
    for mode, fs in sorted(groups(frames, warmup).items()):
        if not fs:
            continue
        s = stats(fs)
        print(f"\n[{MODES[mode] if mode < len(MODES) else mode}] {s['frames']} frames"
              f"  ready p50 {s['ready50']:.2f}  p95 {s['ready95']:.2f}  p99 {s['ready99']:.2f}  max {s['readyMax']:.2f} ms"
              f"  | gpuEnd p95 {s['gpuEnd95']:.2f}  p99 {s['gpuEnd99']:.2f}"
              f"  | missed VBlanks {s['missed']} ({s['missedFrames']} frames)")
        print("  means: " + "  ".join(f"{n}={s[n]:.2f}" for n in SHOWN if s[n] >= 0.005)
              + f"  (profiler {s['profiler']:.3f})")
        print(f"  3D: slider up {s['sliderUp']}, shown {s['stereo']}; routes "
              + ", ".join(f"{k} {v}" for k, v in sorted(s["routes"].items(), key=lambda kv: -kv[1]))
              + "; " + ", ".join(f"{k} {v}" for k, v in s["flat"].items() if v))


def print_compare(a, b, warmup):
    ha, fa = read_run(a)
    hb, fb = read_run(b)
    print(f"A {a}: {describe(ha)}\nB {b}: {describe(hb)}")
    ga, gb = groups(fa, warmup), groups(fb, warmup)
    rows = ["frames", "ready50", "ready95", "ready99", "readyMax", "gpuEnd95", "gpuEnd99", "missed"] + SHOWN
    for mode in sorted(set(ga) | set(gb)):
        label = MODES[mode] if mode < len(MODES) else str(mode)
        if not ga.get(mode) or not gb.get(mode):
            print(f"\n[{label}] only in {'B' if gb.get(mode) else 'A'} "
                  f"({len(ga.get(mode) or gb.get(mode))} frames)")
            continue
        sa = stats(ga[mode])
        sb = stats(gb[mode])
        print(f"\n[{label}]{'':{max(0, 16 - len(label))}}{'A':>10}{'B':>10}{'B-A':>10}")
        for row in rows:
            va, vb = sa[row], sb[row]
            if isinstance(va, float) and va < 0.005 and isinstance(vb, float) and vb < 0.005:
                continue
            print(f"  {row:16}{va:10.2f}{vb:10.2f}{vb - va:+10.2f}")


def write_csv(path, out):
    _, frames = read_run(path)
    with open(out, "w", encoding="utf-8") as f:
        f.write("frame,scene,mode,route,slider,stereo,flat,missed,flags," + ",".join(TIMINGS) + ",gpuEnd\n")
        for fr in frames:
            f.write(f"{fr.frame},{fr.scene:08x},{MODES[fr.mode] if fr.mode < len(MODES) else fr.mode},"
                    f"{ROUTES[fr.route] if fr.route < len(ROUTES) else fr.route},{fr.slider},{fr.stereo},"
                    f"{fr.flat},{fr.missed},{fr.flags}," + ",".join(f"{t:.2f}" for t in fr.t)
                    + f",{'' if fr.gpu_end is None else f'{fr.gpu_end:.2f}'}\n")


def elf_text(path):
    with open(path, "rb") as f:
        data = f.read()
    if data[:4] != b"\x7fELF" or data[4] != 1:
        sys.exit(f"{path}: not a 32-bit ELF")
    shoff, = struct.unpack_from("<I", data, 0x20)
    shentsize, shnum, shstrndx = struct.unpack_from("<3H", data, 0x2E)
    sections = [struct.unpack_from("<10I", data, shoff + i * shentsize) for i in range(shnum)]
    names = sections[shstrndx]
    for s in sections:
        name = data[names[4] + s[0]:data.index(b"\0", names[4] + s[0])].decode()
        if name == ".text":
            return data[s[4]:s[4] + s[5]]
    sys.exit(f"{path}: no .text")


def code_hash(text):
    h = 0xCBF29CE484222325
    for (word,) in struct.iter_unpack("<I", text[:len(text) // 4 * 4]):
        h = ((h ^ word) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


def identity(path, elf):
    header, frames = read_run(path)
    print(f"{path}: {describe(header)}")
    # The link id, when the run has one: the code hash only matches the ELF
    # where the loader lays it out as linked (Azahar, not the console).
    if header["linkid"]:
        with open(elf, "rb") as f:
            same = header["linkid"].encode() in f.read()
        print(f"link id {header['linkid']}: {'same executable' if same else 'DIFFERENT executable'}")
        return 0 if same else 1
    h = code_hash(elf_text(elf))
    print(f"{elf}: code {h:016x}")
    print("same executable" if h == header["code"] else "DIFFERENT executable")
    return 0 if h == header["code"] else 1


def nm_symbols(elf):
    nm = os.environ.get("NM", "arm-none-eabi-nm")
    out = subprocess.run([nm, "-n", elf], capture_output=True, text=True, check=True).stdout
    addrs, names = [], []
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[1] in "tTwW":
            addrs.append(int(parts[0], 16))
            names.append(parts[2])
    return addrs, names


def sites(log, elf):
    addrs, names = nm_symbols(elf)

    def name(address):
        i = bisect.bisect_right(addrs, address) - 1
        return f"{names[i]}+{address - addrs[i]:#x}" if i >= 0 else "?"

    with open(log, encoding="utf-8", errors="replace") as f:
        for line in f:
            if "GPUMEM" not in line:
                continue
            line = line.rstrip("\n")
            if "GPUMEM sites" in line or "GPUMEM fail" in line:
                out = []
                for word in line.split():
                    if ":" in word and "=" in word and word[0] in "vl" and not word.startswith("live"):
                        pool, rest = word.split(":", 1)
                        address, value = rest.split("=", 1)
                        out.append(f"{'vram  ' if pool == 'v' else 'linear'} {value:>14}  {name(int(address, 16))}")
                    elif word.startswith("site="):
                        out.append(f"site {name(int(word[5:], 16))}")
                    elif out and word.startswith("x") and word[1:].isdigit() and "fail" not in line:
                        out[-1] = f"{out[-1]} ({word})"
                    else:
                        out.append(word)
                if "fail" in line:
                    print(" ".join(out))
                else:
                    print(out[0] + " " + out[1] + "\n    " + "\n    ".join(out[2:]))
            else:
                print(line)


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="command", required=True)
    s = sub.add_parser("summary"); s.add_argument("run"); s.add_argument("--warmup", type=int, default=60)
    c = sub.add_parser("compare"); c.add_argument("a"); c.add_argument("b"); c.add_argument("--warmup", type=int, default=60)
    v = sub.add_parser("csv"); v.add_argument("run"); v.add_argument("out")
    i = sub.add_parser("identity"); i.add_argument("run"); i.add_argument("--elf", required=True)
    g = sub.add_parser("sites"); g.add_argument("log"); g.add_argument("--elf", required=True)
    a = p.parse_args()
    if a.command == "summary":
        print_summary(a.run, a.warmup)
    elif a.command == "compare":
        print_compare(a.a, a.b, a.warmup)
    elif a.command == "csv":
        write_csv(a.run, a.out)
    elif a.command == "identity":
        return identity(a.run, a.elf)
    elif a.command == "sites":
        sites(a.log, a.elf)
    return 0


if __name__ == "__main__":
    sys.exit(main())
