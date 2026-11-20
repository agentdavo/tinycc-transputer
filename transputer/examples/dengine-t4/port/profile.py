#!/usr/bin/env python3
"""Summarise a PROFILE=N run of run-scene.sh: map t4's IPtr samples
(`S 0x...` lines in scene.out) to functions through the image's .tccsym
symbol map and print the share of samples per function.

  profile.py RUN_DIR [top]
"""
import bisect
import re
import sys
from pathlib import Path


def main():
    run = Path(sys.argv[1])
    top = int(sys.argv[2]) if len(sys.argv) > 2 else 30
    syms = []
    for line in (run / 'n0.elf.tccsym').read_text().splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[1] in 'Tt':
            syms.append((int(parts[0], 16), parts[2]))
    syms.sort()
    addrs = [a for a, _ in syms]
    counts = {}
    total = 0
    for m in re.finditer(r'^S 0x([0-9A-Fa-f]+)', (run / 'scene.out').read_text(errors='replace'), re.M):
        ip = int(m.group(1), 16)
        i = bisect.bisect_right(addrs, ip) - 1
        name = syms[i][1] if i >= 0 else '?'
        counts[name] = counts.get(name, 0) + 1
        total += 1
    if not total:
        print('no samples in %s/scene.out' % run)
        return 1
    print('%d samples' % total)
    for name, n in sorted(counts.items(), key=lambda kv: -kv[1])[:top]:
        print('%6.2f%% %6d  %s' % (100.0 * n / total, n, name))
    return 0


if __name__ == '__main__':
    sys.exit(main())
