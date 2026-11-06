#define main tinycc_driver_main
#include "tcc.c"
#undef main

static int check(int mode)
{
    const int ops[] = { TP_ENC_AJW, TP_ENC_CJ, TP_ENC_LDL, TP_ENC_LDLP,
        TP_ENC_OPR, TP_ENC_STL, TP_ENC_LDL, TP_ENC_LDLP, TP_ENC_OPR,
        TP_ENC_LDL, TP_ENC_OPR, TP_ENC_AJW, TP_ENC_OPR };
    int args[] = { -24, 0, 20, 18, TP_OPR_WSUB, 9, 20, 18,
        TP_OPR_FPLDNLSNI, 9, TP_FPU_STNLSN, 24, TP_OPR_RET };
    struct tp_relax_insn ins[13];
    struct local_patch first, second;
    int i, off = 0;
    memset(ins, 0, sizeof(ins)); memset(&first, 0, sizeof(first)); memset(&second, 0, sizeof(second));
    for (i = 0; i < 13; i++) {
        ins[i].old_off = off;
        ins[i].old_len = ins[i].new_len = tp_encode_len(ops[i], args[i]);
        ins[i].opcode = ops[i]; ins[i].operand = args[i]; off += ins[i].old_len;
    }
    ins[1].opcode = TP_ENC_J; ins[1].is_branch = 1;
    ins[1].target_old = mode == 1 ? ins[6].old_off : ins[2].old_off;
    if (mode == 4) {
        ins[1].opcode = TP_ENC_LDL; ins[1].operand = 3; ins[1].is_branch = 0;
    }
    if (mode == 2) ins[6].pinned = 1;
    first.instr_offset = ins[2].old_off; first.placeholder_size = ins[2].old_len;
    first.opcode = TP_OP_LDL; first.tcc_offset = -16; first.proven_nonvolatile_read = 1;
    second.instr_offset = ins[6].old_off; second.placeholder_size = ins[6].old_len;
    second.opcode = TP_OP_LDL; second.tcc_offset = -16; second.proven_nonvolatile_read = mode != 3;
    first.next = &second; local_patches = &first; func_start_ind = 0;
    func_frame_words = 24; func_frame_reserved = 10;
    func_max_stack_words = func_max_outgoing_words = 0; func_uses_inline_asm = 0;
    tps_mask = 0x3f7f;
    tp_sched_function(ins, 13, off, 0);
    return mode ? ins[6].opcode == TP_ENC_LDL
                : ins[6].opcode == TP_ENC_OPR && ins[6].operand == TP_OPR_DUP;
}
int main(void) {
    int i; for (i = 0; i < 5; i++) if (!check(i)) { printf("barrier mode %d failed\n", i); return 1; }
    puts("proof and barrier checks passed"); return 0;
}
