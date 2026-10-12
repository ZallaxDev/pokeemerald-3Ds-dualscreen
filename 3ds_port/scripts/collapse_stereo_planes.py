#!/usr/bin/env python3
"""Collapse EM3DSTM1 depth packs without changing silhouettes or frame layout."""
import argparse
import json

from stereo_depth_groups import smart_pixels
from pathlib import Path
import struct


def collapse_pixels(data, source, target):
    if not 2 <= target <= source <= 15:
        raise ValueError('expected 2 <= target planes <= source planes <= 15')
    def plane(p):
        if p == 15:
            return p
        if p >= source:
            raise ValueError(f'invalid plane {p} for {source}-plane source')
        return p * target // source
    return bytes(plane(b & 15) | (plane(b >> 4) << 4) for b in data)


def collapse_pack(data, target=4, annotations=None, report=None, smart=False, cleanup=False):
    if len(data) < 16 or data[:8] != b'EM3DSTM1':
        raise ValueError('not an EM3DSTM1 pack')
    count, source, frames, w, h = struct.unpack_from('<HBBHH', data, 8)
    start = 16 + count * 8
    if (w, h) != (64, 64) or not 1 <= frames <= 15 or start > len(data):
        raise ValueError('invalid header or truncated index')
    # A pack already at (or under) the target is finished: passed through as
    # it is, so a reviewed four-plane pack is not cleaned a second time.
    if source <= target:
        return bytes(data)
    # Already simpler packs stay simpler; never invent extra planes.
    target = min(target, source)
    collapse_pixels(b'', source, target)
    out = bytearray(data)
    out[10] = target
    ranges = []
    ids = set()
    for i in range(count):
        ident, entry_frames, offset = struct.unpack_from('<HHI', data, 16 + i * 8)
        if ident in ids:
            raise ValueError('duplicate sprite id')
        ids.add(ident)
        n = entry_frames if 0 < entry_frames <= frames else frames
        end = offset + n * 2048
        if offset < start or end > len(data):
            raise ValueError(f'invalid frame range for {ident}')
        ranges.append((offset, end))
        if smart:
            anatomy = (annotations or {}).get(str(ident))
            converted, entry_report = smart_pixels(data[offset:end], source, target, anatomy, cleanup)
            out[offset:end] = converted
            if report is not None:
                report.append(dict(id=ident, **entry_report))
        else:
            out[offset:end] = collapse_pixels(data[offset:end], source, target)
    ranges.sort()
    if any(a[1] > b[0] for a, b in zip(ranges, ranges[1:])):
        raise ValueError('overlapping entries')
    return bytes(out)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--planes', type=int, choices=range(2, 16), default=4)
    parser.add_argument('--smart', action='store_true', help='cluster coherent parts; depth-only fallback without annotations')
    parser.add_argument('--cleanup', action='store_true', help='clean small depth fragments on unannotated drafts; requires --smart')
    parser.add_argument('--annotations', type=Path, help='JSON object mapping sprite IDs to anatomy JSON paths')
    parser.add_argument('--report', type=Path, help='write per-sprite grouping and review status')
    args = parser.parse_args()
    if args.cleanup and not args.smart:
        parser.error('--cleanup requires --smart')
    data = args.input.read_bytes()
    annotations = {}
    if args.annotations:
        if not args.smart:
            parser.error('--annotations requires --smart')
        for ident, name in json.loads(args.annotations.read_text()).items():
            path = Path(name)
            if not path.is_absolute():
                path = args.annotations.parent / path
            annotations[ident] = json.loads(path.read_text())
    report = []
    result = collapse_pack(data, args.planes, annotations, report, args.smart, args.cleanup)
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(report, indent=2) + '\n')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(result)
    print(f'{struct.unpack_from("<H", result, 8)[0]} sprites: {data[10]} -> {result[10]} planes -> {args.output}')


if __name__ == '__main__':
    main()
