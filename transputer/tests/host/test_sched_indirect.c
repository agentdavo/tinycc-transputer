/* Host-side regression for workspace reads in the transputer scheduler. */
#define main tinycc_driver_main
#include "tcc.c"
#undef main

static int check(int base, int displacement, int should_keep)
{
    struct tp_relax_insn ins[8];
    const int ops[8] = { TP_ENC_AJW, TP_ENC_LDL, TP_ENC_STL,
        TP_ENC_LDLP, TP_ENC_LDNL, TP_ENC_STL, TP_ENC_AJW, TP_ENC_OPR };
    int args[8] = { -12, 14, 9, base, displacement, 7, 12, TP_OPR_RET };
    int i, off = 0;

    memset(ins, 0, sizeof(ins));
    for (i = 0; i < 8; i++) {
        ins[i].old_off = off;
        ins[i].old_len = ins[i].new_len = tp_encode_len(ops[i], args[i]);
        ins[i].opcode = ops[i];
        ins[i].operand = args[i];
        off += ins[i].old_len;
    }
    func_frame_words = func_frame_reserved = 12;
    func_max_stack_words = func_max_outgoing_words = 0;
    func_uses_inline_asm = tp_loc_marks_count = tp_loc_marks_bad = 0;
    tps_mask = 0;
    tp_sched_function(ins, 8, off, 0);
    return should_keep ? ins[1].opcode == TP_ENC_LDL &&
                         ins[2].opcode == TP_ENC_STL
                       : ins[1].opcode == -1 && ins[2].opcode == -1;
}

int main(void)
{
    if (!check(9, 0, 1) || !check(8, 1, 1) || !check(10, -1, 1) ||
        !check(9, 0x40000000, 1) || !check(8, 0, 0)) {
        fputs("scheduler indirect-read regression\n", stderr);
        return 1;
    }
    puts("scheduler indirect-read checks passed");
    return 0;
}
