"""web-manifest.json: the contract between a release and the web builder.

Every release carries `Emerald3DS-WebPayload.zip`, and next to it the same
`web-manifest.json` as a separate small asset so the website can read a
release's capabilities without downloading the payload. Layout of the ZIP:

    web-manifest.json
    payload/Emerald3DS.3dsx, payload/Emerald3DS.smdh, payload/emerald3ds.recipe
    payload/voxelgen/...          the generator scripts (as in the Windows ZIP)
    python/emerald3ds_builder/... this package, without the desktop window
    licenses/...

It never holds a ROM, a data pack or anything extracted from the game.

`schemaVersion` changes only when an existing field changes meaning or goes
away; new optional fields keep the version. The website refuses a schema it
does not know, so a release can never be half-understood. `validate()` is used
by the generator, by tools/release_audit.py and by the web adapter, so all
three agree on what a valid manifest is.
"""

from __future__ import annotations

import re

SCHEMA_VERSION = 1
KIND = "emerald3ds-web-payload"
MANIFEST_NAME = "web-manifest.json"
PAYLOAD_NAME = "Emerald3DS-WebPayload.zip"
ENTRYPOINT = {"pythonPath": "python", "module": "emerald3ds_builder.web",
              "function": "run_web_build", "apiVersion": 1}
INSTALL_DIR = "3ds/emerald3ds"
INSTALL_FILES = ["Emerald3DS.3dsx", "Emerald3DS.smdh", "emerald3ds.pak"]
# Files that may never be inside a web payload, whatever their name says.
FORBIDDEN_SUFFIXES = (".gba", ".agb", ".pak", ".sav", ".pyc", ".pyo")

_HEX = {"sha1": re.compile(r"^[0-9a-f]{40}$"), "sha256": re.compile(r"^[0-9a-f]{64}$"),
        "abi": re.compile(r"^[0-9a-f]{8}$")}


class ManifestError(ValueError):
    pass


def safe_path(path: str) -> bool:
    return (bool(path) and not path.startswith("/") and "\\" not in path and ":" not in path
            and ".." not in path.split("/"))


def _file_ref(value, where: str, need_release_asset: bool = False) -> None:
    if not isinstance(value, dict):
        raise ManifestError("%s must be an object" % where)
    if not isinstance(value.get("path"), str) or not safe_path(value["path"]):
        raise ManifestError("%s.path is not a safe relative path" % where)
    if not isinstance(value.get("sha256"), str) or not _HEX["sha256"].match(value["sha256"]):
        raise ManifestError("%s.sha256 must be 64 lowercase hex digits" % where)
    if not isinstance(value.get("size"), int) or value["size"] < 0:
        raise ManifestError("%s.size must be a non-negative integer" % where)
    if need_release_asset and value.get("releaseAsset") is not None and not isinstance(value["releaseAsset"], str):
        raise ManifestError("%s.releaseAsset must be a file name or null" % where)


def validate(manifest: dict) -> dict:
    """Raise ManifestError unless `manifest` is a valid schema-1 manifest."""
    if not isinstance(manifest, dict):
        raise ManifestError("the manifest must be a JSON object")
    if manifest.get("schemaVersion") != SCHEMA_VERSION:
        raise ManifestError("unsupported schemaVersion %r" % manifest.get("schemaVersion"))
    if manifest.get("kind") != KIND:
        raise ManifestError("kind must be %r" % KIND)
    for key in ("releaseVersion", "releaseTag", "builderVersion"):
        if not isinstance(manifest.get(key), str) or not manifest[key]:
            raise ManifestError("%s must be a non-empty string" % key)
    if not isinstance(manifest.get("dataAbi"), str) or not _HEX["abi"].match(manifest["dataAbi"]):
        raise ManifestError("dataAbi must be 8 lowercase hex digits")
    if not isinstance(manifest.get("packSchema"), int):
        raise ManifestError("packSchema must be an integer")
    roms = manifest.get("supportedRoms")
    if not isinstance(roms, list) or not roms:
        raise ManifestError("supportedRoms must list at least one ROM")
    for i, rom in enumerate(roms):
        if not isinstance(rom, dict) or not _HEX["sha1"].match(str(rom.get("sha1", ""))):
            raise ManifestError("supportedRoms[%d].sha1 must be 40 lowercase hex digits" % i)
        if not isinstance(rom.get("name"), str):
            raise ManifestError("supportedRoms[%d].name must be a string" % i)
    entry = manifest.get("entrypoint")
    if not isinstance(entry, dict) or not all(isinstance(entry.get(k), str) for k in ("pythonPath", "module", "function")):
        raise ManifestError("entrypoint must name pythonPath, module and function")
    assets = manifest.get("assets")
    if not isinstance(assets, dict):
        raise ManifestError("assets must be an object")
    for key in ("threeDsx", "smdh", "recipe"):
        _file_ref(assets.get(key), "assets.%s" % key, need_release_asset=True)
    if "ciaForwarder" not in assets:
        raise ManifestError("assets.ciaForwarder must be present (null while there is none)")
    cia = assets["ciaForwarder"]
    if cia is not None:
        if not isinstance(cia, dict) or not isinstance(cia.get("releaseAsset"), str):
            raise ManifestError("assets.ciaForwarder must be null or name its release asset")
    files = manifest.get("files")
    if not isinstance(files, list) or not files:
        raise ManifestError("files must list the payload's files")
    seen = set()
    for i, item in enumerate(files):
        _file_ref(item, "files[%d]" % i)
        if item["path"] in seen:
            raise ManifestError("files lists %s twice" % item["path"])
        if item["path"].lower().endswith(FORBIDDEN_SUFFIXES):
            raise ManifestError("files may not contain %s" % item["path"])
        seen.add(item["path"])
    for key in ("threeDsx", "smdh", "recipe"):
        if assets[key]["path"] not in seen:
            raise ManifestError("assets.%s is not in files" % key)
    return manifest
