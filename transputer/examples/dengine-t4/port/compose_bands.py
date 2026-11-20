#!/usr/bin/env python3
"""Compose per-node host frames (host_main --nodes N --node k) and compare
the result with a one-node frame. Each node's PPM holds only its own
interleaved bands; display row r belongs to node (r // band) % nodes."""
import sys
from pathlib import Path


def read_ppm(path):
    data = Path(path).read_bytes()
    parts = data.split(b'\n', 3)
    w, h = map(int, parts[1].split())
    return w, h, parts[3]


def main():
    if len(sys.argv) < 5:
        print('usage: compose_bands.py reference.ppm band nodes node0.ppm ...')
        return 2
    ref, band, nodes = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
    parts = sys.argv[4:]
    w, h, expected = read_ppm(ref)
    images = [read_ppm(p)[2] for p in parts]
    row = w * 3
    out = bytearray(len(expected))
    for r in range(h):
        k = (r // band) % nodes
        out[r * row:(r + 1) * row] = images[k][r * row:(r + 1) * row]
    if bytes(out) != expected:
        bad = [r for r in range(h) if out[r * row:(r + 1) * row] != expected[r * row:(r + 1) * row]]
        print('MISMATCH: %d rows differ, first %s' % (len(bad), bad[:10]))
        return 1
    print('composed %d-node frame matches the one-node frame' % nodes)
    return 0


if __name__ == '__main__':
    sys.exit(main())
