#!/usr/bin/env python3
"""Histogram of T9000-grouper-friendly patterns in a flat vmlinux / ELF.

Estimates how well the kernel image can feed the RTL grouper/pipe:
  - adjacent ldl;ldl (dual-local candidate)
  - ldl;ldl + wsub (addr pack)
  - ldl;ldl;wsub;ldnl (textbook pack)
  - ldnlp density (Addr-stage pointer math)
  - primary high-nibble mix (noisy but directional)

Usage:
  python3 transputer/scripts/group_histogram.py [path/to/vmlinux]
"""
from __future__ import annotations

import struct
import sys
from collections import Counter
from pathlib import Path


def load_code(path: Path) -> bytes:
    data = path.read_bytes()
    if data[:4] != b"\x7fELF":
        return data
    if data[4] != 1:
        raise SystemExit("need ELF32")
    e_phoff = struct.unpack_from("<I", data, 28)[0]
    e_phentsize, e_phnum = struct.unpack_from("<HH", data, 42)
    for i in range(e_phnum):
        off = e_phoff + i * e_phentsize
        p_type, p_offset, _v, _p, p_filesz, _m, p_flags, _a = struct.unpack_from(
            "<IIIIIIII", data, off
        )
        if p_type == 1 and p_filesz and (p_flags & 1):
            return data[p_offset : p_offset + p_filesz]
        if p_type == 1 and p_filesz:
            chunk = data[p_offset : p_offset + p_filesz]
    return chunk  # noqa: F821 — last PT_LOAD


def main() -> int:
    path = Path(sys.argv[1] if len(sys.argv) > 1 else "transputer/linux-7/vmlinux")
    if not path.is_file():
        print(f"missing {path}", file=sys.stderr)
        return 1
    code = load_code(path)
    prim = Counter()
    ldl2 = ldl2_wsub = ldl2_wsub_ldnl = ldnlp = 0
    # wsub secondary 0x0A → byte 0xFA when single-nibble OPR form
    for i, b in enumerate(code):
        hi = b >> 4
        if hi == 0x2:
            prim["pfix"] += 1
        elif hi == 0x6:
            prim["nfix"] += 1
        else:
            names = {
                0x0: "j",
                0x1: "ldl",
                0x3: "ldnl",
                0x4: "ldc",
                0x5: "ldnlp",
                0x7: "ldl",
                0x8: "adc",
                0x9: "call",
                0xA: "cj",
                0xB: "ajw",
                0xC: "eqc",
                0xD: "stl",
                0xE: "stnl",
                0xF: "opr",
            }
            prim[names.get(hi, f"x{hi:x}")] += 1
        if i + 3 < len(code):
            if (code[i] >> 4) == 0x7 and (code[i + 1] >> 4) == 0x7:
                ldl2 += 1
                if code[i + 2] == 0xFA:
                    ldl2_wsub += 1
                    if (code[i + 3] >> 4) == 0x3:
                        ldl2_wsub_ldnl += 1
        if (b >> 4) == 0x5:
            ldnlp += 1

    print(f"file: {path}  code_bytes={len(code)}")
    print("primary high-nibble mix (approx):")
    for k, v in prim.most_common(16):
        print(f"  {k:8s} {v:8d}")
    print("grouper-friendly adjacency:")
    print(f"  ldl;ldl                 {ldl2:8d}  (dual-local candidates)")
    print(f"  ldl;ldl;wsub(FA)        {ldl2_wsub:8d}")
    print(f"  ldl;ldl;wsub;ldnl       {ldl2_wsub_ldnl:8d}  (textbook pack)")
    print(f"  ldnlp (5x)              {ldnlp:8d}  (Addr-stage ptr+const)")
    if ldl2:
        print(
            f"  textbook/dual_ldl ratio {100.0 * ldl2_wsub_ldnl / ldl2:.3f}%"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
