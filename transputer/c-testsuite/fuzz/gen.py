#!/usr/bin/env python3
"""gen.py - random C programs for differential testing of transputer-tcc.

Every program is free of undefined and unspecified behaviour, so host gcc and
transputer-tcc must print the same lines:

  * wrapping arithmetic is done in u32/u64 and converted back (conversion to
    a narrower signed type is implementation-defined, modular on both);
  * division and remainder go through guards for b == 0 and MIN / -1;
  * shift counts are masked to the width; only `>>` ever sees a signed left
    operand (implementation-defined: arithmetic on both);
  * `long` never appears (32 bits here, 64 on the host), nor does anything
    that prints a pointer, a size or an offset;
  * expressions call only pure functions, and an impure call's result goes
    to a plain local, so the order of evaluation cannot change a result;
  * float work is +, -, * and conversions, guarded back into int range.

It aims at the places a transputer backend breaks: 8/16-bit sign and zero
extension, 64-bit operations, calls inside expressions (the three-register
eval stack must spill), struct arguments and returns, varargs with mixed
widths, bitfields, dense and sparse switches, nested loops with break and
continue, and many-argument calls.

Functions have a call level - level 0 calls nothing, level n calls only
lower levels, and loops call only level 0 - so run time stays bounded.

usage: gen.py SEED [--no-float]   (prints the program on stdout)
"""
import random
import sys

INTS = ['i8', 'u8', 'i16', 'u16', 'i32', 'u32', 'i64', 'u64']
BITS = {'i8': 8, 'u8': 8, 'i16': 16, 'u16': 16, 'i32': 32, 'u32': 32,
        'i64': 64, 'u64': 64}
FLOATS = ['f32', 'f64']
SFIELDS = [('a', 'i8'), ('b', 'u16'), ('c', 'i32'), ('d', 'u64'), ('e', 'u8')]
BFIELDS = ['u1', 'u3', 'u7', 'u13', 's5', 'u8x']

PRELUDE = r'''#include <stdarg.h>
int printf(const char *, ...);
typedef signed char i8; typedef unsigned char u8;
typedef short i16; typedef unsigned short u16;
typedef int i32; typedef unsigned u32;
typedef long long i64; typedef unsigned long long u64;
typedef float f32; typedef double f64;
#define UDIV(W, a, b) ((W)(b) ? (W)(a) / (W)(b) : (W)(a))
#define UMOD(W, a, b) ((W)(b) ? (W)(a) % (W)(b) : (W)(a))
#define SDIV32(a, b) (((i32)(b) == 0 || ((i32)(a) == (i32)0x80000000U && (i32)(b) == -1)) ? (i32)(a) : (i32)(a) / (i32)(b))
#define SMOD32(a, b) (((i32)(b) == 0 || ((i32)(a) == (i32)0x80000000U && (i32)(b) == -1)) ? (i32)(a) : (i32)(a) % (i32)(b))
#define SDIV64(a, b) (((i64)(b) == 0 || ((i64)(a) == (i64)0x8000000000000000ULL && (i64)(b) == -1)) ? (i64)(a) : (i64)(a) / (i64)(b))
#define SMOD64(a, b) (((i64)(b) == 0 || ((i64)(a) == (i64)0x8000000000000000ULL && (i64)(b) == -1)) ? (i64)(a) : (i64)(a) % (i64)(b))
#define F2I(x) (((x) > -2147483648.0 && (x) < 2147483648.0) ? (i32)(x) : (i32)0)
struct S { i8 a; u16 b; i32 c; u64 d; u8 e; };
struct B { unsigned u1 : 1; unsigned u3 : 3; unsigned u7 : 7; unsigned u13 : 13; int s5 : 5; unsigned u8x : 8; };
static u64 crc = 0x6a09e667f3bcc908ULL;
static void mix(u64 v) { crc = (crc ^ v) * 0x100000001b3ULL; crc ^= crc >> 29; }
static void show(const char *tag) { printf("%s %08x%08x\n", tag, (u32)(crc >> 32), (u32)crc); }
static u64 vsum(int n, ...)
{
    va_list ap; u64 s = 0; int i;
    va_start(ap, n);
    for (i = 0; i < n; i++)
        s = s * 31 + ((i & 1) ? va_arg(ap, u64) : (u64)(u32)va_arg(ap, i32));
    va_end(ap);
    return s;
}
static struct S mk_s(u32 x, u64 y)
{
    struct S s;
    s.a = (i8)x; s.b = (u16)(x >> 8); s.c = (i32)(x * 2654435761U); s.d = y; s.e = (u8)(y >> 56);
    return s;
}
static void mix_s(struct S s) { mix((u64)(i64)s.a); mix(s.b); mix((u64)(i64)s.c); mix(s.d); mix(s.e); }
#include <string.h>
union U { u32 w; u16 h[2]; u8 b[4]; };
static u32 rec(u32 n, u32 acc) { return n ? rec(n - 1, acc * 31U + n) : acc; }
static f64 vsumd(int n, ...)
{
    va_list ap; f64 s = 0; int i;
    va_start(ap, n);
    for (i = 0; i < n; i++)
        s = s * 0.5 + va_arg(ap, f64);
    va_end(ap);
    return s;
}
'''


def lit(t, v):
    """a constant of integer type t, from any Python int"""
    b = BITS[t]
    v &= (1 << b) - 1
    return '((%s)0x%x%s)' % (t, v, 'ULL' if b == 64 else 'U')


class Fn:
    def __init__(self, idx, ret, params, pure, level):
        self.idx, self.ret, self.params = idx, ret, params
        self.pure, self.level = pure, level
        self.name = 'f%d' % idx


class Gen:
    def __init__(self, seed, use_float=True):
        self.r = random.Random(seed)
        self.use_float = use_float
        self.fns = []
        self.globals = {}          # name -> type
        self.arrays = {}           # name -> element type (8 elements)
        self.locals = {}           # declared locals of the current function
        self.larrays = {}
        self.cur = None
        self.loop_depth = 0

    # ---------------------------------------------------------------- values
    def edge(self, t):
        b, r = BITS[t], self.r
        k = r.randrange(10)
        if k == 0:
            return 0
        if k == 1:
            return 1
        if k == 2:
            return -1
        if k == 3:
            return 1 << (b - 1)                 # MIN for signed
        if k == 4:
            return (1 << (b - 1)) - 1           # MAX for signed
        if k == 5:
            return r.choice([0x7f, 0x80, 0xff, 0x7fff, 0x8000, 0xffff,
                             0x7fffffff, 0x80000000, 0xffffffff, 0x100000000])
        if k == 6:
            return r.randrange(-8, 9)
        return r.getrandbits(b)

    def scalars(self):
        """integer variables readable here: locals, params, globals"""
        pool = [(n, t) for n, t in self.locals.items() if t in INTS]
        if self.cur:
            pool += [(p, t) for p, t in self.cur.params if t in INTS]
        return pool + list(self.globals.items())

    def simple(self, t):
        """a variable or a constant - used for array indices"""
        pool = self.scalars()
        if not pool or self.r.randrange(3) == 0:
            return lit(t, self.edge(t))
        n, vt = self.r.choice(pool)
        return n if vt == t else '((%s)%s)' % (t, n)

    def leaf(self, t):
        r = self.r
        k = r.randrange(14)
        if k >= 10:
            s = self.simple('u32')
            if k == 10:                         # one storage, three views
                return r.choice(['((%s)g_u.w)' % t, '((%s)g_u.h[%s & 1])' % (t, s),
                                 '((%s)g_u.b[%s & 3])' % (t, s)])
            if k == 11:
                return '((%s)gm[%s & 3][%s %% 5])' % (t, s, self.simple('u32'))
            if k == 12:                         # an indirect call
                return '((%s)fptab[%s & 3](%s))' % (t, s, self.simple('u32'))
            return '((%s)rec(%s & 15, %s))' % (t, s, self.simple('u32'))
        if k < 3:
            return lit(t, self.edge(t))
        if k == 3:
            arrs = list(self.arrays.items()) + list(self.larrays.items())
            if arrs:
                n, _ = r.choice(arrs)
                return '((%s)%s[(u32)%s & 7])' % (t, n, self.simple('u32'))
        if k == 4:
            f, _ = r.choice(SFIELDS)
            sp = [p for p, pt in (self.cur.params if self.cur else []) if pt == 'S']
            if sp and r.randrange(2):
                return '((%s)%s.%s)' % (t, r.choice(sp), f)
            return '((%s)g_s.%s)' % (t, f)
        if k == 5:
            return '((%s)g_b.%s)' % (t, r.choice(BFIELDS))
        return self.simple(t)

    def wide(self, t):
        return 'u64' if BITS[t] == 64 or self.r.randrange(4) == 0 else 'u32'

    def anyint(self, d):
        return self.expr(self.r.choice(INTS), d)

    def expr(self, t, depth):
        """an expression whose value has integer type t"""
        r = self.r
        if depth <= 0 or r.randrange(7) == 0:
            return self.leaf(t)
        d = depth - 1
        k = r.randrange(18)
        if k <= 3:
            w = self.wide(t)
            op = r.choice(['+', '-', '*', '&', '|', '^', '+', '-'])
            return '((%s)((%s)%s %s (%s)%s))' % (t, w, self.anyint(d), op, w, self.anyint(d))
        if k == 4:
            w = self.wide(t)
            return '((%s)((%s)%s %s ((u32)%s & %d)))' % (
                t, w, self.anyint(d), r.choice(['<<', '>>']), self.anyint(d), BITS[w] - 1)
        if k == 5:
            s = r.choice(['i32', 'i64', 'i32'])
            return '((%s)((%s)%s >> ((u32)%s & %d)))' % (
                t, s, self.anyint(d), self.anyint(d), BITS[s] - 1)
        if k == 6:
            m = r.choice(['UDIV(u32, ', 'UMOD(u32, ', 'UDIV(u64, ', 'UMOD(u64, ',
                          'SDIV32(', 'SMOD32(', 'SDIV64(', 'SMOD64('])
            return '((%s)%s%s, %s))' % (t, m, self.anyint(d), self.anyint(d))
        if k == 7:
            w = self.wide(t)
            return '((%s)(%s(%s)%s))' % (t, r.choice(['~', '-', '!']), w, self.anyint(d))
        if k == 8:
            s = r.choice(INTS)
            return '((%s)((%s)%s %s (%s)%s))' % (
                t, s, self.anyint(d), r.choice(['<', '<=', '>', '>=', '==', '!=']),
                s, self.anyint(d))
        if k == 9:
            return '((%s)(%s %s %s))' % (t, self.anyint(d), r.choice(['&&', '||']), self.anyint(d))
        if k == 10:
            return '((%s)(%s ? %s : %s))' % (t, self.anyint(d), self.expr(t, d), self.expr(t, d))
        if k == 11:
            return '((%s)(%s)%s)' % (t, r.choice(INTS), self.anyint(d))
        if k == 12 and self.use_float:
            return '((%s)F2I(%s))' % (t, self.fexpr(r.choice(FLOATS), min(d, 2)))
        if k in (13, 14):
            f = self.callee(pure_only=True, ints_only=True)
            if f:
                if k == 13:
                    return '((%s)%s)' % (t, self.call(f))
                # a call as one operand of many: the eval stack must spill
                w = self.wide(t)
                return '((%s)((%s)%s %s ((%s)%s * (%s)%s)))' % (
                    t, w, self.anyint(d), r.choice(['+', '^']), w, self.call(f), w,
                    self.anyint(d))
        return self.leaf(t)

    def fexpr(self, t, depth):
        r = self.r
        if depth <= 0 or r.randrange(4) == 0:
            if r.randrange(3) == 0:
                return '((%s)%s)' % (t, r.choice(['0.5', '1.25', '-3.0', '1024.0',
                                                  '0.0', '-0.75', '6.5e3']))
            return '((%s)%s)' % (t, self.leaf(r.choice(['i8', 'u8', 'i16', 'u16'])))
        d = depth - 1
        k = r.randrange(5)
        if k <= 2:
            return '((%s)(%s %s %s))' % (t, self.fexpr(t, d), r.choice(['+', '-', '*']),
                                         self.fexpr(r.choice(FLOATS), d))
        if k == 3:
            return '((%s)%s)' % (t, self.fexpr(r.choice(FLOATS), d))
        return '((%s)(%s < %s ? %s : %s))' % (
            t, self.fexpr(t, d), self.fexpr(t, d), self.fexpr(t, d), self.fexpr(t, d))

    def callee(self, pure_only, ints_only):
        lvl = self.cur.level if self.cur else 99
        c = [f for f in self.fns
             if f.level < lvl and (f.pure or not pure_only)
             and (f.ret in INTS or not ints_only)
             and (self.loop_depth == 0 or f.level == 0)]
        if not c or self.r.randrange(3) == 0:
            return None
        return self.r.choice(c)

    def call(self, f):
        args = []
        for _, pt in f.params:
            if pt == 'S':
                args.append('mk_s(%s, %s)' % (self.expr('u32', 1), self.expr('u64', 1)))
            else:
                args.append(self.expr(pt, 1))
        return '%s(%s)' % (f.name, ', '.join(args))

    # ------------------------------------------------------------ statements
    def lvalue(self):
        """(text, type); type None means a bitfield of g_b"""
        r = self.r
        k = r.randrange(10)
        if k < 5:
            n = r.choice(list(self.locals))
            return n, self.locals[n]
        if k < 7 and self.larrays:
            n = r.choice(list(self.larrays))
            return '%s[(u32)%s & 7]' % (n, self.simple('u32')), self.larrays[n]
        if not self.cur.pure:
            k = r.randrange(7)
            if k == 5:
                s = self.simple('u32')
                return r.choice([('g_u.w', 'u32'), ('g_u.h[%s & 1]' % s, 'u16'),
                                 ('g_u.b[%s & 3]' % s, 'u8')])
            if k == 6:
                return 'gm[%s & 3][%s %% 5]' % (self.simple('u32'), self.simple('u32')), self.g2t
            if k == 0 and self.arrays:
                n = r.choice(list(self.arrays))
                return '%s[(u32)%s & 7]' % (n, self.simple('u32')), self.arrays[n]
            if k == 1:
                f, ft = r.choice(SFIELDS)
                return 'g_s.%s' % f, ft
            if k == 2:
                return 'g_b', None
            n = r.choice(list(self.globals))
            return n, self.globals[n]
        n = r.choice(list(self.locals))
        return n, self.locals[n]

    def plain_local(self):
        n = self.r.choice(list(self.locals))
        return n, self.locals[n]

    def assign(self, ind, depth):
        r = self.r
        lv, t = self.lvalue()
        if t is None:
            if r.randrange(3) == 0:
                # a signed bitfield only ever gets an in-range value
                return '%sg_b.s5 = (i32)((u32)%s & 31) - 16;' % (ind, self.expr('u32', depth))
            f = r.choice(['u1', 'u3', 'u7', 'u13', 'u8x'])
            return '%sg_b.%s = (u32)%s;' % (ind, f, self.expr('u32', depth))
        k = r.randrange(8)
        e = self.expr(t, depth)
        if k == 0 and t in ('u32', 'u64'):
            return '%s%s %s %s;' % (ind, lv, r.choice(['+=', '-=', '*=', '&=', '|=', '^=']), e)
        if k == 1:
            return '%s%s %s %s;' % (ind, lv, r.choice(['&=', '|=', '^=']), e)
        if k == 2 and t not in ('i32', 'i64'):
            return '%s%s%s;' % (ind, lv, r.choice(['++', '--']))
        if k == 3 and t in ('u32', 'u64'):
            return '%s%s %s ((u32)%s & %d);' % (ind, lv, r.choice(['<<=', '>>=']), e, BITS[t] - 1)
        if k == 4:
            w = self.wide(t)
            return '%s%s = (%s)((%s)%s %s (%s)%s);' % (
                ind, lv, t, w, lv, r.choice(['+', '-', '*', '^']), w, e)
        return '%s%s = %s;' % (ind, lv, e)

    def block(self, ind, n, depth):
        out = []
        for _ in range(n):
            out += self.stmt(ind, depth)
        return out

    def stmt(self, ind, depth):
        r = self.r
        k = r.randrange(19)
        e = 2 if depth > 0 else 1
        if k < 6 or depth <= 0:
            return [self.assign(ind, e + 1)]
        if k == 6:
            out = ['%sif (%s) {' % (ind, self.anyint(e))]
            out += self.block(ind + '    ', r.randrange(1, 4), depth - 1)
            if r.randrange(2):
                out.append('%s} else {' % ind)
                out += self.block(ind + '    ', r.randrange(1, 3), depth - 1)
            out.append('%s}' % ind)
            return out
        if k in (7, 8) and self.loop_depth < 2:
            v = 'k%d' % self.loop_depth
            self.loop_depth += 1
            out = ['%sfor (%s = 0; %s < %d; %s++) {' % (ind, v, v, r.randrange(1, 7), v)]
            body = self.block(ind + '    ', r.randrange(1, 4), depth - 1)
            if r.randrange(3) == 0:
                body.insert(r.randrange(len(body) + 1), '%s    if ((u32)%s & 1) %s;' % (
                    ind, self.expr('u32', 1), r.choice(['break', 'continue'])))
            out += body + ['%s}' % ind]
            self.loop_depth -= 1
            return out
        if k == 9 and self.loop_depth < 2:
            v = 'w%d' % self.loop_depth
            self.loop_depth += 1
            out = ['%s%s = %d;' % (ind, v, r.randrange(1, 6)), '%sdo {' % ind]
            out += self.block(ind + '    ', r.randrange(1, 3), depth - 1)
            out.append('%s} while (--%s > 0);' % (ind, v))
            self.loop_depth -= 1
            return out
        if k == 10:
            dense = r.randrange(2)
            cases = sorted(r.sample(range(16) if dense else range(0, 4096, 37),
                                    r.randrange(2, 7)))
            out = ['%sswitch ((u32)%s & %d) {' % (ind, self.anyint(e), 15 if dense else 4095)]
            for c in cases:
                out.append('%scase %d:' % (ind, c))
                out += self.block(ind + '    ', r.randrange(1, 3), depth - 1)
                if r.randrange(4):
                    out.append('%s    break;' % ind)
            out.append('%sdefault:' % ind)
            out += self.block(ind + '    ', 1, depth - 1)
            out.append('%s}' % ind)
            return out
        if k == 11 and self.larrays:
            # walk a local array through a pointer
            n = r.choice(list(self.larrays))
            t = self.larrays[n]
            w = self.wide(t)
            return ['%s{ %s *p = &%s[(u32)%s & 3]; p[1] = (%s)((%s)p[0] + (%s)%s); *p ^= %s; }' % (
                ind, t, n, self.simple('u32'), t, w, w, self.expr(t, e), self.expr(t, 1))]
        if k == 12:
            # many mixed-width varargs
            args = ['(i32)%s' % self.expr('i32', 1) if i % 2 == 0 else
                    '(u64)%s' % self.expr('u64', 1) for i in range(r.randrange(1, 8))]
            n, t = self.plain_local()
            return ['%s%s = (%s)vsum(%d, %s);' % (ind, n, t, len(args), ', '.join(args))]
        if k == 13:
            f = self.callee(pure_only=self.cur.pure, ints_only=False)
            if f:
                n, t = self.plain_local()
                if f.ret == 'S':
                    return ['%s{ struct S t = %s; %s = (%s)((u64)(i64)t.a + t.b + (u64)(i64)t.c + t.d + t.e); }'
                            % (ind, self.call(f), n, t)]
                return ['%s%s = (%s)%s;' % (ind, n, t, self.call(f))]
        if k == 14 and self.use_float:
            n, t = self.plain_local()
            return ['%s%s = (%s)F2I(%s);' % (ind, n, t, self.fexpr(r.choice(FLOATS), 2))]
        if k == 15:
            # struct copies (a block move) and field updates
            n, t = self.plain_local()
            out = ['%s{' % ind,
                   '%s    struct S s1 = mk_s(%s, %s), s2;' % (ind, self.expr('u32', 1), self.expr('u64', 1)),
                   '%s    s2 = s1;' % ind,
                   '%s    s2.c ^= (i32)%s; s2.e += 3;' % (ind, self.expr('u32', 1))]
            if not self.cur.pure and r.randrange(2):
                out.append('%s    g_s = s2;' % ind)
            out += ['%s    %s = (%s)((u64)(i64)s2.c + s2.d + s1.b + s2.e);' % (ind, n, t),
                    '%s}' % ind]
            return out
        if k == 16:
            # byte-level copies between arrays of any element types
            dsts = list(self.larrays.items())
            if not self.cur.pure:
                dsts += list(self.arrays.items())
            if dsts:
                d, dt = r.choice(dsts)
                s, st = r.choice(dsts + list(self.arrays.items()))
                cap = 8 * min(BITS[dt], BITS[st]) // 8
                nb = r.randrange(1, cap + 1)
                kind = r.randrange(3)
                if kind == 0:
                    return ['%smemset(%s, (int)((u32)%s & 0xff), %d);' % (ind, d, self.expr('u32', 1), nb)]
                if d == s:          # overlapping: memmove, at an offset
                    off = r.randrange(1, 4) * BITS[dt] // 8
                    nb = min(nb, 8 * BITS[dt] // 8 - off)
                    return ['%smemmove((u8 *)%s + %d, %s, %d);' % (ind, d, off, s, nb)]
                return ['%smemcpy(%s, %s, %d);' % (ind, d, s, nb)]
        if k == 17 and self.use_float:
            # doubles and floats through varargs (floats are promoted)
            args = ['(f64)%s' % self.fexpr('f64', 1) if i % 2 == 0 else
                    '(f32)%s' % self.fexpr('f32', 1) for i in range(r.randrange(1, 6))]
            n, t = self.plain_local()
            return ['%s%s = (%s)F2I(vsumd(%d, %s));' % (ind, n, t, len(args), ', '.join(args))]
        return [self.assign(ind, e + 1)]

    # -------------------------------------------------------------- program
    def function(self, idx):
        r = self.r
        params = [('p%d' % i, 'S' if r.randrange(9) == 0 else r.choice(INTS))
                  for i in range(r.choice([0, 1, 2, 3, 3, 4, 5, 7]))]
        ret = 'S' if r.randrange(10) == 0 else r.choice(INTS)
        have = max([f.level for f in self.fns] + [-1])
        level = r.randrange(0, min(have + 1, 2) + 1)
        f = Fn(idx, ret, params, r.randrange(2) == 0, level)
        self.cur, self.locals, self.larrays = f, {}, {}
        sig = ', '.join(('struct S %s' % p) if pt == 'S' else '%s %s' % (pt, p)
                        for p, pt in params) or 'void'
        out = ['static %s %s(%s)' % ('struct S' if ret == 'S' else ret, f.name, sig), '{',
               '    int k0, k1, w0, w1;']
        for i in range(r.randrange(2, 6)):
            t = r.choice(INTS)
            init = self.expr(t, 2)             # sees only earlier locals
            out.append('    %s l%d = %s;' % (t, i, init))
            self.locals['l%d' % i] = t
        if r.randrange(2):
            t = r.choice(INTS)
            out.append('    %s la[8] = { %s };' % (t, ', '.join(lit(t, self.edge(t)) for _ in range(8))))
            self.larrays['la'] = t
        if not f.pure and r.randrange(3) == 0:
            # state that survives between calls - impure functions only, so
            # the order of calls is always fixed
            t = r.choice(INTS)
            out.append('    static %s st = %s;' % (t, lit(t, self.edge(t))))
            self.locals['st'] = t
        out += self.block('    ', r.randrange(3, 9) if level else r.randrange(2, 5), 2 if level else 1)
        out.append('    (void)k0; (void)k1; (void)w0; (void)w1;')
        if ret == 'S':
            out.append('    {')
            out.append('        struct S rs;')
            for fld, ft in SFIELDS:
                out.append('        rs.%s = %s;' % (fld, self.expr(ft, 2)))
            out.append('        return rs;')
            out.append('    }')
        else:
            terms = ['(u64)%s' % n for n in self.locals] + ['(u64)%s' % self.expr(ret, 2)]
            if self.larrays:
                terms.append('(u64)la[(u32)%s & 7]' % self.simple('u32'))
            out.append('    return (%s)(%s);' % (ret, ' ^ '.join(terms)))
        out += ['}', '']
        self.cur = None
        return f, out

    def program(self, nfn):
        r = self.r
        out = [PRELUDE]
        for i in range(r.randrange(4, 9)):
            t = r.choice(INTS)
            self.globals['g%d' % i] = t
            out.append('static %s g%d = %s;' % (t, i, lit(t, self.edge(t))))
        for i in range(r.randrange(1, 3)):
            t = r.choice(INTS)
            self.arrays['ga%d' % i] = t
            out.append('static %s ga%d[8] = { %s };' % (t, i, ', '.join(
                lit(t, self.edge(t)) for _ in range(8))))
        out.append('static struct S g_s = { %d, %d, %d, %dULL, %d };' % (
            r.randrange(-128, 128), r.randrange(65536), r.randrange(-2**31 + 1, 2**31),
            r.getrandbits(64), r.randrange(256)))
        out.append('static struct B g_b;')
        out.append('static union U g_u = { 0x%xU };' % r.getrandbits(32))
        self.g2t = r.choice(INTS)
        out.append('static %s gm[4][5] = { %s };' % (self.g2t, ', '.join(
            '{ %s }' % ', '.join(lit(self.g2t, self.edge(self.g2t)) for _ in range(5))
            for _ in range(4))))
        for i in range(4):
            out.append('static u32 h%d(u32 x) { return (x * 0x%xU) ^ (x >> %d) ^ 0x%xU; }' % (
                i, r.getrandbits(32) | 1, r.randrange(1, 31), r.getrandbits(32)))
        out.append('static u32 (*const fptab[4])(u32) = { h0, h1, h2, h3 };')
        out.append('')
        for i in range(nfn):
            f, lines = self.function(i)
            self.fns.append(f)
            out += lines
        out += ['int main(void)', '{', '    int i = 0;']
        self.cur, self.locals, self.larrays = None, {}, {}
        for f in self.fns:
            for _ in range(2):
                c = self.call(f)
                out.append('    %s;' % ('mix_s(%s)' % c if f.ret == 'S' else 'mix((u64)%s)' % c))
            out.append('    show("%s");' % f.name)
        for n in self.globals:
            out.append('    mix((u64)%s);' % n)
        for n in self.arrays:
            out.append('    for (i = 0; i < 8; i++) mix((u64)%s[i]);' % n)
        out.append('    mix_s(g_s);')
        out.append('    mix(g_u.w);')
        out.append('    for (i = 0; i < 20; i++) mix((u64)gm[i / 5][i % 5]);')
        out.append('    mix(g_b.u1); mix(g_b.u3); mix(g_b.u7); mix(g_b.u13); '
                   'mix((u64)(i64)g_b.s5); mix(g_b.u8x);')
        out += ['    show("end");', '    return 0;', '}']
        return '\n'.join(out) + '\n'


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    g = Gen(int(sys.argv[1]), use_float='--no-float' not in sys.argv)
    sys.stdout.write(g.program(g.r.randrange(6, 14)))


if __name__ == '__main__':
    main()
