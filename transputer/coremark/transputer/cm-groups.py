#!/usr/bin/env python3
"""cm-groups.py elf tccsym hist_hi hist_lo DIV [--top N] [--site "a -> b"]

Instructions per group for the R7 base template of MODERNISE's issue_model.py
(imported: form_groups), from T4 OPHIST counts instead of an RTL trace.
Groups never cross a branch or a label, so the dynamic group count is the sum
over straight-line runs (split at every cj/j/call/ret/gcall and every branch
target, and where the execution count changes) of count x groups(run).
Also reports the commonest group boundaries: (last instruction of a group,
first of the next) inside a run, weighted by executions.
"""
import sys, subprocess, re, collections, importlib.util, os

args = sys.argv[1:]
top = 15
site = None
if '--site' in args:
    i = args.index('--site'); site = args[i + 1]; del args[i:i + 2]
if '--top' in args:
    i = args.index('--top'); top = int(args[i + 1]); del args[i:i + 2]
elf, smap, hhi, hlo, div = args[:5]
div = float(div)
here = os.path.dirname(os.path.abspath(__file__))
# the model is MODERNISE's (tp repo, test/modern/prof/issue_model.py): ISSUE_MODEL
# names the file; by default a copy beside this script
model = os.environ.get('ISSUE_MODEL', os.path.join(here, 'issue_model.py'))
import types
if 'numpy' not in sys.modules:          # form_groups needs no numpy
    try:
        import numpy  # noqa: F401
    except ImportError:
        sys.modules['numpy'] = types.ModuleType('numpy')
spec = importlib.util.spec_from_file_location('issue_model', model)
im = importlib.util.module_from_spec(spec)
spec.loader.exec_module(im)
R7 = dict(GW=8, PB=2, NL=2, E2=False, WL=False, BM=False, FC=False)
DAST = os.environ.get('DAST', os.path.join(here, '..', '..', 'ttools', 'dast', 'dast'))

def readh(fn):
    c = {}
    for line in open(fn):
        p = line.split()
        if len(p) == 6:
            c[int(p[0], 16)] = int(p[3])
    return c
hi = readh(hhi); lo = readh(hlo)
cnt = {k: (v - lo.get(k, 0)) / div for k, v in hi.items()}

out = subprocess.run([DAST, '-s', '0x54', '-O', '0x80000070', elf], capture_output=True).stdout.decode('latin-1')
ins = []      # [addr, name, nbytes, nprefix, target]
pend_b = pend_p = 0
for line in out.splitlines():
    if not line.startswith('0x'):
        continue
    ad = int(line.split(':')[0], 16)
    hexpart = line[13:44]
    nb = len(re.findall(r'\b[0-9a-f]{2}\b', hexpart))
    txt = line[44:].strip()
    m = re.match(r'(\w+)(?:\s+(0x[0-9a-f]+|-?\d+))?', txt)
    if not m:
        continue
    name = m.group(1)
    if name in ('pfix', 'nfix'):          # a lone prefix (padding): fold into the next
        pend_b += nb; pend_p += nb
        continue
    tgt = None
    if name in ('j', 'cj', 'call') and m.group(2):
        try: tgt = int(m.group(2), 0)
        except ValueError: pass
    ins.append([ad, name, nb + pend_b, nb - 1 + pend_p, tgt])
    pend_b = pend_p = 0
labels = {t[4] for t in ins if t[4] is not None and t[1] != 'call'}
total_i = total_g = 0.0
bound = collections.Counter()
sites = collections.Counter()
run = []; rc = None
CTRL = {'cj', 'j', 'call', 'ret', 'gcall', 'lend'}

def flush():
    global total_i, total_g
    if not run or not rc:
        return
    seq = [(t[1], t[2], t[3], t[1] == 'j') for t in run]
    if os.environ.get('SIM_SUM'):       # what-if: bsub right after a load is sum
        seq = [(('sum' if x[0] == 'bsub' and k and seq[k - 1][0] in im.M_OPS else x[0]),) + x[1:]
               for k, x in enumerate(seq)]
    gs = im.form_groups(seq, R7)
    total_g += rc * len(gs)
    total_i += rc * len(seq)
    k = 0
    for gi, g in enumerate(gs[:-1]):
        k += g[0]
        a = seq[k - 1]; b = seq[k]
        why = '%s -> %s' % (a[0], b[0])
        # the bound that ended it: bytes/prefix, or the template order
        gb = sum(x[1] for x in seq[k - g[0]:k]); gp = sum(x[2] for x in seq[k - g[0]:k])
        if gb + b[1] > R7['GW']: why += '  [bytes]'
        elif gp + b[2] > R7['PB']: why += '  [prefix]'
        bound[why] += rc
        if site and why.startswith(site):
            sites[(run[0][0], tuple(x[0] for x in seq))] += rc

for t in ins:
    c = cnt.get(t[0], 0)
    if run and (t[0] in labels or c != rc):
        flush(); run = []
    if not run:
        rc = c
    run.append(t)
    if t[1] in CTRL:
        flush(); run = []
flush()
print('instructions/unit %.0f  groups/unit %.0f  instr/group %.4f' % (total_i, total_g, total_i / total_g))
print('commonest boundaries inside straight runs (share of groups):')
for k, v in bound.most_common(top):
    print('  %-34s %9.0f  %5.2f%%' % (k, v, 100 * v / total_g))

if site:
    print('sites for', site)
    for (ad, seq), v in sites.most_common(8):
        print('  %08x %8.0f  %s' % (ad, v, ' '.join(seq)))
