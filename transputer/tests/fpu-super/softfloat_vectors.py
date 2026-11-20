#!/usr/bin/env python3
"""Generate IEEE vectors with the supplied SoftFloat tree on Linux x86-64.

Run in WSL. Sources are copied to a temporary build; the supplied tree is read
only. The committed vectors let ordinary BTL builds run without SoftFloat.
"""
import argparse
import hashlib
import json
import random
from pathlib import Path
import shutil
import subprocess
import tempfile

p = argparse.ArgumentParser()
p.add_argument('--source', type=Path, required=True)
p.add_argument('--out', type=Path, required=True)
p.add_argument('--seed', type=lambda value: int(value, 0), default=0x9000805)
p.add_argument('--random-only', action='store_true', help='emit only this seed\'s 112 random vectors')
a = p.parse_args()
src = a.source.resolve()
here = Path(__file__).resolve().parent
vectors = []
for width, fraction, bias in [(32, 23, 127), (64, 52, 1023)]:
    one = bias << fraction
    two = (bias + 1) << fraction
    half = (bias - 1) << fraction
    three = two | (1 << (fraction - 1))
    smallest_normal = 1 << fraction
    largest = ((2 * bias + 1) << fraction) - 1
    sign = 1 << (width - 1)
    pairs = {
        'add': [(one, (bias - fraction - 1) << fraction), (one, one | sign)],
        'sub': [(one, (bias - fraction - 2) << fraction), (smallest_normal, 1)],
        'mul': [(largest, two), (1, half)],
        'div': [(one, three), (smallest_normal, three)],
        'sqrt': [(two, 0), (1, 0)],
    }
    for rounding, mode in [('rn', 0), ('rz', 1), ('rm', 2), ('rp', 3)]:
        for op, operands in pairs.items():
            for index, (x, y) in enumerate(operands):
                vectors.append(dict(name=f'softfloat-{width}-{op}-{rounding}-{index}',
                                    width=width, op=op, rounding=rounding,
                                    mode=mode, a=f'{x:016x}', b=f'{y:016x}'))

# Fixed seed and integer bit construction: host FP cannot influence inputs.
seed = a.seed
rng = random.Random(seed)
modes = [('rn', 0), ('rz', 1), ('rm', 2), ('rp', 3)]
def append(width, op, rounding, mode, x, y=0, result_width=None, group='boundary'):
    vectors.append(dict(name=f'softfloat-{group}-{width}-{op}-{rounding}-{len(vectors)}',
                        width=width, result_width=result_width or width, op=op,
                        rounding=rounding, mode=mode, a=f'{x:016x}', b=f'{y:016x}'))

def finite(width):
    fraction, bias = (23, 127) if width == 32 else (52, 1023)
    return (rng.getrandbits(1) << (width-1)) | (rng.randrange(2*bias+1) << fraction) | rng.getrandbits(fraction)

for width, fraction, bias in [(32, 23, 127), (64, 52, 1023)]:
    one, sign = bias << fraction, 1 << (width-1)
    halfway = (bias-fraction-1) << fraction
    for rounding, mode in modes:
        for offset in [-1, 0, 1]:
            for negative in [0, sign]:
                append(width, 'add', rounding, mode, one | negative, (halfway+offset) | negative)
        # Odd significands make ties-to-even round the other way.
        append(width, 'add', rounding, mode, one+1, halfway)
        append(width, 'sub', rounding, mode, (1 << fraction), 1)
        for x in [one-1, one+(1 << (fraction-1)), sign | (one+(1 << (fraction-1)))]:
            append(width, 'round_int', rounding, mode, x)
        for x in [one-1, one+(1 << (fraction-1)), sign | (one+(1 << (fraction-1))),
                  (bias-1) << fraction, sign | ((bias-1) << fraction),
                  sign | ((bias+31) << fraction)]:
            append(width, 'to_i32', rounding, mode, x, result_width=32)
        for x in [0x01000001, 0x7fffffff, 0x80000000, 0xffffffff]:
            append(width, 'from_i32', rounding, mode, x)
        if width == 64:
            for x in [0x80000001, 0xffffffff]:
                append(width, 'from_u32', rounding, mode, x)
            # Double values immediately below, at and above a single tie,
            # and the single normal/subnormal rounding boundary.
            for x in [(1023 << 52)+(1 << 28)+d for d in [-1, 0, 1]] + [(897 << 52)-1, 1, 0x47effffff0000000]:
                append(width, 'to_f32', rounding, mode, x, result_width=32)
        else:
            for x in [1, 0x007fffff, 0x00800000, 0x7f7fffff]:
                append(width, 'to_f64', rounding, mode, x, result_width=64)
    for i in range(40):
        rounding, mode = modes[i % 4]
        op = ['add', 'sub', 'mul', 'div', 'sqrt'][i % 5]
        x, y = finite(width), finite(width)
        if op == 'sqrt': x &= sign-1
        append(width, op, rounding, mode, x, y, group='random')
    for i in range(8):
        rounding, mode = modes[i % 4]
        integer_input=(rng.getrandbits(1) << (width-1)) | ((bias+rng.randrange(-5,30)) << fraction) | rng.getrandbits(fraction)
        append(width, 'to_i32', rounding, mode, integer_input, result_width=32, group='random')
        append(width, 'to_f64' if width == 32 else 'to_f32', rounding, mode,
               finite(width), result_width=64 if width == 32 else 32, group='random')

# Non-finite expectations retain the raw SoftFloat answer. The guest generator
# applies documented architecture rules separately; invalid integer stores are
# never executed (T9000 ISM 11.11.2).
for width, fraction, bias in [(32,23,127),(64,52,1023)]:
    exponent=(2*bias+1)<<fraction
    quiet=1<<(fraction-1)
    sign=1<<(width-1)
    for rounding, mode in modes:
        for value in [exponent|quiet|1, exponent|1, sign|exponent|quiet|1, sign|exponent|1]:
            append(width, 'to_f64' if width==32 else 'to_f32', rounding, mode,
                   value, result_width=64 if width==32 else 32, group='nan')
            vectors[-1]['nan_input']=True
        for value in [exponent|quiet|1, exponent|1, (bias+31)<<fraction]:
            append(width, 'to_i32', rounding, mode, value, result_width=32, group='invalid')
            vectors[-1]['check_result']=False
        if width==64:
            for value in [0x47f0000000000000,0xc7f0000000000000]:
                append(width,'to_f32',rounding,mode,value,result_width=32,group='overflow')

if a.random_only:
    vectors = [v for v in vectors if v['name'].startswith('softfloat-random-')]

with tempfile.TemporaryDirectory(prefix='fpu-softfloat-') as temporary:
    work = Path(temporary)
    source, build = work/'source', work/'build'
    source.mkdir()
    build.mkdir()
    # Support the user's flattened source tree and the upstream source/ layout.
    original = src/'source' if (src/'source').is_dir() else src
    for path in original.glob('*.c'):
        shutil.copyfile(path, source/path.name)
    for name in ['include', '8086-SSE']:
        shutil.copytree(original/name, source/name)
    for name in ['Makefile', 'platform.h']:
        shutil.copyfile(src/'build/Linux-x86_64-GCC'/name, build/name)
    digest = hashlib.sha256()
    for path in sorted(work.rglob('*')):
        if path.is_file():
            digest.update(path.relative_to(work).as_posix().encode() + b'\0')
            digest.update(path.read_bytes())
    with (work/'build.log').open('w') as log:
        result = subprocess.run(['make', '-C', str(build), 'SOURCE_DIR=../source', '-j8'],
                                stdout=log, stderr=subprocess.STDOUT)
    if result.returncode:
        raise RuntimeError((work/'build.log').read_text()[-6000:])
    oracle = work/'oracle'
    subprocess.run(['gcc', '-O2', '-DSOFTFLOAT_FAST_INT64', '-I'+str(source/'include'),
                    str(here/'softfloat_oracle.c'), str(build/'softfloat.a'), '-o', str(oracle)],
                   check=True)
    request = ''.join(f"{v['width']} {v['op']} {v['mode']} {v['a']} {v['b']}\n" for v in vectors)
    lines = subprocess.check_output([str(oracle)], input=request, text=True).splitlines()
    assert len(lines) == len(vectors)
    for vector, line in zip(vectors, lines):
        result, flags = line.split()
        vector.update(result=result, flags=int(flags))
        del vector['mode']
    document = dict(oracle='Berkeley SoftFloat 3e API; supplied source tree',
                    source_sha256=digest.hexdigest(), specialization='8086-SSE',
                    adapter_sha256=hashlib.sha256((here/'softfloat_oracle.c').read_bytes()).hexdigest(),
                    tininess='after rounding', random_seed=seed, vectors=vectors)
    a.out.write_text(json.dumps(document, indent=2)+'\n', encoding='utf-8')
print(f'{len(vectors)} independent reference vectors written to {a.out}')
