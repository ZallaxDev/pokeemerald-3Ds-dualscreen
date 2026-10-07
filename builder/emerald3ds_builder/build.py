"""ROM -> data pack: the whole build, independent of any user interface."""

from __future__ import annotations

import shutil
import sys
import tempfile
import time
import zlib
from dataclasses import dataclass
from pathlib import Path

from . import pak
from .errors import BuilderError
from .recipe import Recipe, RecipeError, build_entry, recipe_rom_sha1
from .rom import Rom, load_rom
from .voxel import run_generators
from .vtree import build_tree

RECIPE_NAME = "emerald3ds.recipe"
EXECUTABLE_NAMES = ("Emerald3DS.3dsx", "Emerald3DS.smdh")


def default_payload() -> Path:
    """payload/ next to the executable (release) or next to the package."""
    if getattr(sys, "frozen", False):
        return Path(sys.executable).resolve().parent / "payload"
    return Path(__file__).resolve().parent.parent / "payload"


@dataclass
class Payload:
    """A release's payload/ folder.

    payload/ holds the default variant (the English game: executable and
    recipe) and the voxel generators every variant shares. Each other
    language is a subfolder with its own executable and recipe, e.g.
    payload/es/ for the Spanish ROM; `for_rom` picks the one made from the
    player's ROM."""
    root: Path
    shared: Path | None = None   # the payload/ folder, for a language subfolder

    @property
    def recipe(self) -> Path:
        return self.root / RECIPE_NAME

    @property
    def voxelgen(self) -> Path:
        return (self.shared or self.root) / "voxelgen"

    def variants(self) -> list["Payload"]:
        """The default variant first, then every language subfolder."""
        base = self.shared or self.root
        if not base.is_dir():
            return [Payload(base)]
        subs = sorted(p for p in base.iterdir() if p.is_dir() and (p / RECIPE_NAME).is_file())
        return [Payload(base)] + [Payload(p, base) for p in subs]

    def for_rom(self, sha1: str) -> "Payload":
        """The variant whose recipe was made from this ROM (self when none is)."""
        for variant in self.variants():
            try:
                if variant.recipe.is_file() and recipe_rom_sha1(variant.recipe) == sha1:
                    return variant
            except (OSError, ValueError, KeyError, RecipeError):
                continue
        return self

    def executables(self) -> list[Path]:
        return [self.root / name for name in EXECUTABLE_NAMES]

    def check(self, need_executables: bool = True) -> None:
        missing = [p.name for p in ([self.recipe] + (self.executables() if need_executables else []))
                   if not p.exists()]
        if missing:
            raise BuilderError("This release is incomplete (%s missing next to the builder)."
                               % ", ".join(missing),
                               "Extract the whole ZIP again and run the builder from there.",
                               code="payload_incomplete")


class Progress:
    """Maps sub-task progress onto one bar: callback(fraction, message)."""

    def __init__(self, callback=None):
        self.callback = callback

    def __call__(self, fraction: float, message: str = "") -> None:
        if self.callback:
            self.callback(max(0.0, min(1.0, fraction)), message)

    def span(self, start: float, end: float, message: str):
        """A sub-task's own 0..1, optionally with a detail: "message: detail"."""
        return lambda f, detail=None: self(start + (end - start) * f,
                                           message if detail is None else "%s: %s" % (message, detail))


def build_pack(rom_path: Path, payload: Payload, out_pak: Path, progress=None,
               keep_workdir: bool = False, runner: str = "subprocess", rom: Rom | None = None) -> dict:
    """Generate emerald3ds.pak from the ROM. Returns a summary.

    `runner` picks how the voxel generators run (see voxel.py); `rom` lets a
    caller that already holds the checked ROM in memory skip reading it again.
    The summary's `timings` holds (step, seconds) for the slow parts."""
    report = Progress(progress)
    timings: list[tuple[str, float]] = []
    payload.check(need_executables=False)
    report(0.0, "Checking the ROM")
    if rom is None:
        rom = load_rom(rom_path)
    payload = payload.for_rom(rom.sha1)
    try:
        recipe = Recipe.load(payload.recipe)
    except (OSError, RecipeError) as exc:
        raise BuilderError("The release's recipe could not be read.", str(exc),
                           code="recipe_unreadable") from exc
    if recipe.rom_sha1 != rom.sha1:
        raise BuilderError("This release has no game data for %s (%s)." % (rom.title, rom.code),
                           "Each language needs its own executable and recipe in the release; "
                           "this one does not include them for that ROM.",
                           code="rom_mismatch")

    files: dict[str, bytes] = {}
    start = time.perf_counter()
    total = len(recipe.entries)
    for i, entry in enumerate(recipe.entries):
        try:
            files[entry["path"]] = build_entry(entry, rom.data, recipe.literals, recipe.bitmaps)
        except RecipeError as exc:
            raise BuilderError("A game data file could not be rebuilt from the ROM.", str(exc),
                               code="data_rebuild_failed") from exc
        if i % 100 == 0:
            report(0.05 + 0.45 * i / max(total, 1), "Extracting game data")
    timings.append(("extract", time.perf_counter() - start))

    if recipe.generated:
        work = Path(tempfile.mkdtemp(prefix="emerald3ds-"))
        try:
            report(0.5, "Preparing the 3D scenery inputs")
            start = time.perf_counter()
            build_tree(rom.data, recipe, work, report.span(0.5, 0.6, "Preparing the 3D scenery inputs"))
            timings.append(("tree", time.perf_counter() - start))
            outputs = run_generators(work, payload.voxelgen, recipe.generated,
                                     report.span(0.6, 0.9, "Generating the 3D scenery"), runner=runner,
                                     timings=timings)
            files.update(outputs)
        finally:
            if not keep_workdir:
                shutil.rmtree(work, ignore_errors=True)

    items = [(p, len(d), zlib.crc32(d) & 0xFFFFFFFF) for p, d in files.items()]
    abi = pak.engine_abi(items)
    if abi != recipe.engine_abi:
        raise BuilderError("The generated data does not match this release (ABI %08x, expected %08x)."
                           % (abi, recipe.engine_abi), code="abi_mismatch")
    report(0.92, "Writing the data pack")
    info = pak.write_pak(out_pak, sorted(files.items()), abi, bytes.fromhex(rom.sha1))
    report(0.96, "Verifying the data pack")
    with pak.PakReader(out_pak) as reader:
        reader.verify()
    report(1.0, "Done")
    return {"entries": info["entries"], "bytes": info["bytes"], "abi": abi, "release": recipe.release,
            "rom": rom.code, "timings": [(name, round(sec, 2)) for name, sec in timings],
            "payload": payload}
