#!/usr/bin/env python3
"""Image tools for the dEngine port.

  compare_frames.py host.ppm screenshot.bmp   compare a host frame (portrait
      320x480 PPM) with a T4 capture (the landscape display, the portrait
      buffer turned left); exit 0 when every pixel matches
  compare_frames.py --png in.(ppm|bmp) out.png [--rotate]   write a PNG
"""
import struct
import sys
import zlib
from pathlib import Path


def read_ppm(path):
    data = Path(path).read_bytes()
    parts = data.split(b'\n', 3)
    w, h = map(int, parts[1].split())
    px = parts[3]
    return w, h, [px[y * w * 3:(y + 1) * w * 3] for y in range(h)]


def read_bmp(path):
    data = Path(path).read_bytes()
    offset = struct.unpack_from('<I', data, 10)[0]
    w, h = struct.unpack_from('<ii', data, 18)
    bpp = struct.unpack_from('<H', data, 28)[0]
    step = bpp // 8
    stride = (w * step + 3) & ~3
    rows = []
    for y in range(abs(h)):
        src = y if h < 0 else abs(h) - 1 - y
        row = data[offset + src * stride: offset + src * stride + w * step]
        rows.append(b''.join(bytes((row[x * step + 2], row[x * step + 1], row[x * step])) for x in range(w)))
    return w, abs(h), rows


def read_image(path):
    return read_bmp(path) if str(path).lower().endswith('.bmp') else read_ppm(path)


def turn_left(w, h, rows):
    """Portrait (x, y) -> landscape (y, w-1-x), as node.c presents it."""
    return h, w, [b''.join(rows[x][(w - 1 - y) * 3:(w - y) * 3] for x in range(h)) for y in range(w)]


def write_png(path, w, h, rows):
    raw = b''.join(b'\0' + r for r in rows)

    def chunk(t, b):
        return struct.pack('>I', len(b)) + t + b + struct.pack('>I', zlib.crc32(t + b) & 0xffffffff)
    Path(path).write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) +
                           chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))


def main():
    args = sys.argv[1:]
    if args and args[0] == '--png':
        w, h, rows = read_image(args[1])
        if '--rotate' in args:
            w, h, rows = turn_left(w, h, rows)
        write_png(args[2], w, h, rows)
        return 0
    if len(args) != 2:
        print(__doc__)
        return 2
    w, h, rows = turn_left(*read_ppm(args[0]))
    bw, bh, brows = read_bmp(args[1])
    if (w, h) != (bw, bh):
        print('size differs: host %dx%d, capture %dx%d' % (w, h, bw, bh))
        return 1
    diff = sum(1 for y in range(h) for x in range(w) if rows[y][x * 3:x * 3 + 3] != brows[y][x * 3:x * 3 + 3])
    if diff:
        print('%d of %d pixels differ' % (diff, w * h))
        return 1
    print('host frame and T4 capture match (%dx%d)' % (w, h))
    return 0


if __name__ == '__main__':
    sys.exit(main())
