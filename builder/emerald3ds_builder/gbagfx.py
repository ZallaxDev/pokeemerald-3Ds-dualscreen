"""pret's gbagfx conversions, byte for byte, for files that are not in the ROM.

The data pack holds some files the player's ROM cannot give: aarant's
follower sprites and Gen 6 icons, fetched from GitHub at a
pinned commit (externals.py). The build converted them with pret's gbagfx
(tools/gbagfx); the builder converts the same sources the same way here, and
each result is checked against the CRC the build recorded.

Covered: PNG to 1/4/8bpp tiles (-mwidth, -mheight, -num_tiles) and plain
(-plain, -data_width), PNG and JASC palettes to .gbapal (-num_colors), LZ77
(-overflow, -search). The PNG reader is our own: gbagfx reads raw samples
(libpng, no transforms), which Pillow would rescale for grayscale images.
"""

from __future__ import annotations

import struct
import zlib


class GfxError(Exception):
    pass


# ── PNG ──────────────────────────────────────────────────────────────────────

def _unfilter(raw: bytes, width: int, height: int, bit_depth: int, channels: int) -> bytes:
    bpp = max(1, bit_depth * channels // 8)          # filter unit, bytes
    stride = (width * bit_depth * channels + 7) // 8
    out = bytearray(stride * height)
    prev = bytearray(stride)
    pos = 0
    for y in range(height):
        kind = raw[pos]
        line = bytearray(raw[pos + 1:pos + 1 + stride])
        pos += 1 + stride
        if kind == 1:
            for i in range(bpp, stride):
                line[i] = (line[i] + line[i - bpp]) & 0xFF
        elif kind == 2:
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 0xFF
        elif kind == 3:
            for i in range(stride):
                left = line[i - bpp] if i >= bpp else 0
                line[i] = (line[i] + ((left + prev[i]) >> 1)) & 0xFF
        elif kind == 4:
            for i in range(stride):
                a = line[i - bpp] if i >= bpp else 0
                b = prev[i]
                c = prev[i - bpp] if i >= bpp else 0
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pred = a if pa <= pb and pa <= pc else (b if pb <= pc else c)
                line[i] = (line[i] + pred) & 0xFF
        elif kind != 0:
            raise GfxError("bad PNG filter %d" % kind)
        out[y * stride:(y + 1) * stride] = line
        prev = line
    return bytes(out)


class Png:
    """The parts of a PNG gbagfx reads: size, bit depth, colour type, the
    packed samples (rows of libpng's rowbytes) and the PLTE colours."""

    def __init__(self, data: bytes):
        if data[:8] != b"\x89PNG\r\n\x1a\n":
            raise GfxError("not a PNG")
        pos, idat, self.palette = 8, bytearray(), []
        while pos < len(data):
            length, kind = struct.unpack(">I4s", data[pos:pos + 8])
            body = data[pos + 8:pos + 8 + length]
            pos += 12 + length
            if kind == b"IHDR":
                (self.width, self.height, self.bit_depth, self.color_type,
                 _, _, interlace) = struct.unpack(">IIBBBBB", body)
                if interlace:
                    raise GfxError("interlaced PNG")
            elif kind == b"PLTE":
                self.palette = [tuple(body[i:i + 3]) for i in range(0, len(body) - 2, 3)]
            elif kind == b"IDAT":
                idat += body
            elif kind == b"IEND":
                break
        channels = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[self.color_type]
        self.samples = _unfilter(zlib.decompress(bytes(idat)), self.width, self.height,
                                 self.bit_depth, channels)


def _convert_bit_depth(src: bytes, src_depth: int, dest_depth: int, num_pixels: int) -> bytes:
    """gbagfx ConvertBitDepth: each sample keeps its low dest_depth bits."""
    src_size = (num_pixels * src_depth + 7) // 8
    out = bytearray((num_pixels * dest_depth + 7) // 8)
    d, dest_bit = 0, 8 - dest_depth
    for i in range(src_size):
        byte = src[i]
        for j in range(8 - src_depth, -1, -src_depth):
            pixel = ((byte >> j) % (1 << src_depth)) % (1 << dest_depth)
            out[d] |= (pixel << dest_bit) & 0xFF
            dest_bit -= dest_depth
            if dest_bit < 0:
                d += 1
                dest_bit = 8 - dest_depth
    return bytes(out)


def _read_png(data: bytes, bit_depth: int):
    png = Png(data)
    if png.color_type not in (0, 3):
        raise GfxError("unsupported PNG colour type %d" % png.color_type)
    pixels = png.samples
    if png.bit_depth != bit_depth:
        pixels = _convert_bit_depth(pixels, png.bit_depth, bit_depth, png.width * png.height)
    return png, pixels


# ── tiles ────────────────────────────────────────────────────────────────────

def _tile_order(num_tiles, metatiles_wide, mw, mh):
    sub_x = sub_y = meta_x = meta_y = 0
    for _ in range(num_tiles):
        yield meta_x * mw + sub_x, meta_y * mh + sub_y
        sub_x += 1
        if sub_x == mw:
            sub_x = 0
            sub_y += 1
            if sub_y == mh:
                sub_y = 0
                meta_x += 1
                if meta_x == metatiles_wide:
                    meta_x = 0
                    meta_y += 1


def png_to_tiles(data: bytes, bit_depth: int, mwidth: int = 1, mheight: int = 1,
                 num_tiles: int = 0, num_tiles_mode: str = "ignore") -> bytes:
    png, pixels = _read_png(data, bit_depth)
    invert = png.color_type != 3
    if png.width % 8 or png.height % 8:
        raise GfxError("size not a multiple of 8")
    tiles_w, tiles_h = png.width // 8, png.height // 8
    if tiles_w % mwidth or tiles_h % mheight:
        raise GfxError("size not a multiple of the metatile size")
    max_tiles = tiles_w * tiles_h
    if num_tiles == 0:
        num_tiles = max_tiles
    elif num_tiles > max_tiles:
        raise GfxError("-num_tiles larger than the image")
    pitch = tiles_w * bit_depth          # bytes per pixel row
    out = bytearray()
    for tx, ty in _tile_order(max_tiles, tiles_w // mwidth, mwidth, mheight):
        for j in range(8):
            row = (ty * 8 + j) * pitch
            if bit_depth == 4:
                for k in range(4):
                    pair = pixels[row + tx * 4 + k]
                    left, right = pair >> 4, pair & 0xF
                    if invert:
                        left, right = 15 - left, 15 - right
                    out.append((right << 4) | left)
            elif bit_depth == 8:
                for k in range(8):
                    p = pixels[row + tx * 8 + k]
                    out.append(255 - p if invert else p)
            else:
                octet, value = pixels[row + tx], 0
                for _ in range(8):
                    value = ((value << 1) | ((octet & 1) ^ invert)) & 0xFF
                    octet >>= 1
                out.append(value)
    tile_size = bit_depth * 8
    size = num_tiles * tile_size
    if any(out[size:]):
        if num_tiles_mode == "error":
            raise GfxError("tiles past -num_tiles are not empty")
        if num_tiles_mode == "warn":
            return bytes(out)
    return bytes(out[:size])


def png_to_plain(data: bytes, bit_depth: int, data_width: int = 1) -> bytes:
    png, pixels = _read_png(data, bit_depth)
    invert = png.color_type != 3
    size = (png.width * png.height * bit_depth + 7) // 8
    out = bytearray(size)
    for i in range(0, size, data_width):
        chunk = pixels[i:i + data_width]
        if invert:
            chunk = bytes(b ^ 0xFF for b in chunk)
        out[i:i + data_width] = chunk[::-1]    # little-endian units
    return bytes(out)


# ── palettes ─────────────────────────────────────────────────────────────────

def _gbapal(colors) -> bytes:
    return b"".join(struct.pack("<H", ((b // 8) << 10) | ((g // 8) << 5) | (r // 8)) for r, g, b in colors)


def png_to_gbapal(data: bytes) -> bytes:
    return _gbapal(Png(data).palette)


def jasc_to_gbapal(data: bytes, num_colors: int = 0) -> bytes:
    lines = data.replace(b"\r\n", b"\n").split(b"\n")
    if lines[0] != b"JASC-PAL" or lines[1] != b"0100":
        raise GfxError("not a JASC-PAL palette")
    count = int(lines[2])
    colors = [tuple(int(x) for x in lines[3 + i].split(b" ")) for i in range(count)]
    if num_colors:
        colors = (colors + [(0, 0, 0)] * num_colors)[:num_colors]
    return _gbapal(colors)


# ── LZ77 ─────────────────────────────────────────────────────────────────────

def lz_compress(src: bytes, min_distance: int = 2, overflow: int = 0) -> bytes:
    """gbagfx LZCompress: at each position the longest match (up to 18) at the
    smallest distance in [min_distance, 0x1000]. Candidates are found through
    their first three bytes; a shorter match is a literal anyway."""
    size = len(src)
    src = src + bytes(overflow)
    total = size + overflow
    if total <= 0:
        raise GfxError("nothing to compress")
    out = bytearray(b"\x10" + struct.pack("<I", total)[:3])
    chains: dict[bytes, list[int]] = {}
    indexed = 0
    pos = 0
    while True:
        flags_at = len(out)
        out.append(0)
        for i in range(8):
            # Index every start below pos (candidates lie behind it).
            while indexed < pos:
                if indexed + 3 <= total:
                    chains.setdefault(src[indexed:indexed + 3], []).append(indexed)
                indexed += 1
            best_len, best_dist = 0, 0
            if pos + 3 <= total:
                for start in reversed(chains.get(src[pos:pos + 3], ())):
                    dist = pos - start
                    if dist < min_distance:
                        continue
                    if dist > 0x1000:
                        break
                    n = 3
                    while n < 18 and pos + n < total and src[start + n] == src[pos + n]:
                        n += 1
                    if n > best_len:
                        best_len, best_dist = n, dist
                        if n == 18:
                            break
            if best_len >= 3:
                out[flags_at] |= 0x80 >> i
                pos += best_len
                best_len -= 3
                best_dist -= 1
                out.append((best_len << 4) | (best_dist >> 8))
                out.append(best_dist & 0xFF)
            else:
                out.append(src[pos])
                pos += 1
            if pos == total:
                out += bytes(-len(out) % 4)
                out[1:4] = struct.pack("<I", size)[:3]
                return bytes(out)


# ── a gbagfx command line ────────────────────────────────────────────────────

def convert(source: bytes, source_ext: str, target_ext: str, args: list[str]) -> bytes:
    """gbagfx <in.source_ext> <out.target_ext> args, on bytes."""
    opts = {}
    it = iter(args)
    for a in it:
        if a in ("-mwidth", "-mheight", "-num_tiles", "-data_width", "-num_colors", "-overflow", "-search"):
            opts[a] = int(next(it))
        elif a in ("-plain", "-Wnum_tiles", "-Werror=num_tiles"):
            opts[a] = True
        else:
            raise GfxError("unsupported gbagfx option %s" % a)
    if target_ext == "lz":
        return lz_compress(source, opts.get("-search", 2), opts.get("-overflow", 0))
    if source_ext == "png" and target_ext in ("1bpp", "4bpp", "8bpp"):
        depth = int(target_ext[0])
        if opts.get("-plain"):
            return png_to_plain(source, depth, opts.get("-data_width", 1))
        mode = "error" if opts.get("-Werror=num_tiles") else "warn" if opts.get("-Wnum_tiles") else "ignore"
        return png_to_tiles(source, depth, opts.get("-mwidth", 1), opts.get("-mheight", 1),
                            opts.get("-num_tiles", 0), mode)
    if target_ext == "gbapal" and source_ext == "png":
        return png_to_gbapal(source)
    if target_ext == "gbapal" and source_ext == "pal":
        return jasc_to_gbapal(source, opts.get("-num_colors", 0))
    raise GfxError("unsupported conversion %s -> %s" % (source_ext, target_ext))
