"""Optional regression against all staged tileset LZ payloads (requires RomFS)."""
from pathlib import Path
import argparse
import subprocess
import tempfile

def reference(data):
    n = int.from_bytes(data[1:4], 'little')
    p, out = 4, bytearray()
    while len(out) < n:
        flags = data[p]; p += 1
        for bit in range(7, -1, -1):
            if len(out) >= n: break
            if flags & (1 << bit):
                a, b = data[p:p+2]; p += 2
                for _ in range((a >> 4) + 3):
                    if len(out) >= n: break
                    out.append(out[-(((a & 15) << 8) + b + 1)])
            else:
                out.append(data[p]); p += 1
    return bytes(out)

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--cc', default='gcc')
    args = parser.parse_args()
    port = Path(__file__).resolve().parents[1]
    paths = []
    for line in (port / 'romfs/assets/asset_map.txt').read_text().splitlines():
        parts = line.split()
        if len(parts) == 3 and '/tilesets/' in parts[2] and parts[2].endswith('/tiles.4bpp.lz'):
            paths.append(port / 'romfs' / parts[2])
    assert paths, 'No staged tilesets'
    with tempfile.TemporaryDirectory(dir=port / 'build', prefix='tile-assets-') as tmp:
        tmp = Path(tmp); c = tmp / 'test.c'; exe = tmp / 'test.exe'
        c.write_text('#include <stdlib.h>\n#define main fixture_main\n#include "' +
                     (port / 'tests/voxel_world_hash_test.c').as_posix() + '"\n#undef main\n' + r'''
int main(int argc, char **argv) {
    assert(argc == 2);
    FILE *f = fopen(argv[1], "rb"); assert(f);
    fseek(f, 0, SEEK_END); long n = ftell(f); rewind(f);
    uint8_t *src = malloc(n), dest[65536]; assert(src);
    assert(fread(src, 1, n, f) == (size_t)n); fclose(f);
    sSizedAsset = src; sSizedBytes = n;
    struct Tileset t = {.isCompressed = true, .tiles = (const void *)src};
    VoxelTileLoad load = {0};
    while (!Voxel_LoadTilesStep(&t, dest, sizeof(dest), &load, 512)) {}
    assert(load.ok);
    for (unsigned i = 0; i < load.written; ++i) printf("%02x", dest[i]);
    free(src); return 0;
}
''')
        subprocess.run([args.cc, '-std=gnu99', '-O2', '-DPORTABLE', '-DMODERN=1',
                        '-D__INTELLISENSE__', '-iquote', str(port / 'compat'),
                        '-iquote', str(port.parent / 'include'), '-I' + str(port / 'src/voxel'),
                        str(c), '-o', str(exe)], check=True)
        for path in paths:
            actual = bytes.fromhex(subprocess.check_output([str(exe), str(path)], text=True))
            assert actual == reference(path.read_bytes()), str(path)
    print('PASS sliced LZ decoder: byte-identical to independent decoder for', len(paths), 'real tileset payloads')
if __name__ == '__main__':
    main()
