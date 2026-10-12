"""Ensure a French payload cannot install the default English executable."""
import hashlib
import sys
import tempfile
import unittest
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from emerald3ds_builder import pak, recipe as rcp, rom as romlib
from emerald3ds_builder.build import EXECUTABLE_NAMES, Payload, build_pack
from emerald3ds_builder.errors import BuilderError
from emerald3ds_builder.install import install


class FrenchPayloadTests(unittest.TestCase):
    def test_all_three_variants_build_and_install_the_matching_executable(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            base = root / "payload"
            base.mkdir()
            cases = []
            for code, directory in (("BPEE", ""), ("BPES", "es"), ("BPEF", "fr")):
                folder = base / directory
                folder.mkdir(exist_ok=True)
                data = ("synthetic-" + code).encode()
                sha = hashlib.sha1(data).hexdigest()
                crc = zlib.crc32(data) & 0xFFFFFFFF
                abi = pak.engine_abi([("test/value", len(data), crc)])
                recipe = rcp.Recipe(engine_abi=abi, rom_sha1=sha, release="synthetic",
                                    entries=[{"path": "test/value", "size": len(data), "crc": crc,
                                              "ops": [["C", 0, len(data)]]}])
                recipe.save(folder / "emerald3ds.recipe")
                for name in EXECUTABLE_NAMES:
                    (folder / name).write_bytes((code + ":" + name).encode())
                rom = romlib.Rom(data, sha, "SYNTHETIC", code, root / "fixture")
                cases.append((rom, folder))
            for rom, folder in cases:
                with self.subTest(code=rom.code):
                    output = root / (rom.code + ".pak")
                    info = build_pack(rom.source, Payload(base), output, rom=rom)
                    selected = info["payload"]
                    self.assertEqual(selected.root, folder)
                    self.assertEqual(selected.voxelgen, base / "voxelgen")
                    selected.check()
                    files = {p.name: p for p in selected.executables()}
                    files["emerald3ds.pak"] = output
                    sd = root / ("sd-" + rom.code)
                    sd.mkdir()
                    destination = install(sd, files)
                    for name in EXECUTABLE_NAMES:
                        self.assertEqual((destination / name).read_bytes(),
                                         (rom.code + ":" + name).encode())
                    with pak.PakReader(destination / "emerald3ds.pak") as reader:
                        self.assertEqual(reader.read("test/value"), rom.data)
                        self.assertEqual(reader.rom_sha1.hex(), rom.sha1)

    def test_missing_french_variant_does_not_write_a_pack(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            rcp.Recipe(engine_abi=0, rom_sha1="00" * 20, release="synthetic", entries=[]).save(
                root / "emerald3ds.recipe")
            rom = romlib.Rom(b"synthetic", "11" * 20, "SYNTHETIC", "BPEF", root / "fixture")
            output = root / "output.pak"
            with self.assertRaises(BuilderError) as error:
                build_pack(rom.source, Payload(root), output, rom=rom)
            self.assertEqual(error.exception.code, "rom_mismatch")
            self.assertFalse(output.exists())
