#!/usr/bin/env python3
"""reduce.py - shrink a program that transputer-tcc gets wrong.

Removes chunks of lines (64, 32, ... 1 at a time, repeated until nothing more
goes) and keeps a cut only while the program stays interesting:

  mismatch  (default) host gcc builds it cleanly - no UBSan report, no
            uninitialised-use warning, -O0 and -O2 agree - and transputer-tcc
            compiles it but T4 prints something else;
  crash     transputer-tcc itself dies on a signal.

usage: reduce.py PROG.c OUT.c [crash]       (run in WSL/Linux)
env:   TCC_BIN, NETLOAD_BIN, T4_BIN as for the c-testsuite runner
"""
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
SUITE = os.path.dirname(HERE)
TCC = os.environ.get('TCC_BIN') or os.path.join(SUITE, '..', '..', 'transputer-tcc')
NETLOAD = os.environ.get('NETLOAD_BIN') or os.path.join(os.path.dirname(TCC), 'transputer-netload')
T4 = os.environ.get('T4_BIN') or os.path.join(SUITE, '..', 't4', 't4')
LIBTCC1 = os.path.join(os.path.dirname(TCC), 'transputer-libtcc1.a')
INC = os.path.join(SUITE, 'include')
BOOT = os.path.join(SUITE, 'test_boot_main.o')
RUNTIME = os.path.join(SUITE, 'test_runtime.o')

WORK = tempfile.mkdtemp(prefix='reduce-')
tests = 0


def run(cmd, timeout=30, **kw):
    try:
        return subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                              timeout=timeout, **kw)
    except subprocess.TimeoutExpired:
        return None


def host_output(src):
    exe0, exe2 = os.path.join(WORK, 'h0'), os.path.join(WORK, 'h2')
    r = run(['gcc', '-std=gnu99', '-O0', '-w', '-fsanitize=undefined,float-cast-overflow',
             '-fno-sanitize-recover=all', '-o', exe0, src])
    if not r or r.returncode:
        return None
    # a cut can leave a value-returning function without its return, or a
    # call to something deleted - both undefined, and UBSan sees neither
    r = run(['gcc', '-std=gnu99', '-O2', '-Werror=uninitialized', '-Werror=maybe-uninitialized',
             '-Werror=return-type', '-Werror=implicit-function-declaration',
             '-Werror=int-conversion', '-Werror=incompatible-pointer-types',
             '-o', exe2, src])
    if not r or r.returncode:
        return None
    a, b = run([exe0], 20), run([exe2], 20)
    if not a or not b or a.returncode or b.returncode or a.stdout != b.stdout:
        return None
    return a.stdout


def t4_output(src):
    obj, elf, btl = (os.path.join(WORK, n) for n in ('t.o', 't.elf', 't.btl'))
    uart = os.path.join(WORK, 'boot.uart')
    r = run([TCC, '-nostdlib', '-nostdinc', '-I' + INC, '-c', '-o', obj, src])
    if not r:
        return 'crash', None
    if r.returncode < 0 or r.returncode >= 128:
        return 'crash', None
    if r.returncode:
        return 'error', None
    libs = [LIBTCC1] if os.path.exists(LIBTCC1) else []
    r = run([TCC, '-nostdlib', '-nostdinc', '-o', elf, BOOT, obj, RUNTIME] + libs)
    if not r or r.returncode:
        return 'error', None
    run([NETLOAD, elf, btl])
    if os.path.exists(uart):
        os.unlink(uart)
    env = dict(os.environ, T4_UART_LOG=uart, DISPLAY='')
    run(['timeout', '-k', '5', '10', T4, '-s8', '-ss', '-se', '-sb', btl], 30, env=env)
    try:
        with open(uart, 'rb') as f:
            return 'ok', f.read()
    except OSError:
        return 'ok', b''


def interesting(lines, mode):
    global tests
    tests += 1
    src = os.path.join(WORK, 'cand.c')
    with open(src, 'w') as f:
        f.writelines(lines)
    if mode == 'crash':
        # stay a valid program, so the result can become a suite test
        r = run(['gcc', '-std=gnu99', '-fsyntax-only', '-w', '-Werror=return-type',
                 '-Werror=implicit-function-declaration', src])
        return r is not None and r.returncode == 0 and t4_output(src)[0] == 'crash'
    want = host_output(src)
    if want is None:
        return False
    status, got = t4_output(src)
    return status == 'ok' and got != want


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    mode = sys.argv[3] if len(sys.argv) > 3 else 'mismatch'
    with open(sys.argv[1]) as f:
        lines = f.readlines()
    if not interesting(lines, mode):
        sys.exit('reduce.py: the input is not interesting to begin with')
    changed = True
    while changed:
        changed = False
        for chunk in (64, 32, 16, 8, 4, 2, 1):
            i = 0
            while i < len(lines):
                cand = lines[:i] + lines[i + chunk:]
                if interesting(cand, mode):
                    lines = cand
                    changed = True
                    with open(sys.argv[2], 'w') as f:
                        f.writelines(lines)
                else:
                    i += chunk
            print('chunk %d: %d lines after %d tests' % (chunk, len(lines), tests), flush=True)
    with open(sys.argv[2], 'w') as f:
        f.writelines(lines)
    print('reduced to %d lines (%d tests)' % (len(lines), tests))


if __name__ == '__main__':
    main()
