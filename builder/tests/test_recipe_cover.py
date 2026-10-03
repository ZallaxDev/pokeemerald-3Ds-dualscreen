"""Recipe search checks with synthetic data, including short text matches."""
import random
import sys
import unittest
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
from gen_recipe import Cover
from emerald3ds_builder import recipe


class CoverTests(unittest.TestCase):
    def test_indexed_search_matches_builtin_for_repeated_and_missing_data(self):
        try:
            import pydivsufsort
        except ImportError:
            self.skipTest("optional research dependency pydivsufsort is not installed")
        randomizer = random.Random(21)
        rom = b"\0" * 80 + randomizer.randbytes(400) * 3 + b"abracadabra" * 8
        cover = Cover(rom, indexed_search=True)
        needles = [b"", b"abra", b"\0" * 64, b"missing", rom[-6:]]
        for _ in range(100):
            start = randomizer.randrange(len(rom))
            needles.append(rom[start:start + randomizer.randrange(1, 70)])
            needles.append(randomizer.randbytes(randomizer.randrange(1, 70)))
        for needle in needles:
            self.assertEqual(cover.find(needle), rom.find(needle))

    def rebuild(self, rom, data, mask=None):
        cover = Cover(rom)
        ops, patches, stats = cover.run(data, mask)
        entry = {"path": "synthetic.bin", "size": len(data),
                 "crc": zlib.crc32(data) & 0xFFFFFFFF, "ops": ops,
                 "patches": patches}
        self.assertEqual(recipe.build_entry(entry, rom, bytes(cover.literals), cover.bitmaps), data)
        return cover, stats

    def test_short_terminated_text_survives_missing_long_prefix(self):
        _, stats = self.rebuild(b"XXhola\xffYY", b"hola\xff123456789")
        self.assertGreaterEqual(stats["copy"], 5)

    def test_pointer_sites_and_mixed_copies_roundtrip(self):
        rom = bytes(range(256)) * 4
        data = rom[100:160] + b"\x99\x88\x77\x66" + rom[164:280] + b"\0" * 32
        mask = bytearray(len(data))
        mask[60:64] = b"\1" * 4
        _, stats = self.rebuild(rom, data, mask)
        self.assertEqual(stats["pointer"], 4)

    def test_unmatched_data_is_cached_and_rebuilt_exactly(self):
        randomizer = random.Random(12)
        rom = randomizer.randbytes(4096)
        data = randomizer.randbytes(300) * 2
        cover, stats = self.rebuild(rom, data)
        self.assertTrue(cover.absent_prefixes)
        self.assertGreater(stats["literal"], 0)
