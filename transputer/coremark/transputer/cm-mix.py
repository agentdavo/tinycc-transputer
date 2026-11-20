#!/usr/bin/env python3
"""cm-mix.py - one CoreMark iteration's instruction mix from two T4_OPHIST runs.

  cm-mix.py image.elf oph-A.txt oph-B.txt DIFF [--grams N]

oph-A/oph-B come from the same program built with ITERATIONS a and b, with
b - a = DIFF; both were linked from the same objects, so every address
matches (cm-measure.sh builds them that way and checks it).  The
per-address difference divided by DIFF is one iteration, with start-up and
the report cancelled out.

Reports, per iteration: instructions with prefixes folded into the
instruction they lead to ("ops"), and every instruction byte counted on
its own as the hardware executes it ("raw", what an RTL retire counter
sees); the opcode mix; the functions; and the commonest straight-line
sequences of 2..N instructions (an n-gram is weighted by its least
executed member), with local slots abstracted and constants kept.
"""
import collections
import importlib.util
import os
import struct
import sys

_spec = importlib.util.spec_from_file_location(
    "t425census", os.path.join(os.path.dirname(os.path.abspath(__file__)),
                               "..", "..", "scripts", "t425-census.py"))
_census = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_census)
DIRECT = _census.DIRECT


def load_image(path):
    d = open(path, "rb").read()
    phoff = struct.unpack_from("<I", d, 28)[0]
    phentsize, phnum = struct.unpack_from("<HH", d, 42)
    segs = []
    for i in range(phnum):
        typ, off, vaddr, _, filesz, _, _, _ = struct.unpack_from(
            "<IIIIIIII", d, phoff + i * phentsize)
        if typ == 1:
            segs.append((vaddr, d[off:off + filesz]))
    return segs


def byte_at(segs, a):
    for v, b in segs:
        if v <= a < v + len(b):
            return b[a - v]
    return None


def decode_at(segs, a):
    """(length, fn, operand) of the instruction (prefixes folded) at a."""
    oreg, i = 0, a
    while True:
        b = byte_at(segs, i)
        if b is None:
            return None
        fn, oreg = b >> 4, (oreg | (b & 0xF)) & 0xFFFFFFFF
        i += 1
        if fn == 2:
            oreg = (oreg << 4) & 0xFFFFFFFF
        elif fn == 6:
            oreg = (~oreg << 4) & 0xFFFFFFFF
        else:
            v = oreg - (1 << 32) if oreg & 0x80000000 else oreg
            return i - a, fn, v


def name_of(fn, v):
    if fn == 15:
        return _census.opr_name(v)
    return DIRECT[fn]


ABSTRACT = {"ldl", "stl", "ldlp", "j", "cj", "call", "ajw"}


def token(fn, v):
    n = name_of(fn, v)
    if fn == 15:
        return n
    if n in ABSTRACT:
        return n
    return "%s %d" % (n, v)


def read_oph(path):
    h = {}
    for line in open(path):
        p = line.split()
        h[int(p[0], 16)] = int(p[3])
    return h


def symbols(elf):
    syms = []
    try:
        for line in open(elf + ".tccsym"):
            p = line.split()
            if len(p) >= 3 and p[1] in "tT":
                syms.append((int(p[0], 16), p[2]))
    except OSError:
        pass
    syms.sort()
    return syms


def func_of(syms, a):
    lo, hi = 0, len(syms)
    while lo < hi:
        m = (lo + hi) // 2
        if syms[m][0] <= a:
            lo = m + 1
        else:
            hi = m
    return syms[lo - 1][1] if lo else "?"


def ins_of(segs, cnt):
    ins = {}
    for a in cnt:
        r = decode_at(segs, a)
        if r:
            ins[a] = r
    return ins


# Candidate compiler-facing instructions, as the straight-line sequences each
# would replace: (name, what it does, pattern, instructions after).  A
# pattern is a list of tests, one per instruction; `ws` marks the operand of
# ldl/stl/ldlp, `k` any constant.  A sequence is counted once per execution,
# weighted by its least executed member, and never across a j/call.
def _op(name, pred=None):
    def t(n, v):
        return n == name and (pred is None or pred(v))
    return t


def _any(*names):
    def t(n, v):
        return n in names
    return t


IDIOMS = [
    ("stlk", "store A to a local and keep it in A (stl x; ldl x)",
     [_op("stl"), _op("ldl")], 1, "same"),
    ("ldlnl", "load word at local pointer + k (ldl x; ldnl k)",
     [_op("ldl"), _op("ldnl")], 1, None),
    ("stlnl", "store A at local pointer + k (ldl x; stnl k)",
     [_op("ldl"), _op("stnl")], 1, None),
    ("cjnz", "jump if A is not zero (eqc 0; cj)",
     [_op("eqc", lambda v: v == 0), _op("cj")], 1, None),
    ("cjcmp", "compare A, B and jump (gt/gtu [; eqc 0]; cj)",
     [_any("gt", "gtu"), _op("cj")], 1, None),
    ("cjcmpn", "compare A, B and jump on false (gt/gtu; eqc 0; cj)",
     [_any("gt", "gtu"), _op("eqc", lambda v: v == 0), _op("cj")], 1, None),
    ("cjeqc", "compare A with a constant and jump (eqc k; cj)",
     [_op("eqc", lambda v: v != 0), _op("cj")], 1, None),
    ("op3", "workspace three-operand op: c = a OP b (ldl a; ldl b; OP; stl c)",
     [_op("ldl"), _op("ldl"),
      _any("bsub", "sum", "diff", "prod", "and", "or", "xor", "wsub", "ssub",
           "gt", "gtu", "shl", "shr"),
      _op("stl")], 1, None),
    ("op2l", "A = A OP local (ldl b; OP), OP an ALU op",
     [_op("ldl"),
      _any("bsub", "sum", "diff", "prod", "and", "or", "xor", "gt", "gtu")],
     1, None),
    ("incl", "add a constant to a local (ldl x; adc k|ldc k; bsub; stl x)",
     None, 1, "incl"),
    ("adcu", "add a constant without the overflow check (ldc k; bsub)",
     [_op("ldc", lambda v: -128 <= v < 128), _op("bsub")], 1, None),
    ("ldxi", "indexed narrow/word load (ssub; lsx|ls, wsub; ldnl 0, bsub; lb)",
     None, 1, "ldxi"),
    ("asri", "arithmetic shift right by a constant (xdble; ldc k; lshr)",
     [_op("xdble"), _op("ldc"), _op("lshr")], 1, None),
    ("maskc", "and with a small constant mask (ldc m; and)",
     [_op("ldc", lambda v: 0 <= v < 65536), _op("and")], 1, None),
]


def _seq_at(ins, cnt, a, n):
    out, w, p = [], None, a
    for _ in range(n):
        if p not in ins or p not in cnt:
            return None, 0
        ln, fn, v = ins[p]
        name = name_of(fn, v)
        out.append((name, v, fn))
        w = cnt[p] if w is None else min(w, cnt[p])
        if (DIRECT[fn] in ("j", "call") or (fn == 15 and v in (0x20, 0x06))) \
                and len(out) < n:
            return None, 0
        p += ln
    return out, w


def idioms(segs, ins, cnt):
    ops = sum(cnt.values())
    print("per iteration: %.0f instructions" % ops)
    print("%-8s %10s %8s %6s  %s" % ("op", "seqs/iter", "saved", "%",
                                     "replaces"))
    for name, what, pat, after, special in IDIOMS:
        total = 0.0
        saved = 0.0
        for a in ins:
            if special == "incl":
                for shape, ln in ((4, 4), (3, 3)):
                    s, w = _seq_at(ins, cnt, a, shape)
                    if not s or w <= 0:
                        continue
                    if shape == 3 and s[0][0] == "ldl" and s[1][0] == "adc" \
                            and s[2][0] == "stl" and s[0][1] == s[2][1]:
                        total += w
                        saved += w * (3 - after)
                        break
                    if shape == 4 and s[0][0] == "ldl" and s[1][0] == "ldc" \
                            and s[2][0] == "bsub" and s[3][0] == "stl" \
                            and s[0][1] == s[3][1]:
                        total += w
                        saved += w * (4 - after)
                        break
                continue
            if special == "ldxi":
                s, w = _seq_at(ins, cnt, a, 2)
                if s and w > 0 and (
                        (s[0][0] == "ssub" and s[1][0] in ("lsx", "ls")) or
                        (s[0][0] == "wsub" and s[1][0] == "ldnl" and
                         s[1][1] == 0) or
                        (s[0][0] == "bsub" and s[1][0] in ("lb", "lbx"))):
                    total += w
                    saved += w
                continue
            s, w = _seq_at(ins, cnt, a, len(pat))
            if not s or w <= 0:
                continue
            if all(t(x[0], x[1]) for t, x in zip(pat, s)):
                if special == "same" and s[0][1] != s[1][1]:
                    continue
                total += w
                saved += w * (len(pat) - after)
        print("%-8s %10.0f %8.0f %5.1f%%  %s" % (name, total, saved,
                                                  100 * saved / ops, what))
    # All of them at once, without double counting: cover the code from
    # each address on with the first idiom (in this order) that matches.
    order = ["incl", "op3", "cjcmpn", "cjcmp", "cjeqc", "cjnz", "stlk",
             "ldlnl", "stlnl", "ldxi", "adcu", "asri", "maskc", "op2l"]
    by = dict((i[0], i) for i in IDIOMS)
    # dynamic programming from the end of the code backwards: at each
    # address, the better of skipping one instruction and taking any idiom
    # that matches there
    best = {}
    choice = {}
    addrs = sorted(ins)
    for a in reversed(addrs):
        nxt = a + ins[a][0]
        best[a] = best.get(nxt, 0.0)
        choice[a] = None
        for name in order:
            _, _, pat, after, special = by[name]
            if special == "incl":
                cands = [(4, 4), (3, 3)]
            elif special == "ldxi":
                cands = [(2, 2)]
            else:
                cands = [(len(pat), len(pat))]
            for shape, ln in cands:
                s, w = _seq_at(ins, cnt, a, shape)
                if not s or w <= 0:
                    continue
                if special == "incl":
                    ok = (shape == 3 and s[0][0] == "ldl" and s[1][0] == "adc"
                          and s[2][0] == "stl" and s[0][1] == s[2][1]) or \
                         (shape == 4 and s[0][0] == "ldl" and s[1][0] == "ldc"
                          and s[2][0] == "bsub" and s[3][0] == "stl"
                          and s[0][1] == s[3][1])
                elif special == "ldxi":
                    ok = (s[0][0] == "ssub" and s[1][0] in ("lsx", "ls")) or \
                         (s[0][0] == "wsub" and s[1][0] == "ldnl" and
                          s[1][1] == 0) or \
                         (s[0][0] == "bsub" and s[1][0] in ("lb", "lbx"))
                else:
                    ok = all(t(x[0], x[1]) for t, x in zip(pat, s))
                    if ok and special == "same":
                        ok = s[0][1] == s[1][1]
                if not ok:
                    continue
                p = a
                for _ in range(shape):
                    p += ins[p][0]
                v = w * (shape - after) + best.get(p, 0.0)
                if v > best[a]:
                    best[a] = v
                    choice[a] = (name, shape, w * (shape - after), p)
    tot = 0.0
    per = collections.Counter()
    seen = set()
    for a in addrs:
        if a in seen:
            continue
        c = choice[a]
        if c is None:
            continue
        # follow the chosen path from here
        p = a
        while p in ins and p not in seen:
            c = choice.get(p)
            seen.add(p)
            if c is None:
                p += ins[p][0]
                continue
            name, shape, sv, q = c
            tot += sv
            per[name] += sv
            r = p
            for _ in range(shape):
                seen.add(r)
                r += ins[r][0]
            p = q
    print("all together (no overlap): %.0f saved, %.1f%%; %.0f instructions "
          "an iteration" % (tot, 100 * tot / ops, ops - tot))
    for name in order:
        print("  %-8s %8.0f" % (name, per[name]))


def main(argv):
    grams = 6
    if "--grams" in argv:
        k = argv.index("--grams")
        grams = int(argv[k + 1])
        del argv[k:k + 2]
    elf, fa, fb, diff = argv[1], argv[2], argv[3], int(argv[4])
    segs = load_image(elf)
    ha, hb = read_oph(fa), read_oph(fb)
    syms = symbols(elf)
    cnt = {}
    for a in set(ha) | set(hb):
        c = hb.get(a, 0) - ha.get(a, 0)
        if c:
            cnt[a] = c / diff
    ins = {}
    for a in cnt:
        r = decode_at(segs, a)
        if r:
            ins[a] = r
    if "--idioms" in argv:
        idioms(segs, ins_of(segs, cnt), cnt)
        return
    if "--func" in argv:
        # Annotated listing of functions: address, executions/iteration, insn.
        for want in argv[argv.index("--func") + 1:]:
            lo = [s for s in syms if s[1] == want]
            if not lo:
                print("no function", want)
                continue
            start = lo[0][0]
            nxt = [s[0] for s in syms if s[0] > start]
            end = min(nxt) if nxt else start + 4096
            print("== %s ==" % want)
            a = start
            while a < end:
                r = decode_at(segs, a)
                if not r:
                    break
                ln, fn, v = r
                n = name_of(fn, v)
                opnd = "" if fn == 15 else " %d" % v
                if DIRECT[fn] in ("j", "cj", "call"):
                    opnd = " %#x" % ((a + ln + v) & 0xFFFFFFFF)
                print("%08x %10.1f  %s%s" % (a, cnt.get(a, 0), n, opnd))
                a += ln
        return
    ops = sum(cnt.values())
    raw = sum(c * ins[a][0] for a, c in cnt.items() if a in ins)
    print("per iteration: %.0f instructions (prefixes folded), %.0f raw "
          "instruction bytes (each prefix counted)" % (ops, raw))
    neg = sum(1 for c in cnt.values() if c < 0)
    if neg:
        print("warning: %d addresses ran fewer times in the longer run" % neg)
    by = collections.Counter()
    pre = collections.Counter()
    for a, c in cnt.items():
        if a in ins:
            ln, fn, v = ins[a]
            by[name_of(fn, v)] += c
            pre[name_of(fn, v)] += c * (ln - 1)
    print("prefix bytes: %.0f (%.1f%% of raw)"
          % (raw - ops, 100.0 * (raw - ops) / raw if raw else 0))
    print()
    print("== opcode mix (ops/iteration, %% of ops, prefix bytes) ==")
    for n, c in by.most_common(45):
        print("%-14s %10.0f %6.2f%% %9.0f" % (n, c, 100 * c / ops, pre[n]))
    ws = by["ldl"] + by["stl"] + by["ldlp"]
    print("workspace ldl+stl+ldlp: %.0f (%.1f%%)" % (ws, 100 * ws / ops))
    print()
    print("== functions (ops/iteration) ==")
    fc = collections.Counter()
    for a, c in cnt.items():
        fc[func_of(syms, a)] += c
    for n, c in fc.most_common(25):
        print("%-28s %10.0f %6.2f%%" % (n, c, 100 * c / ops))
    print()
    for g in range(2, grams + 1):
        print("== straight-line %d-instruction sequences (top 20) ==" % g)
        gc = collections.Counter()
        for a in ins:
            seq, w, p = [], None, a
            for _ in range(g):
                if p not in ins or p not in cnt:
                    seq = None
                    break
                ln, fn, v = ins[p]
                seq.append(token(fn, v))
                w = cnt[p] if w is None else min(w, cnt[p])
                if DIRECT[fn] in ("j", "call") or (fn == 15 and v in (0x20, 0x06)):
                    if len(seq) < g:
                        seq = None
                    break
                p += ln
            if seq and len(seq) == g and w > 0:
                gc["; ".join(seq)] += w
        for s, c in gc.most_common(20):
            print("%10.0f %6.2f%%  %s" % (c, 100 * c / ops, s))
        print()


if __name__ == "__main__":
    main(sys.argv)
