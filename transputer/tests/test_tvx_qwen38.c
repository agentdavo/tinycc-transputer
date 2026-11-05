/*
 * Expected: SQWEN38-OK\nH
 *
 * test_tvx_qwen38.c - QDOT on real Qwen3.8-27B blocks: 15 quantisation
 * formats, up to three tensors each, from the IQ1_M, Q5_K_M and MTP GGUFs
 * (gguf/qwen38_blocks.inc, extract_model_blocks.py).  Each dot is checked
 * bit for bit against llama.cpp's decoders run on the host in QDOT's order
 * (gguf/qwen38_expected.inc, qwen38_ref.c), with two activation vectors:
 * small dyadic steps, and full-mantissa values that round on every step.
 *
 * No Target line: at -mt9000tvx t4 executes QDOT, on every other part it is
 * software TVX (lib/transputer/transputer-tvx-qdot.c), against the same expectations.
 * test_gguf_decode_parts.c runs the same blocks through the C decoders on
 * every part, without TVX.
 */
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

static float a[256 * QW_NB];

int test_main(void)
{
    int k, p, bad = 0;

    for (k = 0; k < QW_N; k++) {
        for (p = 0; p < 2; p++) {
            unsigned got, want = p ? qw_dot_b[k] : qw_dot_a[k];
            qw_act(p, a, qw_ne[k] * QW_NB);
            __qsetfmt(qw_fmt[k]);
            got = f2b(__qdot(qw_blk[k], a, QW_NB, 0.0f));
            if (got != want) {
                bad++;
                us("\n"); ud((unsigned)k); us(" "); us(qw_name[k]);
                us(p ? " dot_b" : " dot_a");
                us(" got="); uh(got); us(" want="); uh(want);
            }
        }
    }
    if (bad) {
        us("\nQWEN38: FAIL "); ud((unsigned)bad); us("\n");
    } else {
        us("QWEN38-OK\n");
    }
    return bad;
}
