/*
 * Expected: SGGUFPARTS-OK\nH
 *
 * test_gguf_decode_parts.c - llama.cpp's GGUF decoders (t4/tvx_gguf_decode.h,
 * verbatim dequantize_row_*) compiled for the part under test, run on real
 * Qwen3.8-27B blocks of 15 quantisation formats, and checked bit for bit
 * against the same C run on the host (gguf/qwen38_expected.inc, from
 * gguf/qwen38_ref.c).
 *
 * No TVX: this is plain C, so it runs on every part.  On the T9000, T805 and
 * T800 the float work goes through the FPU; with -mt425 or -mt414 through
 * lib/transputer-softfp.c, and the binary16 scales through
 * lib/transputer-f16.c everywhere.  Per entry it checks a hash of every
 * decoded binary32 value, so a single wrong bit in decode shows up, and the
 * dot against two activation vectors, one of which rounds on every step.
 *
 *   TP_EXTRA_CFLAGS=-mt414 T4_CPU=-s414 \
 *     bash transputer/tests/test_runner_with_uart.sh test_gguf_decode_parts
 *
 * Built with -nostdinc against lib/transputer-tvx/include, the libc-free
 * headers software TVX is built with (the runner adds it).
 */
#include "../t4/tvx_gguf_decode.h"
#include "gguf/qwen38_blocks.inc"
#include "gguf/qwen38_expected.inc"
#include "gguf/qwen38_act.h"

#define UART_DATA (*(volatile unsigned int *)0x10000000)
static void up(int c) { UART_DATA = (unsigned)(unsigned char)c; }
static void us(const char *s) { while (*s) up(*s++); }
static void uh(unsigned v)
{
    int i;
    for (i = 28; i >= 0; i -= 4) {
        int n = (v >> i) & 0xF;
        up(n < 10 ? '0' + n : 'a' + n - 10);
    }
}
static void ud(unsigned v)
{
    char b[12];
    int n = 0;
    if (!v) { up('0'); return; }
    while (v) { b[n++] = '0' + v % 10; v /= 10; }
    while (n) up(b[--n]);
}
static unsigned f2b(float f) { union { float f; unsigned u; } v; v.f = f; return v.u; }

static float dec[256];
static float a[256 * QW_NB];
static int bad;

static void check(int k, const char *what, unsigned got, unsigned want)
{
    if (got == want)
        return;
    bad++;
    us("\n"); ud((unsigned)k); us(" "); us(qw_name[k]); us(" "); us(what);
    us(" got="); uh(got); us(" want="); uh(want);
}

int test_main(void)
{
    int k, b, i, p;

    for (k = 0; k < QW_N; k++) {
        int fmt = qw_fmt[k], ne = qw_ne[k], bsz = tvx_gguf_block_bytes(fmt);
        unsigned h = QW_HASH_INIT;
        for (b = 0; b < QW_NB; b++) {
            tvx_gguf_decode_block(fmt, qw_blk[k] + b * bsz, dec);
            for (i = 0; i < ne; i++)
                h = qw_hash_step(h, f2b(dec[i]));
        }
        check(k, "decode", h, qw_hash[k]);
        for (p = 0; p < 2; p++) {
            float acc = 0.0f;
            qw_act(p, a, ne * QW_NB);
            for (b = 0; b < QW_NB; b++) {
                float t = 0.0f;
                tvx_gguf_decode_block(fmt, qw_blk[k] + b * bsz, dec);
                for (i = 0; i < ne; i++)
                    t = t + (float)(dec[i] * a[b * ne + i]);
                acc = acc + t;
            }
            check(k, p ? "dot_b" : "dot_a", f2b(acc), p ? qw_dot_b[k] : qw_dot_a[k]);
        }
    }
    if (bad) {
        us("\nGGUFPARTS: FAIL "); ud((unsigned)bad); us("\n");
    } else {
        us("GGUFPARTS-OK\n");
    }
    return bad;
}
