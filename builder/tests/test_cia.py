"""CIA corruption, wrong-release and atomic-export regression tests, no ROM."""
import hashlib
import json
import platform
import struct
import sys
import tempfile
import unittest
import zlib
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from emerald3ds_builder import pak
from emerald3ds_builder.build import Payload
from emerald3ds_builder.cia import check_cia_payload, package_cia, verify_cia
from emerald3ds_builder.errors import BuilderError
from emerald3ds_builder.recipe import Recipe

TITLE = 0x000400000ed30100
SHA = 'fe1558a3dcb0360ab558969e09b690888b846dd9'


def synthetic_cia():
    content = bytearray(0x800)
    content[0x100:0x104] = b'NCCH'
    content[0x18f] = 4
    struct.pack_into('<I', content, 0x104, len(content) // 0x200)
    struct.pack_into('<Q', content, 0x118, TITLE)
    tmd = bytearray(0x140 + 0x9c4 + 48)
    struct.pack_into('>I', tmd, 0, 0x10004)
    struct.pack_into('>Q', tmd, 0x140 + 0x4c, TITLE)
    struct.pack_into('>H', tmd, 0x140 + 0x9e, 1)
    struct.pack_into('>IHHQ', tmd, 0x140 + 0x9c4, 0, 0, 0, len(content))
    tmd[0x140 + 0x9d4:] = hashlib.sha256(content).digest()
    header = bytearray(0x2040)
    struct.pack_into('<IHHIIIIQ', header, 0, 0x2020, 0, 0, 0, 0, len(tmd), 0, len(content))
    raw = header + tmd
    raw.extend(bytes((-len(raw)) % 64))
    return raw + content


class CiaTests(unittest.TestCase):
    def test_integrity_rejects_corrupt_truncated_and_wrong_title(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / 'game.cia'
            good = synthetic_cia()
            path.write_bytes(good)
            self.assertEqual(verify_cia(path)['title_id'], '%016x' % TITLE)
            bad = good.copy()
            bad[-1] ^= 1
            for raw in (bad, good[:-1], b'not a CIA'):
                path.write_bytes(raw)
                with self.assertRaises(BuilderError):
                    verify_cia(path)
            wrong_title = good.copy()
            wrong_title[0x2040 + 0x140 + 0x4c] ^= 1
            path.write_bytes(wrong_title)
            with self.assertRaises(BuilderError):
                verify_cia(path)

    def fixture(self, root):
        data = b'USER-OWNED SYNTHETIC GAME DATA'
        crc = zlib.crc32(data) & 0xffffffff
        abi = pak.engine_abi([('game/test', len(data), crc)])
        Recipe(abi, SHA, 'test', entries=[dict(path='game/test', size=len(data), crc=crc,
                                               ops=[])]).save(root / 'emerald3ds.recipe')
        assets = root / 'cia'
        assets.mkdir()
        files = {'engine.elf': b'ELF fixture', 'app.rsf': b'fixture', 'icon.smdh': b'SMDH',
                 'banner.bnr': b'banner', 'engine/engine/abi.bin': struct.pack('<I', abi),
                 'makerom.exe' if sys.platform == 'win32' else 'makerom': b'tool fixture'}
        for name, value in files.items():
            path = assets / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(value)
        manifest = dict(schema=1, engine_abi=abi, rom_sha1=SHA, title_id='%016x' % TITLE,
                        host=platform.system(), arch=platform.machine(),
                        files={p: hashlib.sha256(d).hexdigest() for p, d in files.items()})
        (assets / 'manifest.json').write_text(json.dumps(manifest))
        pack = root / 'data.pak'
        pak.write_pak(pack, [('game/test', data)], abi, bytes.fromhex(SHA))
        return Payload(root), pack, assets

    def test_tampered_template_and_wrong_pack_are_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            payload, pack, assets = self.fixture(root)
            check_cia_payload(payload)
            (assets / 'engine.elf').write_bytes(b'wrong engine')
            with self.assertRaises(BuilderError):
                check_cia_payload(payload)
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            payload, pack, assets = self.fixture(root)
            pak.write_pak(pack, [('game/test', b'other release')], 0, bytes(20))
            with patch('emerald3ds_builder.cia.subprocess.run') as run:
                with self.assertRaises(BuilderError):
                    package_cia(pack, payload, root / 'game.cia')
                run.assert_not_called()

    def test_embeds_pack_and_failed_export_preserves_existing_file(self):
        with tempfile.TemporaryDirectory(prefix='CIA path with spaces ') as temp:
            root = Path(temp)
            payload, pack, assets = self.fixture(root)
            output = root / 'Saved game.cia'
            output.write_bytes(b'previous export')
            with patch('emerald3ds_builder.cia.subprocess.run') as run:
                run.return_value.returncode = 1
                run.return_value.stderr = 'makerom failed'
                with self.assertRaises(BuilderError):
                    package_cia(pack, payload, output)
            self.assertEqual(output.read_bytes(), b'previous export')
            def makerom(args, **kwargs):
                work = Path(kwargs['cwd'])
                self.assertEqual((work / 'romfs/emerald3ds.pak').read_bytes(), pack.read_bytes())
                self.assertEqual((work / 'romfs/engine/abi.bin').read_bytes(),
                                 (assets / 'engine/engine/abi.bin').read_bytes())
                Path(args[args.index('-o') + 1]).write_bytes(synthetic_cia())
                class Result:
                    returncode, stdout, stderr = 0, '', ''
                return Result()
            with patch('emerald3ds_builder.cia.subprocess.run', side_effect=makerom):
                package_cia(pack, payload, output)
            self.assertEqual(output.read_bytes(), synthetic_cia())
            self.assertFalse(list(root.glob('.emerald3ds-cia-*')))
