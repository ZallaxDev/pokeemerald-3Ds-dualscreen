"""Localization staging against tiny synthetic bytes, never ROM fixtures."""
import contextlib
import gzip
import hashlib
import io
import json
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import localize_french
from emerald3ds_builder.rom import Rom


class FrenchStagingTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.tree = self.root / "tree"
        self.data = bytes(range(128))
        self.sha = hashlib.sha1(self.data).hexdigest()
        self.rom = Rom(self.data, self.sha, "SYNTHETIC", "BPEF", self.root / "fixture")
        self.sources = {
            "src/fonts.c": "const unsigned char widths[] = {0};\n",
            "src/phrases.c": "const unsigned short words[][2] = {{0,0}};\n",
            "src/data/credits.h": "/* synthetic credits placeholder */\n",
            "src/data/battle_frontier/trainer_hill.h": "/* no floors */\n",
            "include/constants/global.h": "#define GAME_LANGUAGE (LANGUAGE_ENGLISH)\n",
        }
        for name, text in self.sources.items():
            path = self.tree / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(text.encode())
        files = {}
        for name, needle, offset, size, kind in (
                ("src/fonts.c", "{0}", 8, 3, "u8array"),
                ("src/phrases.c", "{0,0}", 16, 8, "u16rows2")):
            text = self.sources[name]
            start = text.index(needle)
            files[name] = {"sha256": hashlib.sha256(text.encode()).hexdigest(),
                           "edits": [[start, start + len(needle), offset, size, kind]]}
        self.manifest = {"rom_sha1": self.sha, "files": files, "graphics": [],
                         "credits": 0, "credits_entries": 0, "floors": []}
        self.locales = self.root / "tools/locales"
        self.locales.mkdir(parents=True)
        # Avoid any dependency on reference symbols from a real cartridge.
        (self.locales / "french-symbols.s.gz").write_bytes(gzip.compress(b".syntax unified\n"))

    def stage(self):
        (self.locales / "french.json.gz").write_bytes(
            gzip.compress(json.dumps(self.manifest).encode()))
        with patch.object(localize_french, "ROOT", self.root), \
                patch.object(localize_french, "FRENCH_SHA1", self.sha), \
                patch.object(localize_french, "load_rom", return_value=self.rom), \
                contextlib.redirect_stdout(io.StringIO()):
            localize_french.stage(self.tree, self.rom.source)

    def assert_untouched(self):
        for name, text in self.sources.items():
            self.assertEqual((self.tree / name).read_bytes(), text.encode(), name)
        self.assertFalse((self.tree / ".emerald3ds-locale").exists())

    def test_numeric_tables_keep_their_c_dimensions(self):
        self.stage()
        self.assertEqual((self.tree / "src/fonts.c").read_text(),
                         "const unsigned char widths[] = {0x08,0x09,0x0A};\n")
        self.assertEqual((self.tree / "src/phrases.c").read_text(),
                         "const unsigned short words[][2] = {{0x1110,0x1312},{0x1514,0x1716}};\n")
        self.assertIn("LANGUAGE_FRENCH", (self.tree / "include/constants/global.h").read_text())

    def test_wrong_rom_hash_is_rejected_before_any_write(self):
        self.manifest["rom_sha1"] = "00" * 20
        with self.assertRaisesRegex(ValueError, "clean French BPEF"):
            self.stage()
        self.assert_untouched()

    def test_naming_column_positions_are_read_as_three_nine_byte_rows(self):
        edit = self.manifest["files"]["src/fonts.c"]["edits"][0]
        edit[3:] = [27, "u8rows9"]
        self.stage()
        rows = ["{" + ",".join("0x%02X" % n for n in range(start, start + 9)) + "}"
                for start in (8, 17, 26)]
        expected = "const unsigned char widths[] = {" + ",".join(rows) + "};\n"
        self.assertEqual((self.tree / "src/fonts.c").read_text(), expected)

    def test_source_mismatch_is_rejected_before_any_write(self):
        self.manifest["files"]["src/phrases.c"]["sha256"] = "00" * 32
        with self.assertRaisesRegex(ValueError, "Source differs"):
            self.stage()
        self.assert_untouched()

    def test_bad_edit_or_table_dimensions_leave_all_sources_untouched(self):
        original = self.manifest["files"]["src/phrases.c"]["edits"][0]
        for edit, error in (
                ([original[0], original[1], 127, 8, "u16rows2"], "out-of-range"),
                ([original[0], original[1], 16, 6, "u16rows2"], "dimensions"),
                ([original[0], original[1], 16, 8, "unknown"], "Unknown edit kind")):
            with self.subTest(edit=edit):
                self.manifest["files"]["src/phrases.c"]["edits"] = [edit]
                with self.assertRaisesRegex(ValueError, error):
                    self.stage()
                self.assert_untouched()

    def test_overlapping_edits_are_rejected_before_any_write(self):
        edits = self.manifest["files"]["src/phrases.c"]["edits"]
        edits.append(edits[0][:])
        with self.assertRaisesRegex(ValueError, "Overlapping"):
            self.stage()
        self.assert_untouched()
