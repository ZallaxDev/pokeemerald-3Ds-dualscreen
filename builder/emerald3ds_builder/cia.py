"""Build one installable CIA locally, with the player's pack in RomFS."""
from __future__ import annotations

import hashlib
import json
import os
import platform
import shutil
import struct
import subprocess
import tempfile
from pathlib import Path, PurePosixPath

from . import pak
from .build import Payload, Progress, build_pack
from .errors import BuilderError
from .recipe import Recipe


def _align(value: int) -> int:
    return (value + 63) & ~63


def verify_cia(path: Path) -> dict:
    """Verify our single-content, unencrypted CIA, including its TMD content hash."""
    try:
        with path.open('rb') as stream:
            header = stream.read(32)
            if len(header) != 32:
                raise ValueError('truncated CIA header')
            hsize, kind, version, cert, ticket, tmd, meta, content = struct.unpack('<IHHIIIIQ', header)
            if hsize != 0x2020 or kind != 0 or version != 0 or content < 0x200:
                raise ValueError('unexpected CIA header')
            tmd_offset = _align(_align(_align(hsize) + cert) + ticket)
            content_offset = _align(tmd_offset + tmd)
            expected_size = _align(content_offset + content) + meta if meta else content_offset + content
            if path.stat().st_size != expected_size:
                raise ValueError('CIA size does not match its header')
            stream.seek(tmd_offset)
            raw = stream.read(tmd)
            if len(raw) < 4 or struct.unpack_from('>I', raw)[0] != 0x10004:
                raise ValueError('unexpected TMD signature type')
            body = raw[0x140:]
            if len(body) < 0x9c4 + 48 or struct.unpack_from('>H', body, 0x9e)[0] != 1:
                raise ValueError('expected one CIA content')
            title_id = struct.unpack_from('>Q', body, 0x4c)[0]
            _, index, flags, size = struct.unpack_from('>IHHQ', body, 0x9c4)
            if index != 0 or flags & 1 or size != content:
                raise ValueError('unexpected encrypted or multiple content')
            stream.seek(content_offset)
            ncch = stream.read(0x200)
            if ncch[0x100:0x104] != b'NCCH' or not ncch[0x18f] & 4:
                raise ValueError('expected an unencrypted NCCH executable')
            if struct.unpack_from('<Q', ncch, 0x118)[0] != title_id:
                raise ValueError('CIA and executable title IDs differ')
            if struct.unpack_from('<I', ncch, 0x104)[0] * 0x200 != content:
                raise ValueError('NCCH size does not match the CIA')
            stream.seek(content_offset)
            digest = hashlib.sha256()
            remaining = content
            while remaining:
                block = stream.read(min(1 << 20, remaining))
                if not block:
                    raise ValueError('truncated CIA content')
                digest.update(block)
                remaining -= len(block)
            if digest.digest() != body[0x9d4:0x9f4]:
                raise ValueError('CIA content integrity check failed')
            return {'bytes': path.stat().st_size, 'title_id': '%016x' % title_id}
    except (OSError, ValueError, struct.error) as exc:
        raise BuilderError('El CIA generado no supera la comprobación de integridad.', str(exc)) from exc


def check_cia_payload(payload: Payload) -> tuple[Path, dict]:
    root = payload.root / 'cia'
    try:
        manifest = json.loads((root / 'manifest.json').read_text(encoding='utf-8'))
        recipe = Recipe.load(payload.recipe)
        if (manifest['schema'] != 1 or manifest['engine_abi'] != recipe.engine_abi
                or manifest['rom_sha1'] != recipe.rom_sha1):
            raise ValueError('CIA engine does not match this release')
        if manifest['host'] != platform.system() or manifest['arch'] != platform.machine():
            raise ValueError('CIA tools are for another operating system or architecture')
        required = {'engine.elf', 'app.rsf', 'banner.bnr', 'icon.smdh', 'engine/engine/abi.bin',
                    'makerom.exe' if os.name == 'nt' else 'makerom'}
        if not required.issubset(manifest['files']):
            raise ValueError('incomplete CIA payload')
        for rel, expected in manifest['files'].items():
            path = PurePosixPath(rel)
            if path.is_absolute() or '..' in path.parts or '\\' in rel or ':' in rel:
                raise ValueError('unsafe CIA payload path')
            if hashlib.sha256((root / rel).read_bytes()).hexdigest() != expected:
                raise ValueError('damaged CIA payload: ' + rel)
        if struct.unpack('<I', (root / 'engine/engine/abi.bin').read_bytes())[0] != recipe.engine_abi:
            raise ValueError('CIA RomFS ABI does not match the recipe')
        return root, manifest
    except (OSError, ValueError, KeyError, TypeError, struct.error) as exc:
        raise BuilderError('Esta descarga no incluye las herramientas CIA correctas.',
                           'Extrae el ZIP completo de esta versión.\n' + str(exc)) from exc


def package_cia(pack: Path, payload: Payload, output: Path, progress=None) -> dict:
    """Package a verified pack; atomically publish only a complete, verified CIA."""
    report = Progress(progress)
    root, manifest = check_cia_payload(payload)
    recipe = Recipe.load(payload.recipe)
    with pak.PakReader(pack) as reader:
        reader.verify()
        expected = {pak.path_id(e['path']): (e['size'], e['crc']) for e in recipe.entries + recipe.generated}
        actual = {pid: (e.raw_size, e.crc32) for pid, e in reader.entries.items()}
        if (reader.abi != recipe.engine_abi or reader.rom_sha1.hex() != recipe.rom_sha1 or actual != expected):
            raise BuilderError('Los datos del juego no corresponden a esta versión del CIA.')
    if output.suffix.lower() != '.cia':
        raise BuilderError('Elige un archivo con extensión .cia.')
    output.parent.mkdir(parents=True, exist_ok=True)
    try:
        with tempfile.TemporaryDirectory(prefix='.emerald3ds-cia-', dir=output.parent) as temp:
            work = Path(temp)
            romfs = work / 'romfs'
            shutil.copytree(root / 'engine', romfs)
            shutil.copy2(pack, romfs / 'emerald3ds.pak')
            result = work / 'game.cia'
            makerom = root / ('makerom.exe' if os.name == 'nt' else 'makerom')
            report(0.1, 'Preparando el CIA con el juego completo')
            # RootPath is deliberately relative: quoted user paths never enter the RSF.
            proc = subprocess.run([str(makerom.resolve()), '-f', 'cia', '-target', 't',
                                   '-rsf', str((root / 'app.rsf').resolve()),
                                   '-elf', str((root / 'engine.elf').resolve()),
                                   '-icon', str((root / 'icon.smdh').resolve()),
                                   '-banner', str((root / 'banner.bnr').resolve()), '-o', str(result.resolve())],
                                  cwd=work, capture_output=True, text=True, errors='replace',
                                  creationflags=0x08000000 if os.name == 'nt' else 0)
            if proc.returncode:
                raise BuilderError('No se pudo generar el CIA.', (proc.stderr or proc.stdout)[-3000:])
            report(0.9, 'Comprobando la integridad del CIA')
            info = verify_cia(result)
            if info['title_id'] != manifest['title_id']:
                raise BuilderError('El identificador del CIA no corresponde a esta versión.')
            os.replace(result, output)
        report(1.0, 'CIA generado')
        return info
    except OSError as exc:
        raise BuilderError('No se pudo guardar el CIA.', str(exc)) from exc


def build_cia(rom: Path, payload: Payload, output: Path, progress=None) -> dict:
    report = Progress(progress)
    check_cia_payload(payload)  # Fail before spending minutes generating scenery.
    if output.suffix.lower() != '.cia':
        raise BuilderError('Elige un archivo con extensión .cia.')
    with tempfile.TemporaryDirectory(prefix='emerald3ds-cia-data-') as temp:
        pack = Path(temp) / 'emerald3ds.pak'
        build_pack(rom, payload, pack, lambda f, m: report(f * 0.85, m))
        return package_cia(pack, payload, output, lambda f, m: report(0.85 + 0.15 * f, m))
