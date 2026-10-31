#!/usr/bin/env python3
"""List the instructions of functions in a transputer object file.

usage: tpdis.py file.o [function ...]

Decodes .text by symbol, folding pfix/nfix prefixes into the instruction
they lead to, and prints each with its address; j, cj and call also show
their target.  With no function names, every function is listed.  Secondary
(opr) names come from t425-census.py's tables (the T425's, and the FPU and
T9000 ones it names when it finds them); anything else prints as
opr#<code>.

Handy for checks such as "this function has no j or lend" - the timeslice
points (see lib/transputer-conc.c):

    tpdis.py conc.o __tp_proc_stop | grep -E ': (j |lend)'
"""
import importlib.util
import os
import struct
import sys

DIRECT = ["j", "ldlp", "pfix", "ldnl", "ldc", "ldnlp", "nfix", "ldl",
          "adc", "call", "cj", "ajw", "eqc", "stl", "stnl", "opr"]

_spec = importlib.util.spec_from_file_location(
    "t425census", os.path.join(os.path.dirname(os.path.abspath(__file__)),
                               "t425-census.py"))
_census = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_census)
OPR = dict(_census.OTHER_OPR)     # FPU, T9000, ... (gtu among them)
OPR.update(_census.T425_OPR)


def sections(d):
    shoff = struct.unpack_from("<I", d, 32)[0]
    shentsize, shnum, shstrndx = struct.unpack_from("<HHH", d, 46)
    secs = [struct.unpack_from("<IIIIIIIIII", d, shoff + i * shentsize)
            for i in range(shnum)]

    def name(off, tab):
        s = d[secs[tab][4] + off:]
        return s[:s.index(b"\0")].decode()
    return secs, [name(s[0], shstrndx) for s in secs], name


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    d = open(sys.argv[1], "rb").read()
    secs, names, name = sections(d)
    text = names.index(".text")
    symtab = secs[names.index(".symtab")]
    funcs = []
    for i in range(symtab[5] // 16):
        n, v, sz, info, _, shn = struct.unpack_from("<IIIBBH", d,
                                                    symtab[4] + i * 16)
        if shn == text and info & 0xf == 2:
            funcs.append((v, sz, name(n, symtab[6])))
    want = sys.argv[2:]
    for v, sz, fn in sorted(funcs):
        if want and fn not in want:
            continue
        print(fn + ":")
        code = d[secs[text][4] + v:secs[text][4] + v + sz]
        i = start = o = 0
        while i < len(code):
            f, n = code[i] >> 4, code[i] & 15
            i += 1
            o |= n
            if f == 2:          # pfix
                o = (o << 4) & 0xffffffff
                continue
            if f == 6:          # nfix
                o = (~o << 4) & 0xffffffff
                continue
            val = o - (1 << 32) if o & 0x80000000 else o
            if f == 15:
                txt = OPR.get(o, "opr#%x" % o)
            elif f in (0, 9, 10):
                txt = "%s %+d -> %#x" % (DIRECT[f], val, v + i + val)
            else:
                txt = "%s %d" % (DIRECT[f], val)
            print("  %5x: %s" % (v + start, txt))
            o = 0
            start = i


if __name__ == "__main__":
    main()
