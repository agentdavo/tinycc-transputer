#!/usr/bin/env python3
"""Check volatile aggregate calls retain the original per-word moves.

Usage: python3 transputer/tests/host/test_struct_call_volatile.py [compiler]
"""
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[3]
compiler = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else root / 'transputer-tcc'
fixture = Path(__file__).with_name('struct_call_volatile_fixture.c')
names = {'from_volatile_global': 5, 'from_volatile_pointer': 5,
         'from_plain_pointer': 1, 'from_volatile_member': 4,
         'from_volatile_union': 2, 'from_pointer_to_volatile': 1}
with tempfile.TemporaryDirectory() as tmp:
    obj = Path(tmp) / 'fixture.o'
    subprocess.run([str(compiler), '-mt800', '-c', '-o', str(obj), str(fixture)], check=True)
    output = subprocess.check_output(
        [sys.executable, str(root / 'transputer/scripts/tpdis.py'), str(obj), *names], text=True)
    found = {}
    current = None
    for line in output.splitlines():
        if line.endswith(':') and line[:-1] in names:
            current = line[:-1]
            found[current] = 0
        elif current and line.strip().endswith(': move'):
            found[current] += 1
    if found != names:
        raise SystemExit(f'wrong MOVE counts: expected {names}, got {found}\n{output}')
    normal = output.split('from_plain_pointer:', 1)[1].split('from_volatile_member:', 1)[0]
    instructions = [line.split(':', 1)[1].strip() for line in normal.splitlines() if ':' in line]
    at = instructions.index('move')
    if at == 0 or instructions[at - 1] != 'ldc 17':
        raise SystemExit(f'ordinary MOVE must copy exactly 17 source bytes:\n{normal}')
print('volatile aggregate call code generation: PASS')
