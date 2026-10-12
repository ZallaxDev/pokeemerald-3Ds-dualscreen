#!/usr/bin/env python3
"""Find the faults of the voxel models without looking for them by eye.

The proofs of gen_voxel_buildings.py only see the GBA's 45-degree view: a
model can match its drawing pixel for pixel there and still look wrong from
the game's camera, because every face the drawing does not show (sides,
backs, undersides, the real depth) is invented. This scan measures every
triangle of every model placed in a layout and flags what goes wrong on
those invented faces:

    stretch   one texel smeared over several pixels in one direction (a
              column or a row pulled along a face): texels per pixel along
              the face's weaker axis below --stretch (default 0.4)
    ground    a face standing up (not a top) textured with the ground round
              the model (grass, paving, water): the drawing's ground leaking
              into a wall
    holes     a face with many transparent texels: seen through from the
              game's camera, though at 45 degrees something hid it
    steps     tiny facets: a curved object built in stair steps (boats,
              bushes); scored per model as small triangles per square pixel
              of surface
    thin      a part narrower than --thin pixels on the ground plan and
              taller than it is wide modelled as a box: from the game's
              camera its sides repeat its front (posts, legs)

    python voxel_defect_scan.py LAYOUT [--cache FILE] [--out DIR]
                                [--view name:cx,cz,pitch,yaw,dist ...]

Writes DIR/report.txt (every model, its flagged faces by kind and tag) and,
for every --view, DIR/<name>.png (the scene from the game's camera) and
DIR/<name>_flags.png (the same with every flagged face tinted: red stretch,
blue ground, magenta holes, yellow steps, cyan thin). With no --view, one
view per model placed, framed on it.

--cache keeps the built models (pickled) so that a second scan does not
rebuild them; delete it after changing a spec.
"""

import argparse
import json
import math
import os
import pickle
import sys
import types
from collections import Counter, defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_voxel_buildings as g  # noqa: E402
import voxel_building as vb  # noqa: E402
from PIL import Image  # noqa: E402

TINT = {"stretch": (255, 40, 40), "ground": (40, 120, 255), "holes": (255, 0, 255),
        "steps": (255, 220, 0), "thin": (0, 230, 230)}


def _sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def _cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def _dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def _len(a):
    return math.sqrt(_dot(a, a))


def tri_frame(tri):
    """The triangle's area (px^2), unit normal and the singular values of its
    texture map (texels per pixel along the face's two principal axes)."""
    p = [t[:3] for t in tri]
    uv = [t[3:5] for t in tri]
    e1, e2 = _sub(p[1], p[0]), _sub(p[2], p[0])
    n = _cross(e1, e2)
    area2 = _len(n)
    if area2 < 1e-9:
        return 0.0, (0.0, 1.0, 0.0), (0.0, 0.0)
    n = tuple(c / area2 for c in n)
    # an orthonormal frame on the face
    l1 = _len(e1)
    ax = tuple(c / l1 for c in e1)
    ay = _cross(n, ax)
    a = (l1, 0.0)
    b = (_dot(e2, ax), _dot(e2, ay))
    du1, dv1 = uv[1][0] - uv[0][0], uv[1][1] - uv[0][1]
    du2, dv2 = uv[2][0] - uv[0][0], uv[2][1] - uv[0][1]
    det = a[0] * b[1] - a[1] * b[0]
    if abs(det) < 1e-9:
        return area2 / 2, n, (0.0, 0.0)
    # J = dUV / dXY on the face
    j11 = (du1 * b[1] - du2 * a[1]) / det
    j12 = (du2 * a[0] - du1 * b[0]) / det
    j21 = (dv1 * b[1] - dv2 * a[1]) / det
    j22 = (dv2 * a[0] - dv1 * b[0]) / det
    # singular values of a 2x2 matrix
    s = j11 * j11 + j12 * j12 + j21 * j21 + j22 * j22
    d = abs(j11 * j22 - j12 * j21)
    r = math.sqrt(max(s * s / 4 - d * d, 0.0))
    s1 = math.sqrt(max(s / 2 + r, 0.0))
    s2 = math.sqrt(max(s / 2 - r, 0.0))
    return area2 / 2, n, (s1, s2)


def tri_texels(tri, art_px, size, samples=6):
    """The art's texels the triangle covers, sampled on a barycentric grid."""
    W, H = size
    out = []
    for i in range(samples + 1):
        for j in range(samples + 1 - i):
            a, b = (i + 0.5) / (samples + 1.5), (j + 0.5) / (samples + 1.5)
            c = 1 - a - b
            u = a * tri[0][3] + b * tri[1][3] + c * tri[2][3]
            v = a * tri[0][4] + b * tri[1][4] + c * tri[2][4]
            tu, tv = int(math.floor(u)), int(math.floor(v))
            if 0 <= tu < W and 0 <= tv < H:
                out.append(art_px[tu, tv])
            else:
                out.append((0, 0, 0, 0))
    return out


def ground_colours(layout, taken):
    """The colours of the layout's own ground: every cell no model takes
    and nothing stands on (no upper layer: a tree's crown is not ground),
    its commonest colours (grass, paving, sand, water)."""
    count = Counter()
    for y in range(layout.h):
        for x in range(layout.w):
            m = layout.metatile(x, y)
            if (x, y) in taken or any(i for block in layout.subtiles(m, 1) for _, i in block):
                continue
            for block in layout.subtiles(m, 0):
                for rgb, _ in block:
                    count[rgb] += 1
    total = sum(count.values())
    keep, acc = set(), 0
    for c, k in count.most_common():
        if acc > 0.85 * total:
            break
        keep.add(c)
        acc += k
    return keep


def scan_model(m, ground, opt):
    """Flags for every triangle of a model: {index: set(kinds)}, and its
    step score."""
    flags = defaultdict(set)
    art_px = m.art.load()
    size = m.art.size
    small, surface = 0, 0.0
    by_tag = defaultdict(list)
    for k, (tri, shade, tag) in enumerate(m.mesh.tris):
        if "~depth" in tag or "~behind" in tag:
            continue
        area, n, (s1, s2) = tri_frame(tri)
        if area <= 0:
            continue
        surface += area
        if area < opt.tiny:
            small += 1
        by_tag[tag.split("~")[0]].append(k)
        upright = abs(n[1]) < 0.7
        # flat fills (one colour on purpose: a Tent's hips) have no texture
        # map at all, so they are not stretched
        if s1 > 1e-3 and s2 < opt.stretch:
            flags[k].add("stretch")
        tex = tri_texels(tri, art_px, size)
        opaque = [t for t in tex if t[3] >= 128]
        if tex and len(opaque) < 0.7 * len(tex) and len(opaque) > 0 and upright:
            flags[k].add("holes")
        if upright and opaque and \
                sum(1 for t in opaque if t[:3] in ground) > 0.5 * len(opaque):
            flags[k].add("ground")
    steps = small / max(surface / 256.0, 1e-6)    # small facets per cell of surface
    if steps > opt.steps:
        for k, (tri, _, _) in enumerate(m.mesh.tris):
            if tri_frame(tri)[0] < opt.tiny:
                flags[k].add("steps")
    # thin parts: per tag, the ground-plan box of each connected group
    for tag, ks in by_tag.items():
        for group in _groups(m.mesh.tris, ks):
            xs = [p[0] for k in group for p in m.mesh.tris[k][0]]
            ys = [p[1] for k in group for p in m.mesh.tris[k][0]]
            zs = [p[2] for k in group for p in m.mesh.tris[k][0]]
            wx, wz, hy = max(xs) - min(xs), max(zs) - min(zs), max(ys) - min(ys)
            normals = {_axis(tri_frame(m.mesh.tris[k][0])[1]) for k in group}
            if (min(wx, wz) < opt.thin and max(wx, wz) < 2 * opt.thin and hy > max(wx, wz)
                    and len(normals & {"x", "z"}) == 2):
                for k in group:
                    flags[k].add("thin")
    return flags, steps


def _axis(n):
    a = max(range(3), key=lambda i: abs(n[i]))
    return "xyz"[a]


def _groups(tris, ks):
    """Triangles of `ks` joined by shared vertices."""
    key = lambda p: (round(p[0], 2), round(p[1], 2), round(p[2], 2))  # noqa: E731
    parent = {k: k for k in ks}

    def find(a):
        while parent[a] != a:
            parent[a] = parent[parent[a]]
            a = parent[a]
        return a
    seen = {}
    for k in ks:
        for p in tris[k][0]:
            q = key(p)
            if q in seen:
                parent[find(k)] = find(seen[q])
            else:
                seen[q] = k
    out = defaultdict(list)
    for k in ks:
        out[find(k)].append(k)
    return list(out.values())


def render(cam, ground_item, placed, scale=2):
    """The scene, and the same with the flagged faces tinted."""
    ras = vb.Raster(cam.width * scale, cam.height * scale)
    cam2 = vb.Camera(cam.target, cam.pitch, cam.yaw, cam.distance, cam.fov,
                     cam.width * scale, cam.height * scale)

    def draw(tris, tex, owner_of):
        for k, (tri, shade) in enumerate(tris):
            vs = []
            for (x, y, z, u, v) in tri:
                pr = cam2.project((x, y, z))
                if pr is None:
                    break
                sx, sy, d, iw = pr
                vs.append((sx, sy, d, iw, u * iw, v * iw))
            else:
                ras.draw(vs, tex, shade, owner_of(k))
    draw(ground_item[0], ground_item[1], lambda k: None)
    for i, (tris, tex, flags) in enumerate(placed):
        draw(tris, tex, lambda k, i=i: (i, k))
    plain = ras.image()
    tinted = []
    for idx, col in enumerate(ras.color):
        o = ras.owner[idx]
        kinds = o and placed[o[0]][2].get(o[1])
        if kinds:
            t = TINT[sorted(kinds, key=list(TINT).index)[0]]
            col = tuple((c + 2 * tc) // 3 for c, tc in zip(col, t))
        tinted.append(col)
    img = Image.new("RGB", (ras.w, ras.h))
    img.putdata(tinted)
    return plain, img


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("layout")
    ap.add_argument("--cache", default=None)
    ap.add_argument("--out", default=os.path.join(g.PORT, "build", "defects"))
    ap.add_argument("--view", action="append", default=[])
    ap.add_argument("--stretch", type=float, default=0.4)
    ap.add_argument("--tiny", type=float, default=3.0)
    ap.add_argument("--steps", type=float, default=6.0)
    ap.add_argument("--thin", type=float, default=8.0)
    ap.add_argument("--only", default=None, help="comma-separated model name prefixes")
    opt = ap.parse_args()
    os.makedirs(opt.out, exist_ok=True)
    g.DIAGNOSTICS = opt.out
    layout = vb.LayoutArt(opt.layout)
    key = (opt.layout, opt.only)
    if opt.cache and os.path.exists(opt.cache):
        with open(opt.cache, "rb") as f:
            cached = pickle.load(f)
        if cached[0] != key:
            cached = None
    else:
        cached = None
    if cached is None:
        lj = json.load(open(os.path.join(vb.ROOT, "data", "layouts", "layouts.json"),
                            encoding="utf-8"))["layouts"]
        found = []
        for m in g.build_models():
            if opt.only and not any(m.name.startswith(p) for p in opt.only.split(",")):
                continue
            at = [(px, py, gm) for _, px, py, gm, name, odd in g.find_placements(m, lj)
                  if name == opt.layout]
            if at:
                # what the scan needs of a model, without its spec (lambdas)
                slim = types.SimpleNamespace(name=m.name, cells=m.cells, art=m.art,
                                             mesh=types.SimpleNamespace(tris=m.mesh.tris))
                found.append((slim, at))
        cached = (key, found)
        if opt.cache:
            with open(opt.cache, "wb") as f:
                pickle.dump(cached, f)
    override, placements, taken = {}, [], set()
    for m, at in cached[1]:
        w, h = m.cells
        for px, py, gm in at:
            for j in range(h):
                for i in range(w):
                    taken.add((px + i, py + j))
                    if gm != g.OWN_GROUND:
                        override[(px + i, py + j)] = gm
            placements.append((m, px, py))
    ground = ground_colours(layout, taken)
    report, placed, scanned = [], [], {}
    for m, px, py in placements:
        if m.name not in scanned:
            scanned[m.name] = scan_model(m, ground, opt)
        flags, steps = scanned[m.name]
        placed.append((vb.model_world_tris(m, px, py), m.art, flags))
    for name, (flags, steps) in sorted(scanned.items()):
        kinds = Counter(k for ks in flags.values() for k in ks)
        if not kinds:
            continue
        m = next(m for m, _, _ in placements if m.name == name)
        tags = defaultdict(Counter)
        for k, ks in flags.items():
            for kind in ks:
                tags[kind][m.mesh.tris[k][2]] += 1
        at = ["%d,%d" % (px, py) for mm, px, py in placements if mm.name == name]
        report.append("%s  at %s  steps %.1f" % (name, " ".join(at[:6]), steps))
        for kind, n in kinds.most_common():
            report.append("    %-8s %4d tris: %s" % (kind, n, ", ".join(
                "%s(%d)" % t for t in tags[kind].most_common(5))))
    with open(os.path.join(opt.out, "report.txt"), "w", encoding="utf-8") as f:
        f.write("\n".join(report) + "\n")
    print("\n".join(report))
    gt, gtex = vb.ground_tris(layout, 0, 0, layout.w, layout.h, override, None)
    views = opt.view
    if not views:
        done = set()
        for m, px, py in placements:
            if m.name in done or not scanned[m.name][0]:
                continue
            done.add(m.name)
            w, h = m.cells
            views.append("%s:%.1f,%.1f,40,0,%.1f" % (m.name, px + w / 2, py + h / 2,
                                                       max(4.0, 1.4 * max(w, h))))
    for spec in views:
        name, rest = spec.split(":")
        cx, cz, pitch, yaw, dist = map(float, rest.split(","))
        cam = vb.Camera((cx, 0.0, cz), pitch=pitch, yaw=yaw, distance=dist)
        plain, tinted = render(cam, (gt, gtex), placed)
        plain.save(os.path.join(opt.out, name + ".png"))
        tinted.save(os.path.join(opt.out, name + "_flags.png"))
        print(os.path.join(opt.out, name + "_flags.png"))


if __name__ == "__main__":
    main()
