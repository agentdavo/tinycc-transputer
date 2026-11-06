#!/usr/bin/env python3
"""Reject incomplete Doom benchmark runs and mismatched result hashes."""
import re
import sys
from pathlib import Path

PHASES = ("column", "tcolumn", "span", "segloop", "fixedmul", "fixeddiv",
          "palblit", "rowcopy", "fill", "masked", "mapplane")
EXPECTED = ("b6eadcb0", "5a9ca119", "05d26359", "6b9d321f", "58d667ae",
            "63da69d9", "23091322", "23091322", "a9ac55c5", "9c0f0700",
            "e3973655")


def read_run(path):
    text = Path(path).read_text().replace('\0', '')
    if not text.startswith('S') or not text.endswith('H'):
        raise ValueError(f"{path}: missing start or halt marker")
    lines = text[1:-1].splitlines()
    if len(lines) != len(PHASES) + 2 or lines[-2:] != ['check 0 0', 'done']:
        raise ValueError(f"{path}: incomplete run, failed check or unexpected output")
    values = []
    for name, line in zip(PHASES, lines):
        match = re.fullmatch(r'(\w+) (\d+) ([0-9a-f]{8})', line)
        if not match or match[1] != name:
            raise ValueError(f"{path}: invalid {name} result: {line!r}")
        values.append((int(match[2]), match[3]))
    return values


def main():
    cpu, cycles_path, instructions_path = sys.argv[1:]
    cycles, instructions = read_run(cycles_path), read_run(instructions_path)
    if any(a[1] != b[1] for a, b in zip(cycles, instructions)):
        raise ValueError("cycle and instruction runs produced different hashes")
    if tuple(digest for _, digest in cycles) != EXPECTED:
        raise ValueError("benchmark phase hashes differ from the reference")
    print(f'{cpu:9} {"cycles":>12} {"instructions":>12}  hash')
    for name, (count, digest), (insns, _) in zip(PHASES, cycles, instructions):
        print(f'{name:9} {count:12} {insns:12}  {digest}')
    print('check 0 0\ndone')


if __name__ == '__main__':
    try:
        main()
    except ValueError as exc:
        sys.exit(str(exc))
