"""verify_scripts.py --song-headers on a made-up song region: the linker's
section markers in it are not song headers, and a real header with a pointer
missing from the relocation table still fails the build."""

import contextlib
import importlib.util
import io
import struct
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("verify_scripts", ROOT / "3ds_port" / "scripts" / "verify_scripts.py")
VERIFY = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(VERIFY)

START = 0x2000
# One song header at offset 8: one track; its voice group pointer at 12 and
# its track pointer at 16 are the words that need relocating.
HEADER_AT = 8


def run(sites, markers):
    payload = bytearray(24)
    payload[HEADER_AT] = 1
    symbols = {"AgbMain": 0x100, "__ctr_song_blob": START, "__ctr_song_blob_end": START + len(payload),
               "mus_dummy": START + HEADER_AT}
    symbols.update(markers)

    def fake_symbols(elf, global_names):
        global_names.update(symbols)
        return dict(symbols)

    with tempfile.TemporaryDirectory() as folder:
        out = Path(folder)
        (out / "songs.bin").write_bytes(payload)
        table = VERIFY.HEADER.pack(b"C3AB", VERIFY.VERSION, len(payload), symbols["AgbMain"], len(sites), 0)
        (out / "songs.rel").write_bytes(table + struct.pack("<%dI" % len(sites), *sites))
        argv = ["verify_scripts.py", "--elf", "unused.elf", "--out-dir", str(out), "--tag", "verify_songs",
                "--magic", "C3AB", "--payload", "songs.bin", "--reloc", "songs.rel",
                "--blob-symbol", "__ctr_song_blob", "--require", "mus_dummy", "--song-headers"]
        log = io.StringIO()
        with patch("sys.argv", argv), patch.object(VERIFY, "elf_symbols", fake_symbols), \
                patch.object(VERIFY, "loaded_range", return_value=(START, START + len(payload))), \
                contextlib.redirect_stdout(log), contextlib.redirect_stderr(log):
            return VERIFY.main(), log.getvalue()


class SongHeaderMarkerTests(unittest.TestCase):
    def test_script_blob_end_on_the_first_byte(self):
        # the Spanish build's layout: the script blob ends where songs begin
        status, log = run([HEADER_AT + 4, HEADER_AT + 8], {"__ctr_script_blob_end": START})
        self.assertEqual(status, 0, log)

    def test_any_section_marker_inside(self):
        status, log = run([HEADER_AT + 4, HEADER_AT + 8], {"__some_section_end": START + 4})
        self.assertEqual(status, 0, log)

    def test_unrelocated_track_still_fails(self):
        status, log = run([HEADER_AT + 4], {"__ctr_script_blob_end": START})
        self.assertNotEqual(status, 0, log)
        self.assertIn("unrelocated pointers: mus_dummy", log)


if __name__ == "__main__":
    unittest.main()
