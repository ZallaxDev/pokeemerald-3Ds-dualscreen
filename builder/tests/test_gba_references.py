import importlib.util
import struct
import unittest
from pathlib import Path

spec = importlib.util.spec_from_file_location('gba_references', Path(__file__).resolve().parents[2] / 'tools/gba_references.py')
references = importlib.util.module_from_spec(spec)
spec.loader.exec_module(references)


class ReferenceDecodingTests(unittest.TestCase):
    def test_rom_and_ram_pointers(self):
        rom = struct.pack('<II', 0x08000004, 0x02000000)
        self.assertEqual(references.pointer_target(rom, 0), 4)
        self.assertIsNone(references.pointer_target(rom, 4))

    def test_forward_and_backward_thumb_calls(self):
        rom = struct.pack('<HHHH', 0xf000, 0xf800, 0xf7ff, 0xfffc)
        self.assertEqual(references.thumb_call_target(rom, 0), 4)
        self.assertEqual(references.thumb_call_target(rom, 4), 0)

    def test_rejects_truncated_and_non_call_data(self):
        with self.assertRaises(ValueError):
            references.pointer_target(b'\0', 0)
        with self.assertRaises(ValueError):
            references.thumb_call_target(bytes(4), 0)
