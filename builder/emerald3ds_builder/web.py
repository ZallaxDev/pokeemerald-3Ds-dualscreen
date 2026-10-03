"""The builder's entry point for the web builder (Python in the browser).

The website runs this package under Pyodide in a Web Worker, with the
release's WebPayload unpacked into the in-memory file system:

    from emerald3ds_builder.web import run_web_build
    result = run_web_build("/rom/rom.gba", "/payload", "/out/emerald3ds.pak",
                           progress, "/web-manifest.json")

It is the desktop build (`build.build_pack`) with two differences only: the
voxel generators run in this interpreter instead of in child processes, and
progress is reported as named stages. Validation, recipe, CRC, ABI and pack
checks are the same code. The ROM is read from the browser's memory; nothing
is sent anywhere.
"""

from __future__ import annotations

import json
from pathlib import Path

from . import __version__, pak
from .build import Payload, build_pack
from .errors import BuilderError
from .recipe import RecipeError
from .rom import SUPPORTED_SHA1, load_rom
from .webmanifest import ManifestError, validate

API_VERSION = 1
# build_pack's progress messages -> the stages the web page shows.
STAGES = {
    "Checking the ROM": "rom",
    "Extracting game data": "data",
    "Preparing the 3D scenery inputs": "scenery",
    "Generating the 3D scenery": "scenery",
    "Writing the data pack": "verify",
    "Verifying the data pack": "verify",
    "Done": "verify",
}


def supported_sha1s(manifest: dict | None) -> tuple[str, ...]:
    if manifest:
        return tuple(rom["sha1"] for rom in manifest["supportedRoms"])
    return (SUPPORTED_SHA1,)


def build_pack_for_web(rom_path, payload_path, output_path, progress_callback=None,
                       manifest_path=None) -> dict:
    """ROM -> emerald3ds.pak with in-process generators.

    progress_callback(stage, fraction, message): stage is one of "rom", "data",
    "scenery", "verify"; fraction is the overall position (0..1)."""
    manifest = None
    if manifest_path is not None:
        try:
            manifest = validate(json.loads(Path(manifest_path).read_text(encoding="utf-8")))
        except (OSError, ValueError, ManifestError) as exc:
            raise BuilderError("This release's web manifest is not valid.", str(exc),
                               code="manifest_invalid") from exc

    def report(fraction: float, message: str) -> None:
        if progress_callback is not None:
            progress_callback(STAGES.get(message, "data"), fraction, message)

    rom = load_rom(Path(rom_path), supported_sha1s(manifest))
    info = build_pack(Path(rom_path), Payload(Path(payload_path)), Path(output_path), report,
                      runner="inprocess", rom=rom)
    if manifest is not None and "%08x" % info["abi"] != manifest["dataAbi"]:
        raise BuilderError("The generated data does not match this release's manifest.",
                           code="abi_mismatch")
    info["abi"] = "%08x" % info["abi"]
    info["builderVersion"] = __version__
    return info


def describe(exc: BaseException) -> dict:
    """A JSON-friendly description of a failure, for the web page."""
    if isinstance(exc, BuilderError):
        return {"code": exc.code, "message": exc.message, "hint": exc.hint}
    if isinstance(exc, MemoryError):
        return {"code": "insufficient_memory", "message": "The browser ran out of memory.", "hint": ""}
    if isinstance(exc, (pak.PakError, RecipeError)):
        return {"code": "pak_invalid", "message": "The generated data pack failed verification.",
                "hint": str(exc)}
    return {"code": "internal_error", "message": "The builder stopped unexpectedly.",
            "hint": "%s: %s" % (type(exc).__name__, exc)}


def run_web_build(rom_path, payload_path, output_path, progress_callback=None, manifest_path=None) -> str:
    """build_pack_for_web, never raising: returns a JSON string
    {"ok": true, "result": {...}} or {"ok": false, "error": {code, message, hint}}."""
    try:
        result = build_pack_for_web(rom_path, payload_path, output_path, progress_callback, manifest_path)
        return json.dumps({"ok": True, "apiVersion": API_VERSION, "result": result})
    except BaseException as exc:  # noqa: BLE001 - everything is reported to the page
        if isinstance(exc, (KeyboardInterrupt, SystemExit)):
            raise
        return json.dumps({"ok": False, "apiVersion": API_VERSION, "error": describe(exc)})
