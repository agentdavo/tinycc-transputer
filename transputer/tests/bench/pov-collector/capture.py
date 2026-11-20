#!/usr/bin/env python3
"""Tap the root FRead→Cache row messages of the packaged POV-Ray BTL."""
import argparse
from pathlib import Path
import re
import struct

WIDTH, HEIGHT = 640, 480
PACKET = 9 + 4 * WIDTH


def instrument(path, map_file):
    text = map_file.read_text()
    ranges = re.findall(r"Code from 'fread\.c8x'[^\n]*\n\s*#([0-9A-F]+)\s+#([0-9A-F]+)", text)
    if len(ranges) != 3 or len(set(ranges)) != 1:
        raise ValueError('expected three root FRead processes sharing one code range')
    start, stop = ranges[0]
    source = path.read_text()
    marker = '                           PROFILE(profile[PRO_CHANOUT] += AReg);'
    if source.count(marker) != 1:
        raise ValueError('T4 OUT dispatch has changed')
    tap = f'''                           /* Capture a complete RGB row before the soft OUT. */
                           if (nodeid == 0 && IPtr >= 0x{start}u && IPtr < 0x{stop}u &&
                               AReg == {PACKET} && byte(CReg) == 128) {{
                                const char *name = getenv("POV_COLLECTOR_CAPTURE");
                                if (name && *name) {{
                                     static FILE *capture;
                                     unsigned char row[4 + {PACKET}];
                                     unsigned i;
                                     for (i = 0; i < 4; i++)
                                          row[i] = BReg >> (i * 8);
                                     for (i = 0; i < {PACKET}; i++)
                                          row[i + 4] = byte(CReg + i);
                                     if (!capture)
                                          capture = fopen(name, "wb");
                                     if (!capture || fwrite(row, 1, sizeof row, capture) != sizeof row ||
                                         fflush(capture)) {{
                                          perror(name);
                                          exit(1);
                                     }}
                                }}
                           }}
'''
    path.write_text(source.replace(marker, tap + marker))


def normalize(raw, packets, picture):
    data = raw.read_bytes()
    if len(data) != HEIGHT * (PACKET + 4):
        raise ValueError(f'expected {HEIGHT} captured rows, got {len(data)} bytes')
    channels, rows = {}, {}
    stream = bytearray()
    for i in range(0, len(data), PACKET + 4):
        channel, tag, width, y = struct.unpack_from('<IBII', data, i)
        if tag != 128 or width != WIDTH or y >= HEIGHT or y in rows:
            raise ValueError(f'invalid captured packet {tag=}, {width=}, {y=}')
        slot = channels.setdefault(channel, len(channels))
        stream.append(slot)
        stream.extend(data[i + 4:i + 4 + PACKET])
        rows[y] = b''.join(data[j:j+3] for j in range(i + 13, i + 4 + PACKET, 4))
    if len(channels) != 3:
        raise ValueError(f'expected three FRead channels, got {len(channels)}')
    expected = f'P6\n{WIDTH} {HEIGHT}\n255\n'.encode() + b''.join(rows[y] for y in range(HEIGHT))
    if picture.read_bytes() != expected:
        raise ValueError('captured RGB data differs from the historical collector image')
    packets.write_bytes(stream)
    print(f'{packets}: {HEIGHT} original row packets on {len(channels)} channels')


if __name__ == '__main__':
    cli = argparse.ArgumentParser(description=__doc__)
    sub = cli.add_subparsers(dest='action', required=True)
    p = sub.add_parser('instrument')
    p.add_argument('source', type=Path)
    p.add_argument('map', type=Path)
    p = sub.add_parser('normalize')
    for name in ('raw', 'packets', 'picture'):
        p.add_argument(name, type=Path)
    args = cli.parse_args()
    if args.action == 'instrument':
        instrument(args.source, args.map)
    else:
        normalize(args.raw, args.packets, args.picture)
