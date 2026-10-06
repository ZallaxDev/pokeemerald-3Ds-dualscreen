"""Files the data pack needs that are not in the player's ROM.

A release with follower Pokemon and Gen 6 icons (aarant/pokeemerald, pinned
as [followers] in upstream.lock) uses graphics drawn for that project. They
are not shipped: the recipe names each source file (GitHub repository,
commit, path, SHA-256) and the gbagfx steps that made the pack's bytes from
it. The builder downloads each source once (kept in a cache on a desktop),
checks its SHA-256, converts it (gbagfx.py) and checks the result's size and
CRC-32. The results, one after the other in the recipe's order, are what its
"X" operations copy from.

In the web builder (Pyodide in a Web Worker) there are neither sockets nor
threads: the sources are fetched one by one with synchronous requests, which
a worker may make, from the same GitHub address (it allows any origin).
"""

from __future__ import annotations

import hashlib
import os
import sys
import zlib
from pathlib import Path

from . import gbagfx
from .errors import BuilderError

RAW_URL = "https://raw.githubusercontent.com/{repo}/{commit}/{path}"
DOWNLOAD_THREADS = 16
TIMEOUT = 60
IN_BROWSER = sys.platform == "emscripten"


def cache_root() -> Path:
    """The builder's caches on this computer (EMERALD3DS_BUILDER_CACHE
    overrides it, for tests)."""
    if os.environ.get("EMERALD3DS_BUILDER_CACHE"):
        return Path(os.environ["EMERALD3DS_BUILDER_CACHE"])
    if sys.platform == "win32":
        base = Path(os.environ.get("LOCALAPPDATA") or Path.home() / "AppData" / "Local")
    elif sys.platform == "darwin":
        base = Path.home() / "Library" / "Caches"
    else:
        base = Path(os.environ.get("XDG_CACHE_HOME") or Path.home() / ".cache")
    return base / "emerald3ds-builder"


def cache_dir() -> Path:
    """Where downloaded sources are kept between builds (by SHA-256)."""
    return cache_root() / "sources"


def _download(url: str) -> bytes:
    if IN_BROWSER:
        # A synchronous request: allowed in the Web Worker the builder runs in.
        from js import Uint8Array, XMLHttpRequest   # noqa: PLC0415 - Pyodide only

        request = XMLHttpRequest.new()
        request.open("GET", url, False)
        request.responseType = "arraybuffer"
        request.send(None)
        if request.status != 200:
            raise OSError("HTTP %d" % request.status)
        return bytes(Uint8Array.new(request.response).to_py())
    import urllib.request   # noqa: PLC0415

    with urllib.request.urlopen(url, timeout=TIMEOUT) as response:
        return response.read()


def _fetch(source: tuple, cache: Path) -> bytes:
    repo, commit, path, sha256 = source
    cached = cache / sha256
    if cached.is_file():
        data = cached.read_bytes()
        if hashlib.sha256(data).hexdigest() == sha256:
            return data
    url = RAW_URL.format(repo=repo, commit=commit, path=path)
    try:
        data = _download(url)
    except Exception as exc:  # noqa: BLE001 - urllib's errors, or the browser's
        raise BuilderError("A file could not be downloaded from GitHub.",
                           "%s\n%s\nCheck the internet connection and try again." % (url, exc),
                           code="external_download_failed") from exc
    if hashlib.sha256(data).hexdigest() != sha256:
        raise BuilderError("A downloaded file is not the expected version.", url,
                           code="external_mismatch")
    try:
        cache.mkdir(parents=True, exist_ok=True)
        tmp = cached.with_suffix(".partial")
        tmp.write_bytes(data)
        os.replace(tmp, cached)
    except OSError:
        pass   # a cache that cannot be written only means downloading again next time
    return data


def convert(external: dict, source: bytes) -> bytes:
    """The pack's bytes from one source, by the recipe's gbagfx steps."""
    data = source
    for src_ext, dst_ext, args in external["steps"]:
        data = gbagfx.convert(data, src_ext, dst_ext, list(args))
    return data


def build_externals(externals: list, progress=None, cache: Path | None = None) -> bytes:
    """Download, check and convert every external; returns them joined."""
    if not externals:
        return b""
    cache = cache or cache_dir()
    sources = sorted({(e["repo"], e["commit"], e["path"], e["sha256"]) for e in externals})
    fetched: dict[tuple, bytes] = {}

    def fetched_one(done: int) -> None:
        if progress and done % 25 == 0:
            progress(done / len(sources) * 0.8)

    if IN_BROWSER:
        for done, source in enumerate(sources, 1):
            fetched[source] = _fetch(source, cache)
            fetched_one(done)
    else:
        from concurrent.futures import ThreadPoolExecutor   # noqa: PLC0415

        with ThreadPoolExecutor(DOWNLOAD_THREADS) as pool:
            for done, (source, data) in enumerate(zip(sources, pool.map(lambda s: _fetch(s, cache), sources)), 1):
                fetched[source] = data
                fetched_one(done)
    out = bytearray()
    for i, e in enumerate(externals):
        source = fetched[(e["repo"], e["commit"], e["path"], e["sha256"])]
        try:
            data = convert(e, source)
        except gbagfx.GfxError as exc:
            raise BuilderError("A downloaded file could not be converted.", "%s: %s" % (e["path"], exc),
                               code="external_convert_failed") from exc
        if len(data) != e["size"] or (zlib.crc32(data) & 0xFFFFFFFF) != e["crc"]:
            raise BuilderError("A converted file does not match this release.",
                               "%s (size %d/%d)" % (e["path"], len(data), e["size"]),
                               code="external_convert_mismatch")
        out += data
        if progress and i % 50 == 0:
            progress(0.8 + 0.2 * i / len(externals))
    return bytes(out)
