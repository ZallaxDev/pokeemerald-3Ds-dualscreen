"""Host tests for files that are not in the ROM (recipe schema 2, gbagfx.py,
externals.py). Synthetic data only, no network: downloads are replaced.
Run: python -m unittest discover builder/tests
"""

import hashlib
import struct
import sys
import tempfile
import unittest
import zlib
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from emerald3ds_builder import externals as ext, gbagfx, recipe as rcp  # noqa: E402
from emerald3ds_builder.errors import BuilderError  # noqa: E402

COLOURS = [(i * 16, 255 - i * 16, (i * 37) % 256) for i in range(16)]


def indexed_png(width: int, height: int, pixel) -> bytes:
    """An 8-bit indexed PNG with COLOURS as its palette."""
    def chunk(kind: bytes, body: bytes) -> bytes:
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body))

    raw = b"".join(b"\0" + bytes(pixel(x, y) for x in range(width)) for y in range(height))
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 3, 0, 0, 0))
            + chunk(b"PLTE", b"".join(bytes(c) for c in COLOURS)) + chunk(b"IDAT", zlib.compress(raw))
            + chunk(b"IEND", b""))


def gbapal(colours) -> bytes:
    return b"".join(struct.pack("<H", (r >> 3) | (g >> 3) << 5 | (b >> 3) << 10) for r, g, b in colours)


def external(path: str, source: bytes, steps: list, result: bytes) -> dict:
    return {"repo": "owner/repo", "commit": "c0ffee", "path": path,
            "sha256": hashlib.sha256(source).hexdigest(), "steps": steps,
            "size": len(result), "crc": zlib.crc32(result) & 0xFFFFFFFF}


class RecipeSchemaTests(unittest.TestCase):
    @staticmethod
    def meta(recipe: rcp.Recipe) -> dict:
        data = recipe.to_bytes()
        (size,) = struct.unpack_from("<I", data, 8)
        return rcp.json.loads(rcp.lzma.decompress(data[12:12 + size]))

    def test_schema_follows_the_externals(self):
        plain = rcp.Recipe(engine_abi=1, rom_sha1="00" * 20, release="t")
        self.assertEqual(self.meta(plain)["schema"], rcp.SCHEMA)
        self.assertNotIn("externals", self.meta(plain))
        listed = [external("a.png", b"src", [], b"out")]
        withext = rcp.Recipe(engine_abi=1, rom_sha1="00" * 20, release="t", externals=listed)
        self.assertEqual(self.meta(withext)["schema"], rcp.SCHEMA_EXTERNALS)
        self.assertEqual(rcp.Recipe.from_bytes(withext.to_bytes()).externals, listed)
        self.assertEqual(rcp.Recipe.from_bytes(plain.to_bytes()).externals, [])

    def test_x_operation_copies_from_the_externals(self):
        externals = b"0123456789"
        entry = {"path": "x", "ops": [["X", 2, 3], ["F", 0x2A, 1]]}
        expected = b"234*"
        entry.update(size=len(expected), crc=zlib.crc32(expected) & 0xFFFFFFFF)
        self.assertEqual(rcp.build_entry(entry, b"", b"", [], externals), expected)
        entry["ops"] = [["X", 8, 3]]
        with self.assertRaises(rcp.RecipeError):
            rcp.build_entry(entry, b"", b"", [], externals)


class GbagfxTests(unittest.TestCase):
    def test_png_to_4bpp_tile_and_palette(self):
        png = indexed_png(8, 8, lambda x, y: (x + y) % 16)
        expected = bytes(((x + y) % 16) | ((x + 1 + y) % 16) << 4 for y in range(8) for x in range(0, 8, 2))
        self.assertEqual(gbagfx.convert(png, "png", "4bpp", []), expected)
        self.assertEqual(gbagfx.convert(png, "png", "gbapal", []), gbapal(COLOURS))

    def test_jasc_palette(self):
        text = "JASC-PAL\r\n0100\r\n16\r\n" + "".join("%d %d %d\r\n" % c for c in COLOURS)
        self.assertEqual(gbagfx.convert(text.encode(), "pal", "gbapal", []), gbapal(COLOURS))

    def test_lz_round_trip(self):
        data = bytes(range(64)) * 8 + b"tail"
        packed = gbagfx.convert(data, "4bpp", "lz", [])
        self.assertEqual(rcp.lz77_decompress(packed, 0), data)

    def test_unknown_option_is_refused(self):
        with self.assertRaises(gbagfx.GfxError):
            gbagfx.convert(b"", "png", "4bpp", ["-bogus"])


class ExternalsTests(unittest.TestCase):
    def setUp(self):
        self.png = indexed_png(8, 8, lambda x, y: x % 16)
        self.tiles = gbagfx.convert(self.png, "png", "4bpp", [])
        self.listed = [external("graphics/a.png", self.png, [["png", "4bpp", []]], self.tiles),
                       external("graphics/a.png", self.png, [["png", "gbapal", []]], gbapal(COLOURS))]

    def test_cached_sources_need_no_download(self):
        with tempfile.TemporaryDirectory() as tmp:
            cache = Path(tmp)
            (cache / self.listed[0]["sha256"]).write_bytes(self.png)
            with patch.object(ext, "_download", side_effect=AssertionError("no download expected")):
                out = ext.build_externals(self.listed, cache=cache)
        self.assertEqual(out, self.tiles + gbapal(COLOURS))

    def test_downloads_once_per_source_and_caches_it(self):
        with tempfile.TemporaryDirectory() as tmp:
            cache = Path(tmp)
            with patch.object(ext, "_download", return_value=self.png) as download:
                ext.build_externals(self.listed, cache=cache)
            self.assertEqual(download.call_count, 1)
            self.assertIn("raw.githubusercontent.com/owner/repo/c0ffee/graphics/a.png", download.call_args[0][0])
            self.assertEqual((cache / self.listed[0]["sha256"]).read_bytes(), self.png)

    def test_browser_path_fetches_in_order_without_threads(self):
        with tempfile.TemporaryDirectory() as tmp, patch.object(ext, "IN_BROWSER", True), \
                patch.object(ext, "_download", return_value=self.png):
            self.assertEqual(ext.build_externals(self.listed, cache=Path(tmp)), self.tiles + gbapal(COLOURS))

    def test_failures_are_reported_with_their_codes(self):
        with tempfile.TemporaryDirectory() as tmp:
            with patch.object(ext, "_download", side_effect=OSError("offline")):
                with self.assertRaises(BuilderError) as caught:
                    ext.build_externals(self.listed, cache=Path(tmp))
            self.assertEqual(caught.exception.code, "external_download_failed")
            with patch.object(ext, "_download", return_value=b"another file"):
                with self.assertRaises(BuilderError) as caught:
                    ext.build_externals(self.listed, cache=Path(tmp))
            self.assertEqual(caught.exception.code, "external_mismatch")
            wrong = [dict(self.listed[0], crc=0)]
            with patch.object(ext, "_download", return_value=self.png):
                with self.assertRaises(BuilderError) as caught:
                    ext.build_externals(wrong, cache=Path(tmp))
            self.assertEqual(caught.exception.code, "external_convert_mismatch")


if __name__ == "__main__":
    unittest.main()
