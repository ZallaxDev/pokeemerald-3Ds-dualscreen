"""Header-local asset staging must follow the including translation unit."""
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


class HeaderAssetTests(unittest.TestCase):
    def test_static_header_asset_is_staged_but_ambiguous_names_are_not(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / 'src/data').mkdir(parents=True)
            (root / 'graphics/party').mkdir(parents=True)
            (root / '3ds_port/build').mkdir(parents=True)
            (root / 'src/data/party.h').write_text(
                'static const u8 sSlot[] = INCBIN_U8("graphics/party/slot.bin");\n'
                'static const u8 sCursor[] = INCBIN_U8("graphics/party/cursor.bin");\n')
            (root / 'graphics/party/slot.bin').write_bytes(b'SYNTHETIC SLOT DATA')
            (root / 'graphics/party/cursor.bin').write_bytes(b'SYNTHETIC CURSOR')
            (root / '3ds_port/build/emerald3ds.map').write_text(
                'Linker script and memory map\n'
                ' .rodata.sSlot\n  0x004615bc 0x1 build/root/src/party.o\n'
                ' .rodata.sCursor\n  0x004615c0 0x1 build/root/src/party.o\n'
                ' .rodata.sCursor\n  0x004615c4 0x1 build/root/src/other.o\n')
            subprocess.run([sys.executable, str(ROOT / 'tools/port_common/gen_asset_table.py'),
                            '--port-dir', str(root / '3ds_port')], check=True, capture_output=True)
            romfs = root / '3ds_port/romfs'
            self.assertEqual((romfs / 'graphics/party/slot.bin').read_bytes(), b'SYNTHETIC SLOT DATA')
            manifest = (romfs / 'assets/asset_map.txt').read_text()
            self.assertIn('graphics/party/slot.bin', manifest)
            self.assertNotIn('cursor.bin', manifest)
