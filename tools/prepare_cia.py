#!/usr/bin/env python3
"""Prepare ROM-free CIA export assets for a host-specific builder release.

Install native makerom and bannertool first (tools/build_cia_tools.py). The
engine ELF is the stripped NOLOAD release ELF, never gamedata_image.elf.
"""
import argparse
import hashlib
import json
import platform
import shutil
import subprocess
import wave
from pathlib import Path

from elftools.elf.elffile import ELFFile
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]


def prepare(port, recipe, makerom, bannertool, out):
    import sys
    sys.path.insert(0, str(ROOT / 'builder'))
    from emerald3ds_builder.recipe import Recipe
    from tools.port_common.staging import engine_files
    spec = Recipe.load(recipe)
    elf_path = port / 'build/cia_engine.elf'
    with elf_path.open('rb') as stream:
        elf = ELFFile(stream)
        data = elf.get_section_by_name('.gamedata')
        if data is None or data['sh_type'] != 'SHT_NOBITS':
            raise ValueError('CIA template must reserve game data as NOLOAD')
        if elf.get_section_by_name('.symtab') or elf.get_section_by_name('.debug_info'):
            raise ValueError('CIA template must be stripped of symbols and debug paths')
        if elf.header['e_entry'] != 0x100000:
            raise ValueError('unexpected engine entry point')
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)
    shutil.copy2(elf_path, out / 'engine.elf')
    shutil.copy2(makerom, out / ('makerom.exe' if platform.system() == 'Windows' else 'makerom'))
    shutil.copy2(port / 'dist/Emerald3DS.smdh', out / 'icon.smdh')
    rsf = (ROOT / 'tools/cia/app.rsf').read_text()
    # Separate title IDs let English and Spanish packages coexist.
    unique_id = 0xED301 if spec.rom_sha1.startswith('fe1558a3') else 0xED300
    rsf = rsf.replace('0xED301', hex(unique_id))
    (out / 'app.rsf').write_text(rsf, encoding='utf-8')
    for rel, src in engine_files(port / 'build/romfs_release'):
        if rel == 'data.embedded':
            continue
        dst = out / 'engine' / rel
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, dst)
    # Original typography and geometry only; no cartridge artwork or audio.
    image = Image.new('RGB', (256, 128), '#124b3c')
    draw = ImageDraw.Draw(image)
    draw.rounded_rectangle((5, 5, 250, 122), radius=12, outline='#72d6a1', width=2)
    draw.text((128, 34), 'Esmeralda 3DS' if unique_id == 0xED301 else 'Emerald 3DS',
              fill='white', font=ImageFont.load_default(size=23), anchor='mm')
    draw.text((128, 65), 'Dual Screen', fill='#b1efce',
              font=ImageFont.load_default(size=17), anchor='mm')
    draw.text((128, 99), 'Proyecto homebrew', fill='#b1efce',
              font=ImageFont.load_default(size=12), anchor='mm')
    # Inputs remain in ignored build/; only the final banner enters the payload.
    tmp = ROOT / 'build/cia-banner'
    tmp.mkdir(parents=True, exist_ok=True)
    image.save(tmp / 'banner.png')
    with wave.open(str(tmp / 'silence.wav'), 'wb') as audio:
        audio.setparams((1, 2, 22050, 0, 'NONE', 'not compressed'))
        audio.writeframes(bytes(22050 * 2))
    subprocess.run([str(bannertool.resolve()), 'makebanner', '-i', str(tmp / 'banner.png'),
                    '-a', str(tmp / 'silence.wav'), '-o', str((out / 'banner.bnr').resolve())], check=True)
    files = {p.relative_to(out).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
             for p in sorted(out.rglob('*')) if p.is_file()}
    manifest = dict(schema=1, engine_abi=spec.engine_abi, rom_sha1=spec.rom_sha1,
                    title_id='%016x' % (0x0004000000000000 | unique_id << 8),
                    host=platform.system(), arch=platform.machine(), files=files)
    (out / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    print('CIA export assets:', out)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--port', type=Path, required=True)
    ap.add_argument('--recipe', type=Path, required=True)
    ap.add_argument('--makerom', type=Path, required=True)
    ap.add_argument('--bannertool', type=Path, required=True)
    ap.add_argument('--out', type=Path, required=True)
    args = ap.parse_args()
    prepare(args.port.resolve(), args.recipe.resolve(), args.makerom.resolve(),
            args.bannertool.resolve(), args.out.resolve())


if __name__ == '__main__':
    import sys
    sys.path.insert(0, str(ROOT))
    main()
