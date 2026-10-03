"""ROM -> data pack: the whole build, independent of any user interface."""

from __future__ import annotations

import shutil
import sys
import tempfile
import zlib
from dataclasses import dataclass
from pathlib import Path

from . import pak
from .errors import BuilderError
from .recipe import Recipe, RecipeError, build_entry
from .rom import load_rom
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
    root: Path

    @property
    def recipe(self) -> Path:
        return self.root / RECIPE_NAME

    @property
    def voxelgen(self) -> Path:
        return self.root / "voxelgen"

    def executables(self) -> list[Path]:
        return [self.root / name for name in EXECUTABLE_NAMES]

    def check(self, need_executables: bool = True) -> None:
        missing = [p.name for p in ([self.recipe] + (self.executables() if need_executables else []))
                   if not p.exists()]
        if missing:
            raise BuilderError("This release is incomplete (%s missing next to the builder)."
                               % ", ".join(missing),
                               "Extract the whole ZIP again and run the builder from there.")


class Progress:
    """Maps sub-task progress onto one bar: callback(fraction, message)."""

    def __init__(self, callback=None):
        self.callback = callback

    def __call__(self, fraction: float, message: str = "") -> None:
        if self.callback:
            self.callback(max(0.0, min(1.0, fraction)), message)

    def span(self, start: float, end: float, message: str):
        return lambda f: self(start + (end - start) * f, message)


def build_pack(rom_path: Path, payload: Payload, out_pak: Path, progress=None,
               keep_workdir: bool = False) -> dict:
    """Generate emerald3ds.pak from the ROM. Returns a summary."""
    report = Progress(progress)
    payload.check(need_executables=False)
    report(0.0, "Checking the ROM")
    rom = load_rom(rom_path)
    try:
        recipe = Recipe.load(payload.recipe)
    except (OSError, RecipeError) as exc:
        raise BuilderError("The release's recipe could not be read.", str(exc)) from exc
    if recipe.rom_sha1 != rom.sha1:
        raise BuilderError("This release has no matching recipe for %s (%s)." % (rom.title, rom.code),
                           "A Spanish ROM needs a Spanish payload: its own recipe and matching "
                           "3DS executable. The original v0.1.2 English payload cannot be reused.")

    files: dict[str, bytes] = {}
    total = len(recipe.entries)
    for i, entry in enumerate(recipe.entries):
        try:
            files[entry["path"]] = build_entry(entry, rom.data, recipe.literals, recipe.bitmaps)
        except RecipeError as exc:
            raise BuilderError("A game data file could not be rebuilt from the ROM.", str(exc)) from exc
        if i % 100 == 0:
            report(0.05 + 0.45 * i / max(total, 1), "Extracting game data")

    if recipe.generated:
        work = Path(tempfile.mkdtemp(prefix="emerald3ds-"))
        try:
            report(0.5, "Preparing the 3D scenery inputs")
            build_tree(rom.data, recipe, work, report.span(0.5, 0.6, "Preparing the 3D scenery inputs"))
            outputs = run_generators(work, payload.voxelgen, recipe.generated,
                                     report.span(0.6, 0.9, "Generating the 3D scenery"))
            files.update(outputs)
        finally:
            if not keep_workdir:
                shutil.rmtree(work, ignore_errors=True)

    items = [(p, len(d), zlib.crc32(d) & 0xFFFFFFFF) for p, d in files.items()]
    abi = pak.engine_abi(items)
    if abi != recipe.engine_abi:
        raise BuilderError("The generated data does not match this release (ABI %08x, expected %08x)."
                           % (abi, recipe.engine_abi))
    report(0.92, "Writing the data pack")
    info = pak.write_pak(out_pak, sorted(files.items()), abi, bytes.fromhex(rom.sha1))
    with pak.PakReader(out_pak) as reader:
        reader.verify()
    report(1.0, "Done")
    return {"entries": info["entries"], "bytes": info["bytes"], "abi": abi, "release": recipe.release}
