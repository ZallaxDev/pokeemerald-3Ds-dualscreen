#!/usr/bin/env python3
"""Software preview of the actual packed building and C-emitted light meshes.
Not a hardware screenshot; excludes scene weather, bloom and terrain shadows.
Generate --lights with tests/night_buildings_test.c's optional dump argument.
"""

import argparse
import struct
from pathlib import Path
from PIL import Image, ImageDraw
import voxel_building as vb
from gen_voxel_buildings import NIGHT_WINDOWS


def texel(x, y, width):
    m = sum(
        (((x >> bit) & 1) << (bit * 2)) | (((y >> bit) & 1) << (bit * 2 + 1))
        for bit in range(3)
    )
    return ((y // 8) * (width // 8) + x // 8) * 64 + m


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--pack", type=Path, required=True)
    ap.add_argument("--lights", type=Path, required=True)
    ap.add_argument("--out", type=Path, required=True)
    args = ap.parse_args()
    raw = args.pack.read_bytes()
    assert raw[:4] == b"VXB7"
    pages, models, pms, placements, h, masks, vertices, variants, windows = (
        struct.unpack_from("<6HI2H", raw, 4)
    )
    page_table = [struct.unpack_from("<HHI", raw, 24 + i * 8) for i in range(pages)]
    off = 24 + pages * 8
    model_table = [
        struct.unpack_from("<BBHIII", raw, off + i * 16) for i in range(models)
    ]
    off += models * 16
    pm_table = [struct.unpack_from("<HHhh", raw, off + i * 8) for i in range(pms)]
    off += pms * 8 + placements * 16 + h
    off += off & 1
    off += h * 2 + masks * 32 + h
    off += off & 1
    off += variants * 6
    off += (-off) % 4
    packed_vertices = [
        struct.unpack_from("<6f", raw, off + i * 24) for i in range(vertices)
    ]
    off += vertices * 24
    rects = {}
    for i in range(windows):
        model, *rect = struct.unpack_from("<5H", raw, off + i * 10)
        rects.setdefault(model, []).append(tuple(rect))
    lights = args.lights.read_bytes()
    if lights[:4] != b"NXM1":
        raise ValueError(
            "Light mesh has no pack fingerprint; regenerate it with night_buildings_test"
        )
    fingerprint = 14695981039346656037
    for byte in raw:
        fingerprint = ((fingerprint ^ byte) * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    if struct.unpack_from("<Q", lights, 4)[0] != fingerprint:
        raise ValueError(
            "Light mesh belongs to a different building pack; regenerate the mesh"
        )
    assert struct.unpack_from("<I", lights, 12)[0] == models
    off = 16
    night = []
    for i in range(models):
        count = struct.unpack_from("<I", lights, off)[0]
        off += 4
        night.append(
            [struct.unpack_from("<6f", lights, off + j * 24) for j in range(count)]
        )
        off += count * 24
    assert off == len(lights)
    names = [
        "littleroot_house_w",
        "littleroot_house_e",
        "littleroot_lab",
        "oldale_house",
        "kit_house_4",
        "kit_house_5",
        "pokemon_center",
        "poke_mart",
        "gym",
        "flower_shop",
    ]
    labels = [
        "Littleroot · casa oeste",
        "Littleroot · casa este",
        "Littleroot · laboratorio",
        "Oldale · casa",
        "Casa · 4 celdas",
        "Casa · 5 celdas",
        "Centro Pokémon",
        "Tienda",
        "Gimnasio",
        "Floristería",
    ]
    canvas = Image.new("RGB", (960, 280 * 4), (24, 28, 42))
    draw = ImageDraw.Draw(canvas)
    used = set()
    lamp = Image.new("RGBA", (1, 1), (241, 183, 105, 255))
    for index, (name, label) in enumerate(zip(names, labels)):
        candidates = [
            i
            for i, r in rects.items()
            if sorted(r) == sorted(NIGHT_WINDOWS[name]) and i not in used
        ]
        assert candidates, name
        model = min(candidates)
        used.add(model)
        w, h, _, first, count, _ = model_table[model]
        pm = next(p for p in pm_table if p[0] == model)
        tw, th, offset = page_table[pm[1]]
        art = Image.new("RGBA", (w * 16, h * 16))
        px = art.load()
        for y in range(h * 16):
            for x in range(w * 16):
                tx = x + pm[2]
                ty = y + pm[3]
                if not (0 <= tx < tw and 0 <= ty < th):
                    continue
                t = struct.unpack_from("<H", raw, offset + 2 * texel(tx, ty, tw))[0]
                px[x, y] = (
                    int(((t >> 11) & 31) * 255 / 31 * 0.52),
                    int(((t >> 6) & 31) * 255 / 31 * 0.60),
                    int(((t >> 1) & 31) * 255 / 31 * 0.82),
                    255 * (t & 1),
                )
        cam = vb.Camera(
            (w / 2, 1.0, h / 2),
            pitch=40,
            distance=max(9, w * 1.6),
            width=320,
            height=240,
        )
        ras = vb.Raster(320, 240, bg=(24, 28, 42))
        for mesh, texture in [
            (packed_vertices[first : first + count], art),
            (night[model], lamp),
        ]:
            for k in range(0, len(mesh), 3):
                tri = []
                for x, y, z, u, v, _ in mesh[k : k + 3]:
                    sx, sy, depth, iw = cam.project((x, y, z))
                    if texture is lamp:
                        u = v = 0
                    tri.append((sx, sy, depth, iw, u * iw, v * iw))
                ras.draw(tri, texture, 1)
        ox = (index % 3) * 320
        oy = (index // 3) * 280
        canvas.paste(ras.image(), (ox, oy + 30))
        draw.text((ox + 12, oy + 10), label, fill=(237, 230, 211))
    draw.text(
        (12, 1110),
        "Preview por software · geometría real del paquete · sin bloom ni clima",
        fill=(180, 186, 198),
    )
    args.out.parent.mkdir(parents=True, exist_ok=True)
    canvas.save(args.out)
    print(args.out)


if __name__ == "__main__":
    main()
