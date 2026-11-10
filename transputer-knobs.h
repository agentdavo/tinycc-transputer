/*
 *  Transputer: the environment switches of the code generator and linker.
 *
 *  Every switch the transputer-* sources read is listed here once, with
 *  what it is for, and is read through tp_knob(), which asks the
 *  environment the first time and remembers the answer.  None changes
 *  what a default build produces; they exist to measure a change (A/B),
 *  to bisect a miscompile, or to trace.
 *
 *  How a switch is spelled is its user's business and is kept as it was:
 *  some are on when merely set, some when non-empty, some are off when
 *  they start with '0', some carry a number, a mask or a name to filter
 *  by.  The note says which.
 *
 *  tccgen.c reads a few T4_* switches of its own under
 *  TCC_TARGET_TRANSPUTER; they are not in this table.
 */
#ifndef TRANSPUTER_KNOBS_H
#define TRANSPUTER_KNOBS_H

#define TP_KNOBS(K) \
    /* code generation: turn one thing off, for an A/B run */ \
    K(TP_NO_CALLOPT, "set: the old call sequence (four-slot return capture, spill/reload staging)") \
    K(TP_NO_LDNLP, "set: adc, not ldnlp, for a pointer displacement") \
    K(TP_NO_STORE_SWAP, "set: store() computes the address before the value, as it used to") \
    K(TP_NO_EQC0, "set: keep the eager inversion of `x == 0`") \
    K(TP_NO_CMPFLIP, "set: the old lowering of a comparison against a literal") \
    K(TCC_TRANSPUTER_WSUB_OPT, "=0: no indexed pointer adds (wsub, wsubdb, bsub, ssub)") \
    K(TCC_TRANSPUTER_BSUB_OPT, "=0: no bsub pointer adds, no address-slot adds") \
    K(TCC_TRANSPUTER_FPLDNLI, "0...: keep `wsub; fpldnlsn' apart") \
    K(TCC_TRANSPUTER_LDIFF_CMP, "0...: unsigned compares flip signs instead of using ldiff") \
    K(TCC_TRANSPUTER_SHIFT_MASK, "0...: no byte load in place of a shift and mask") \
    K(TCC_TRANSPUTER_VPOP_DEAD, "0...: keep dead parking at statement end; 2...: report each removal") \
    K(TCC_TRANSPUTER_VPOP_ONLY, "name: dead-parking removal only in functions whose name contains it") \
    K(TCC_TRANSPUTER_FPSTACK, "0...: no FPU-stack residency; 2...: report") \
    K(TCC_TRANSPUTER_FPSTACK_ONLY, "name: FPU-stack residency only in functions whose name contains it") \
    K(TCC_TRANSPUTER_INLINE, "0...: no in-place expansion of tiny inline functions") \
    K(TCC_FRAME_POLICY, "0, 1 or 2: reserved frame words - legacy, max, by use (default 2)") \
    K(TP_T9K_MASK, "mask: the T9000 instruction groups to use (default 0x7f)") \
    K(TP_POOL, "n: bytes reserved for a loop's constant pool") \
    /* the passes over a finished function */ \
    K(TP_NO_RELAX, "set: no pass over the finished function at all (none of the four below)") \
    K(TP_NO_SCHED, "set: no evaluation-stack scheduling") \
    K(TP_SCHED_MASK, "mask: the scheduler rules to apply (default 0x3f7f)") \
    K(TP_SCHED_BISECT, "lo-hi: rule S11 only in functions whose name hashes into the range") \
    K(TP_NO_SLOTMAP, "set: no renumbering of workspace slots") \
    K(TCC_TRANSPUTER_GADDR, "0...: no cache of global addresses in frame words") \
    /* code generation: counts and traces */ \
    K(TP_RELAX_STATS, "set: bytes saved by relaxation, per function") \
    K(TP_SCHED_STATS, "set: scheduler rule counts, and why a function was declined") \
    K(TP_SLOTMAP_STATS, "set: slot renumbering counts") \
    K(TP_GADDR_STATS, "set: global-address cache counts") \
    K(TP_POOL_DEBUG, "set: trace the loop constant pool") \
    K(T4_DEBUG, "non-empty: general code-generator trace") \
    K(T4_GENOPI_DEBUG, "non-empty: trace gen_opi") \
    K(T4_OPF_DEBUG, "non-empty: trace gen_opf") \
    K(T4_LOAD_DEBUG, "non-empty: trace load()") \
    K(T4_STORE_DEBUG, "non-empty: trace store()") \
    K(T4_INSTR_DEBUG, "non-empty: trace each instruction emitted") \
    K(T4_EMIT_LOCAL_DEBUG, "n: trace local accesses") \
    K(T4_STRUCTCOPY_DEBUG, "non-empty: trace structure copies") \
    K(T4_CALLARG_DEBUG, "non-empty: trace call arguments") \
    K(T4_VT_LOCAL_DEBUG, "non-empty: trace VT_LOCAL operands") \
    K(TCC_TRANSPUTER_DEBUG_VT, "non-empty: the same as T4_VT_LOCAL_DEBUG") \
    K(T4_GEN_SYM, "name: limit the traces above to this symbol") \
    K(TCC_TRACE_FUNC, "name: trace the code generated for this function") \
    K(T4_NFIX_OPR_WARN, "n: non-zero warns where an nfix byte is followed by an opr") \
    K(T4_CALL_EMIT_TRACE, "n: trace the first n call, j and cj bytes emitted") \
    K(T4_CALL_EMIT_SHORT, "n: the same, for short encodings only") \
    K(T4_CALL_EMIT_FILTER, "name: limit T4_CALL_EMIT_SHORT to a file or function") \
    /* linker: behaviour */ \
    K(T4_XIP_LINK, "non-empty: execute-in-place layout, text at 0x7F001000") \
    K(T4_ICF, "0: no identical code folding") \
    K(T4_LINK_RELAX, "0: no call relaxation at the link") \
    K(T4_STRICT_RELOCS, "non-empty: no repair of a relocation's symbol by name") \
    K(T4_STRICT_LDC_ABS, "non-empty: no fallback for an absolute ldc that does not decode") \
    K(T4_STRICT_IPREL, "non-empty: no fallback for a call slot that does not decode") \
    K(T4_STRICT_UNRESOLVED_CALLS, "non-empty: an unresolved call is an error") \
    /* linker: counts and traces */ \
    K(T4_LINK_DEBUG, "non-empty: narrate the link") \
    K(T4_LINK_TRACE, "non-empty: trace relocations as they are applied") \
    K(T4_ICF_STATS, "non-empty: what identical code folding removed") \
    K(T4_RELAX_STATS, "non-empty: what call relaxation saved") \
    K(T4_CALL_AUDIT, "non-empty: decode every patched call and check its target") \
    K(T4_RELOC_FIX_DEBUG, "non-empty: trace relocation symbol repairs") \
    K(T4_IPREL_PATCH_TRACE, "non-empty: trace call-slot patches") \
    K(T4_IPREL_PATCH_DEBUG, "non-empty: more on call-slot patches") \
    K(T4_DATA_RELOC_DEBUG, "non-empty: trace relocations in data sections") \
    K(T4_ZERO_RELOC_DEBUG, "non-empty: report call relocations that resolve to zero") \
    K(T4_RELOC_RANGE_START, "address: with T4_LINK_TRACE, where the relocation trace starts") \
    K(T4_RELOC_RANGE_END, "address: with T4_LINK_TRACE, where it ends") \
    K(T4_POST_RELOC_DUMP_ADDR, "address: dump image bytes there after relocation") \
    K(T4_POST_RELOC_DUMP_LEN, "n: how many bytes (default 64)") \
    K(T4_DUMP_SYM, "name: print where this symbol landed") \
    /* linker: as each object file is loaded */ \
    K(T4_RELOC_LOG, "non-empty: log each relocation loaded") \
    K(T4_RELOC_SYM, "name: limit T4_RELOC_LOG to this symbol") \
    K(T4_RELOC_TYPE, "n: limit T4_RELOC_LOG to this relocation type") \
    K(T4_RELOC_LOAD_DUMP, "non-empty: with T4_RELOC_LOG, the bytes and their decoding") \
    K(T4_RELOC_SUMMARY, "non-empty: call relocations per object") \
    K(T4_RELOC_SYMINFO, "non-empty or name: symbol details for each relocation") \
    K(T4_RELOC_LDC_MISMATCH, "non-empty: report ldc relocations not on an ldc slot") \
    K(T4_SYMMERGE_DEBUG, "name: trace how symbols containing it are merged")

enum tp_knob_id {
#define K(name, note) TPK_##name,
    TP_KNOBS(K)
#undef K
    TPK_COUNT
};

/* The switch's value in the environment, or NULL if it is not set. */
static const char *tp_knob(int id)
{
    static const char *const name[TPK_COUNT] = {
#define K(name, note) #name,
        TP_KNOBS(K)
#undef K
    };
    static const char *value[TPK_COUNT];
    static unsigned char asked[TPK_COUNT];

    if (!asked[id]) {
        value[id] = getenv(name[id]);
        asked[id] = 1;
    }
    return value[id];
}

/* Set to something other than the empty string. */
static int tp_knob_on(int id)
{
    const char *v = tp_knob(id);
    return v && *v;
}

#endif /* TRANSPUTER_KNOBS_H */
