#!/usr/bin/env python3
"""Dynamic census of workspace round trips in a CoreMark (or any) image.

  cm-mine.py elf tccsym hist_hi [hist_lo DIV] [-top N] [-ctx]

(CoreMark: cm-measure.sh's oph-*-i30.txt and oph-*-i10.txt, DIV 20.
One histogram and a DIV divides it: Dhrystone's 20,000-loop run.)

Counts per executed instruction come from a T4_OPHIST histogram; with two
histograms (i30 and i10 runs) the difference / DIV gives per-iteration counts.
Reports, weighted by executions:
  A  stl x; ldl x adjacent, x dead after (removable: 2 instr)
  B  stl x; ldl x adjacent, x live after (dup; stl x: 0 instr)
  C  stl x; <1-3 instr>; ldl x, x dead after the ldl (2 instr if the stack allows)
  D  ldl x; ldl x adjacent (ldl x; dup: 0 instr, saves a memory read)
  E  ldl x ... ldl x within 4, no write of x between (upper bound 1 instr)
"""
import sys, subprocess, bisect, collections, re

a = sys.argv[1:]
top = 25; ctx = False
if '-top' in a:
    i = a.index('-top'); top = int(a[i + 1]); del a[i:i + 2]
if '-ctx' in a:
    a.remove('-ctx'); ctx = True
elf, smap, hhi = a[:3]
hlo = a[3] if len(a) > 3 else None
div = float(a[4]) if len(a) > 4 else 1.0
import os
DAST = os.environ.get('DAST', os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                     '..', '..', 'ttools', 'dast', 'dast'))

def readh(fn):
    c = {}
    for line in open(fn):
        p = line.split()
        if len(p) == 6:
            c[int(p[0], 16)] = int(p[3])
    return c
cnt = readh(hhi)
if hlo:
    lo = readh(hlo)
    cnt = {k: (v - lo.get(k, 0)) / div for k, v in cnt.items()}
elif div > 1:
    cnt = {k: v / div for k, v in cnt.items()}
total = sum(cnt.values())

syms = []
for line in open(smap):
    p = line.split()
    if len(p) == 3 and p[1] in 'Tt':
        syms.append((int(p[0], 16), p[2]))
syms.sort()
starts = sorted({s for s, _ in syms})
name_at = {}
for s, n in syms:
    name_at.setdefault(s, n)

out = subprocess.run([DAST, '-s', '0x54', '-O', '0x80000070', elf], capture_output=True).stdout.decode('latin-1')
insn = []
for line in out.splitlines():
    if not line.startswith('0x'):
        continue
    ad = int(line.split(':')[0], 16)
    txt = line[44:].strip()
    m = re.match(r'(\w+)(?:\s+(0x[0-9a-f]+|-?\d+))?', txt)
    if not m:
        continue
    try:
        opv = int(m.group(2), 0) if m.group(2) else None
    except ValueError:
        opv = None
    i = bisect.bisect_right(starts, ad) - 1
    f = name_at[starts[i]] if i >= 0 else '?'
    insn.append([ad, m.group(1), opv, cnt.get(ad, 0), f])
idx = {t[0]: k for k, t in enumerate(insn)}
# branch targets: dast prints j/cj/call operand as a relative offset from next insn
def target(k):
    t = insn[k]
    if t[1] in ('j', 'cj', 'call') and t[2] is not None and k + 1 < len(insn):
        return t[2]
    return None
labels = set()
for k in range(len(insn)):
    tg = target(k)
    if tg is not None and insn[k][1] != 'call':
        labels.add(tg)
addr_taken = collections.defaultdict(set)
for t in insn:
    if t[1] == 'ldlp' and t[2] is not None:
        for d in range(0, 2):
            addr_taken[t[4]].add(t[2] + d)

def live_after(k, x, f):
    """Is local x read (before written) on some path from instruction k+1?"""
    if x in addr_taken[f]:
        return True
    seen = set(); work = [k + 1]
    while work:
        j = work.pop()
        steps = 0
        while 0 <= j < len(insn) and insn[j][4] == f and j not in seen and steps < 400:
            seen.add(j); steps += 1
            mn, ov = insn[j][1], insn[j][2]
            if mn in ('ldl',) and ov == x:
                return True
            if mn == 'stl' and ov == x:
                break
            if mn in ('ret', 'gcall', 'stopp', 'endp'):
                break
            if mn == 'ajw' and ov and ov > 0:
                # epilogue: frame gone
                break
            if mn == 'j':
                tg = target(j)
                if tg in idx:
                    j = idx[tg]; continue
                break
            if mn == 'cj':
                tg = target(j)
                if tg in idx:
                    work.append(idx[tg])
            j += 1
        else:
            if j in seen or steps >= 400:
                pass
    return False

pat = collections.Counter()
site = collections.defaultdict(collections.Counter)
func = collections.defaultdict(collections.Counter)
def hit(key, c, k):
    pat[key] += c
    site[key][k] += c
    func[key][insn[k][4]] += c
WRITES = ('stl',)
for k in range(1, len(insn)):
    ad, mn, ov, c, f = insn[k]
    if not c or mn != 'ldl' or ov is None:
        continue
    p = insn[k - 1]
    if p[1] == 'stl' and p[2] == ov and ad not in labels:
        if live_after(k, ov, f):
            hit('B stl x; ldl x, x live after', c, k)
        else:
            hit('A stl x; ldl x, x dead after (2 removable)', 2 * c, k)
        continue
    if p[1] == 'ldl' and p[2] == ov and ad not in labels:
        hit('D ldl x; ldl x (-> dup)', c, k)
        continue
    # look back up to 4 instructions
    for d in range(2, 5):
        if k - d < 0:
            break
        q = insn[k - d]
        if any(insn[j][0] in labels for j in range(k - d + 1, k + 1)):
            break
        if q[4] != f:
            break
        mid = insn[k - d + 1:k]
        if any(m[1] in ('call', 'j', 'cj', 'gcall', 'ret') for m in mid):
            break
        if any(m[1] == 'stl' and m[2] == ov for m in mid):
            break
        if q[1] == 'stl' and q[2] == ov:
            if not live_after(k, ov, f):
                hit('C stl x; <%d>; ldl x, x dead after' % (d - 1), 2 * c, k)
            else:
                hit('C2 stl x; <%d>; ldl x, x live after' % (d - 1), c, k)
            break
        if q[1] == 'ldl' and q[2] == ov:
            hit('E ldl x; <%d>; ldl x' % (d - 1), c, k)
            break

print('executed per unit: %.0f' % total)
for key in sorted(pat):
    print('%-45s %10.0f  %5.2f%%' % (key, pat[key], 100.0 * pat[key] / total))
for key in sorted(pat):
    print('\n== %s' % key)
    for f, c in func[key].most_common(8):
        print('   %-28s %9.0f' % (f, c))
    if ctx:
        for k, c in site[key].most_common(top):
            lo_ = max(0, k - 5); hi_ = min(len(insn), k + 3)
            print('  -- %s %08x  %.0f' % (insn[k][4], insn[k][0], c))
            for j in range(lo_, hi_):
                t = insn[j]
                print('     %s%08x %-6s %-8s %9.0f' % ('>' if j == k else ' ', t[0], t[1], '' if t[2] is None else t[2], t[3]))
