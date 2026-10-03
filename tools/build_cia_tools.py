#!/usr/bin/env python3
"""Build pinned native CIA tooling; players receive the binary in their ZIP.

macOS/Linux: python tools/build_cia_tools.py
Sources and build products stay under ignored build/. No ROM is required.
"""
import argparse
import platform
import subprocess
import tarfile
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MAKEROM_REV = 'e8f5f529c54ff9b22a2491a480ffa69206bf7b19'
BANNER_REV = '16d8c5a0ce02a5e06e64ab42275132fca57c04a2'


def run(args, cwd=None):
    subprocess.run([str(a) for a in args], cwd=cwd, check=True)


def checkout(url, revision, destination):
    if not destination.exists():
        run(['git', 'clone', url, destination])
    run(['git', 'fetch', 'origin', revision], cwd=destination)
    run(['git', 'checkout', '--detach', revision], cwd=destination)
    run(['git', 'submodule', 'update', '--init', '--recursive'], cwd=destination)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--dir', type=Path, default=ROOT / 'build/cia-tools')
    args = ap.parse_args()
    if platform.system() not in ('Darwin', 'Linux'):
        ap.error('Build native makerom/bannertool using their documented Windows toolchain instead.')
    source = args.dir.resolve()
    source.mkdir(parents=True, exist_ok=True)
    ctr = source / 'Project_CTR'
    banner = source / 'bannertool'
    checkout('https://github.com/3DSGuy/Project_CTR.git', MAKEROM_REV, ctr)
    checkout('https://github.com/diasurgical/bannertool.git', BANNER_REV, banner)
    run(['make', 'deps'], cwd=ctr / 'makerom')
    run(['make', '-j8', 'program'], cwd=ctr / 'makerom')
    # This older build system predates Apple Silicon and Linux ARM hosts.
    base = banner / 'buildtools/make_base'
    original = subprocess.check_output(['git', 'show', 'HEAD:make_base'],
                                       cwd=banner / 'buildtools').decode('utf-8')
    updated = original.replace('else ifeq ($(UNAME_M),$(filter $(UNAME_M),i386 i686))',
        'else ifeq ($(UNAME_M),$(filter $(UNAME_M),arm64 aarch64))\n'
        '        HOST_ARCH := $(UNAME_M)\n'
        '    else ifeq ($(UNAME_M),$(filter $(UNAME_M),i386 i686))')
    base.write_text(updated)
    run(['make', '-j8', 'VERSION_MAJOR=1', 'VERSION_MINOR=2', 'VERSION_MICRO=0'], cwd=banner)
    # libblz is GPL-3.0-or-later: accompany the executable with its complete
    # corresponding source, including the dependency makefiles and libraries.
    files = subprocess.check_output(['git', 'ls-files', '-z', 'makerom'], cwd=ctr).decode().split('\0')
    with tarfile.open(source / 'makerom-source.tar.gz', 'w:gz') as archive:
        for rel in files:
            if rel and (ctr / rel).is_file():
                archive.add(ctr / rel, arcname=rel)
    urllib.request.urlretrieve(
        'https://raw.githubusercontent.com/spdx/license-list-data/main/text/GPL-3.0-or-later.txt',
        source / 'GPL-3.0.txt')
    print('makerom:', ctr / 'makerom/bin/makerom')
    print('bannertool: search output/<host>-<arch>/ in', banner)
    print('Corresponding source:', source / 'makerom-source.tar.gz')


if __name__ == '__main__':
    main()
