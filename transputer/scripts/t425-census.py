#!/usr/bin/env python3
"""Check transputer ELF files against the IMS T425 (or T414) instruction set.

usage: t425-census.py [-v] [--hist] [--t414|--t800|--t805] file.elf|file.o ...

Decodes every executable section as a stream of prefixed instructions
and lists each secondary (opr) instruction a T425 does not have - FPU and
T9000 instructions - with the function it is in.  A linked image is a flat
ELF with one PT_LOAD and no section headers: there the code is taken from
the linker's symbol map, file.tccsym, from each text symbol to the next
symbol (without the map the whole segment is decoded, data included).
--hist also prints the instruction histogram and the code size per file.
--t414 checks against the T414 instead, which lacks the T425's additions;
--t805 against the T805, the T425's integer set less the T414's
floating-point support plus the T800 FPU; --t800 against the original
T800, the T805 without pop, the timer enables, ldmemstartval, the j0 break
group, lddevid and start.
Exit status 1 if any file uses an instruction outside the set.

The legal set is the T414 base, the T414 floating-point support
(unpacksn, roundsn, postnormsn, ldinf, cflerr), fmul, and what the T425
adds: dup, the move2d group, crcword/crcbyte, bitcnt, bitrevword,
bitrevnbits, wsubdb, pop, the timer enables, ldmemstartval, the j0 break
group, lddevid and start (Norcroft xpuops.h; Compiler Writer's Guide D.4).
Data placed in a code section decodes as instructions too, so check a
reported address before believing it.
"""
import struct
import sys

DIRECT = ["j", "ldlp", "pfix", "ldnl", "ldc", "ldnlp", "nfix", "ldl",
          "adc", "call", "cj", "ajw", "eqc", "stl", "stnl", "opr"]

T425_OPR = {
    0x00: "rev", 0x01: "lb", 0x02: "bsub", 0x03: "endp", 0x04: "diff",
    0x05: "add", 0x06: "gcall", 0x07: "in", 0x08: "prod", 0x09: "gt",
    0x0A: "wsub", 0x0B: "out", 0x0C: "sub", 0x0D: "startp",
    0x0E: "outbyte", 0x0F: "outword", 0x10: "seterr", 0x12: "resetch",
    0x13: "csub0", 0x15: "stopp", 0x16: "ladd", 0x17: "stlb", 0x18: "sthf",
    0x19: "norm", 0x1A: "ldiv", 0x1B: "ldpi", 0x1C: "stlf", 0x1D: "xdble",
    0x1E: "ldpri", 0x1F: "rem", 0x20: "ret", 0x21: "lend",
    0x22: "ldtimer", 0x23: "testlds", 0x24: "testlde", 0x25: "testldd",
    0x26: "teststs", 0x27: "testste", 0x28: "teststd", 0x29: "testerr",
    0x2A: "testpranal", 0x2B: "tin", 0x2C: "div", 0x2D: "testhardchan",
    0x2E: "dist", 0x2F: "disc", 0x30: "diss", 0x31: "lmul", 0x32: "not",
    0x33: "xor", 0x34: "bcnt", 0x35: "lshr", 0x36: "lshl", 0x37: "lsum",
    0x38: "lsub", 0x39: "runp", 0x3A: "xword", 0x3B: "sb", 0x3C: "gajw",
    0x3D: "savel", 0x3E: "saveh", 0x3F: "wcnt", 0x40: "shr", 0x41: "shl",
    0x42: "mint", 0x43: "alt", 0x44: "altwt", 0x45: "altend", 0x46: "and",
    0x47: "enbt", 0x48: "enbc", 0x49: "enbs", 0x4A: "move", 0x4B: "or",
    0x4C: "csngl", 0x4D: "ccnt1", 0x4E: "talt", 0x4F: "ldiff",
    0x50: "sthb", 0x51: "taltwt", 0x52: "sum", 0x53: "mul",
    0x54: "sttimer", 0x55: "stoperr", 0x56: "cword", 0x57: "clrhalterr",
    0x58: "sethalterr", 0x59: "testhalterr", 0x5A: "dup",
    0x5B: "move2dinit", 0x5C: "move2dall", 0x5D: "move2dnonzero",
    0x5E: "move2dzero", 0x63: "unpacksn", 0x6C: "postnormsn",
    0x6D: "roundsn", 0x71: "ldinf", 0x72: "fmul", 0x73: "cflerr",
    0x74: "crcword", 0x75: "crcbyte", 0x76: "bitcnt", 0x77: "bitrevword",
    0x78: "bitrevnbits", 0x79: "pop", 0x7A: "timerdisableh",
    0x7B: "timerdisablel", 0x7C: "timerenableh", 0x7D: "timerenablel",
    0x7E: "ldmemstartval", 0x81: "wsubdb", 0xB1: "break",
    0xB2: "clrj0break", 0xB3: "setj0break", 0xB4: "testj0break",
    0x17C: "lddevid", 0x1FF: "start",
}

# What the T425 added to the T414
T425_ONLY = {0x5A, 0x5B, 0x5C, 0x5D, 0x5E, 0x74, 0x75, 0x76, 0x77, 0x78,
             0x79, 0x7A, 0x7B, 0x7C, 0x7D, 0x7E, 0x81, 0xB1, 0xB2, 0xB3,
             0xB4, 0x17C, 0x1FF}

# Of those, what the original T800 lacks and the T805 has: pop, the timer
# enables, ldmemstartval and the j0 break group (xpuops.h: "T425 only"),
# with lddevid and start.  The rest - dup, move2d, crc, bitcnt, the bit
# reversals, wsubdb - xpuops.h marks "Only on T800".
T805_ONLY = {0x79, 0x7A, 0x7B, 0x7C, 0x7D, 0x7E, 0xB1, 0xB2, 0xB3, 0xB4,
             0x17C, 0x1FF}

# The T414's floating-point support, which the T800 does not have
# (xpuops.h: "Not on T800"), and the T800's FPU (xpuops.h: "All the rest
# are T800 only", less fprange, fpge and fplg, which it marks T9000 only).
T414_FP_SUPPORT = {0x63, 0x6C, 0x6D, 0x71, 0x73}
T800_FPU = ({0x82, 0x83, 0x84, 0x98, 0x9A, 0xA3, 0xA4, 0xA6, 0xA8, 0xAA,
             0xAB, 0xAC}
            | set(range(0x86, 0x8D)) | set(range(0x8E, 0x97))
            | set(range(0x9C, 0xA2)))

# Names for what a T425 lacks, so a report says what was found.
OTHER_OPR = {
    0x5F: "gtu", 0x80: "fpsttest", 0x82: "fpldnldbi", 0x83: "fpchkerr",
    0x84: "fpstnldb", 0x85: "fpldtest", 0x86: "fpldnlsni", 0x87: "fpadd",
    0x88: "fpstnlsn", 0x89: "fpsub", 0x8A: "fpldnldb", 0x8B: "fpmul",
    0x8C: "fpdiv", 0x8D: "fprange", 0x8E: "fpldnlsn", 0x8F: "fpremfirst",
    0x90: "fpremstep", 0x91: "fpnan", 0x92: "fpordered",
    0x93: "fpnotfinite", 0x94: "fpgt", 0x95: "fpeq", 0x96: "fpi32tor32",
    0x97: "fpge", 0x98: "fpi32tor64", 0x9A: "fpb32tor64", 0x9B: "fplg",
    0x9C: "fptesterr", 0x9D: "fprtoi32", 0x9E: "fpstnli32",
    0x9F: "fpldzerosn", 0xA0: "fpldzerodb", 0xA1: "fpint", 0xA3: "fpdup",
    0xA4: "fprev", 0xA6: "fpldnladddb", 0xA8: "fpldnlmuldb",
    0xAA: "fpldnladdsn", 0xAB: "fpentry", 0xAC: "fpldnlmulsn",
    0xB0: "settimeslice", 0xB6: "ldflags", 0xB7: "stflags",
    0xB8: "xbword", 0xB9: "lbx", 0xBA: "cb", 0xBB: "cbu", 0xBC: "insphdr",
    0xBD: "readbfr", 0xBE: "ldconf", 0xBF: "stconf", 0xC0: "ldcnt",
    0xC1: "ssub", 0xC2: "ldth", 0xC3: "ldchstatus", 0xC4: "intdis",
    0xC5: "intenb", 0xC7: "cir", 0xC8: "ss", 0xC9: "chantype", 0xCA: "ls",
    0xCB: "fpseterr", 0xCC: "ciru", 0xCF: "fprem", 0xD0: "fprn",
    0xD1: "fpdivby2", 0xD2: "fpmulby2", 0xD3: "fpsqrt", 0xD4: "fprp",
    0xD5: "fprm", 0xD6: "fprz", 0xD7: "fpr32tor64", 0xD8: "fpr64tor32",
    0xD9: "fpexpdec32", 0xDA: "fpexpinc32", 0xDB: "fpabs",
    0xDC: "fpclrerr", 0xDD: "fpadddbsn", 0xDE: "fpchki32",
    0xDF: "fpchki64", 0xF0: "devlb", 0xF1: "devsb", 0xF2: "devls",
    0xF3: "devss", 0xF4: "devlw", 0xF5: "devsw", 0xF8: "xsword",
    0xF9: "lsx", 0xFA: "cs", 0xFB: "csu",
}


def opr_name(v):
    if v in T425_OPR:
        return T425_OPR[v]
    if v in OTHER_OPR:
        return OTHER_OPR[v]
    if v < 0:
        return "opr -%#x (T9000)" % -v
    if 0x1C0 <= v <= 0x1FA:
        return "opr %#x (TVX)" % v
    return "opr %#x" % v


def code_ranges(data, path):
    """[(name, file offset, size, load address)] of the executable code,
    and the function symbols [(address, size, name)]."""
    if data[:4] != b"\x7fELF" or data[4] != 1 or data[5] != 1:
        raise ValueError("not a 32-bit little-endian ELF")
    (e_type, _, _, _, e_phoff, e_shoff, _, _, e_phentsize, e_phnum,
     e_shentsize, e_shnum, e_shstrndx) = struct.unpack_from(
        "<HHIIIIIHHHHHH", data, 16)
    ranges, funcs = [], []
    secs = [struct.unpack_from("<IIIIIIIIII", data, e_shoff + i * e_shentsize)
            for i in range(e_shnum)] if e_shoff and e_shnum else []
    if secs:
        strtab = secs[e_shstrndx]

        def secname(off):
            o = strtab[4] + off
            return data[o:data.index(b"\0", o)].decode()
        for i, s in enumerate(secs):
            # SHT_PROGBITS with SHF_EXECINSTR
            if s[1] == 1 and s[2] & 4 and s[5]:
                ranges.append((secname(s[0]), s[4], s[5], s[3], i))
        for s in secs:
            if s[1] != 2:                        # SHT_SYMTAB
                continue
            str_off = secs[s[6]][4]
            for k in range(s[5] // 16):
                name, value, size, info, _, shndx = struct.unpack_from(
                    "<IIIBBH", data, s[4] + k * 16)
                if info & 0xF in (0, 2) and name:   # NOTYPE / FUNC
                    nm = data[str_off + name:data.index(b"\0", str_off + name)]
                    funcs.append((shndx, value, size, nm.decode()))
    else:
        segs, syms = [], []
        for i in range(e_phnum):
            p_type, p_off, p_vaddr, _, p_filesz, _, p_flags, _ = \
                struct.unpack_from("<IIIIIIII", data, e_phoff + i * e_phentsize)
            if p_type == 1 and p_flags & 1:          # PT_LOAD, PF_X
                segs.append((p_off, p_vaddr, p_filesz))
        try:
            for line in open(path + ".tccsym"):
                f = line.split()
                if len(f) == 3:
                    syms.append((int(f[0], 16), f[1], f[2]))
        except OSError:
            pass
        syms.sort()
        for off, vaddr, size in segs:
            if not syms:
                ranges.append(("PT_LOAD", off, size, vaddr, None))
                continue
            # a text symbol's code runs to the next symbol at a higher address
            for k, (a, t, n) in enumerate(syms):
                if t not in "TtW" or not vaddr <= a < vaddr + size:
                    continue
                end = vaddr + size
                for a2, _, _ in syms[k + 1:]:
                    if a2 > a:
                        end = min(end, a2)
                        break
                ranges.append((n, off + a - vaddr, end - a, a, None))
                funcs.append((None, a, end - a, n))
    return ranges, funcs


def decode(code):
    """yield (offset, length, fn, operand)"""
    i, start, oreg = 0, 0, 0
    while i < len(code):
        b = code[i]
        fn, oreg = b >> 4, (oreg | (b & 0xF)) & 0xFFFFFFFF
        i += 1
        if fn == 2:
            oreg = (oreg << 4) & 0xFFFFFFFF
        elif fn == 6:
            oreg = (~oreg << 4) & 0xFFFFFFFF
        else:
            v = oreg - (1 << 32) if oreg & 0x80000000 else oreg
            yield start, i - start, fn, v
            start, oreg = i, 0


def census(path, verbose, hist, legal):
    data = open(path, "rb").read()
    ranges, funcs = code_ranges(data, path)
    bad, counts, total = {}, {}, 0
    for name, off, size, addr, shndx in ranges:
        fs = sorted((v, n) for s, v, _, n in funcs if shndx is None or s == shndx)

        def where(o):
            a = (addr if addr else 0) + o
            best = None
            for v, n in fs:
                if v <= a:
                    best = (v, n)
                else:
                    break
            return "%s+%#x" % (best[1], a - best[0]) if best else "%s+%#x" % (name, o)
        total += size
        for o, ln, fn, v in decode(data[off:off + size]):
            key = opr_name(v) if fn == 15 else DIRECT[fn]
            counts[key] = counts.get(key, 0) + 1
            if fn == 15 and v not in legal:
                bad.setdefault(key, []).append(where(o))
    for key in sorted(bad):
        sites = bad[key]
        shown = ", ".join(sites[:4 if not verbose else len(sites)])
        more = "" if verbose or len(sites) <= 4 else ", ... (%d more)" % (len(sites) - 4)
        print("%s: %s x%d: %s%s" % (path, key, len(sites), shown, more))
    if hist:
        print("%s: %d code bytes, %d instructions" % (path, total, sum(counts.values())))
        for key, n in sorted(counts.items(), key=lambda kv: -kv[1]):
            print("  %8d  %s" % (n, key))
    return not bad


def main(argv):
    verbose = "-v" in argv
    hist = "--hist" in argv
    cpu = "T425"
    for c in ("T414", "T800", "T805"):
        if "--" + c.lower() in argv:
            cpu = c
    legal = set(T425_OPR)
    if cpu == "T414":
        legal -= T425_ONLY
    elif cpu in ("T800", "T805"):
        legal = (legal - T414_FP_SUPPORT) | T800_FPU
        if cpu == "T800":
            legal -= T805_ONLY
    files = [a for a in argv if not a.startswith("-")]
    if not files:
        sys.exit(__doc__.strip().splitlines()[2])
    ok = True
    for f in files:
        ok &= census(f, verbose, hist, legal)
    if ok:
        print("%s: %d file(s), no instruction outside the %s set"
              % (cpu, len(files), cpu))
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main(sys.argv[1:])
