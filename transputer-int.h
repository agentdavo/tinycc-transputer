/*
 *  Transputer code generator: integer operations.
 *
 *  Code, not declarations: transputer-gen.c includes this once.
 *
 *    tp_push_sv()            a vstack value onto the evaluation stack
 *    gen_ptradd_indexed()    pointer + index in one indexed instruction
 *    tp_ptradd_addr()        brackets an add whose result is an address
 *    tp_cmp_hint_branch_true()
 *    gen_opi()               every 32-bit integer operator
 *    gen_opl_shift()         64-bit shifts by a variable count
 */

/* Push a vstack entry's value onto the hardware eval stack.
 * Unlike load(REG_IRET, ...), this ALWAYS generates code to push
 * the value, even if it's conceptually "already in REG_IRET". */
static void tp_push_sv(SValue *sv) {
    int fr = sv->r;
    int v = fr & VT_VALMASK;
    int bt = sv->type.t & VT_BTYPE;
    int is_unsigned = (sv->type.t & VT_UNSIGNED) != 0 || bt == VT_BF16 || bt == VT_F16
                      || bt == VT_FLOAT16;
    int size = get_primitive_type_size(bt);

    if (size > 4)
        size = 4;   /* one word: the low half of a two-word value */

    /* DUP optimization: if AReg already holds the value we want, DUP it
     * instead of reloading from workspace. Only for word-size local lvals
     * and rvalues in workspace slots (the common case). */
    if (tp_areg_tag != TP_AREG_TAG_NONE && !tp_t414()) {
        int want_tag = TP_AREG_TAG_NONE;
        if ((fr & VT_LVAL) && v == VT_LOCAL && size == 4) {
            want_tag = TP_AREG_TAG_LOCAL_BASE + (int)sv->c.i;
        } else if (!(fr & VT_LVAL) && v < VT_CONST && v >= 0) {
            want_tag = TP_AREG_TAG_REG_BASE + v;
        }
        if (want_tag != TP_AREG_TAG_NONE && want_tag == tp_areg_tag) {
            Tp_DUP();
            tp_areg_tag = want_tag;  /* restore tag — DUP doesn't change AReg */
            return;
        }
    }
    tp_areg_invalidate();

    if (fr & VT_LVAL) {
        /* Lvalue: load from memory */
        if (v == VT_LOCAL) {
            if (size == 1) {
                /* Byte load: use byte-addressed LDLP + LB.
                 * LDL truncates non-word-aligned offsets, so sbuf[1]
                 * at byte offset -3 would read from the same word as
                 * sbuf[0] at offset -4.  LDLP + ADC + LB is correct
                 * for any byte position within a word. */
                emit_local_addr(sv->c.i);
                abcreg_push(1);
                if (!is_unsigned && tp_have_t9000_feat(TP_T9K_LOAD8)) {
                    Tp_LBX();
                } else {
                    Tp_LB();                    /* zero-extends */
                    if (!is_unsigned)
                        tp_sign_extend(8, 1);
                }
            } else if (size == 2 && tp_t9000_half_ok((int)sv->c.i, sv->r)) {
                emit_local_addr(sv->c.i);
                abcreg_push(1);
                if (is_unsigned) Tp_LS(); else Tp_LSX();
            } else if (size == 2) {
                tp_load_local_half((int)sv->c.i, is_unsigned);
                abcreg_push(1);         /* as the byte path above counts */
            } else if (sv->c.i & 3) {
                /* a word of a packed local (tp_word_unaligned) */
                int t = tp_park_new(4);
                emit_local_addr(sv->c.i);
                tp_park_op(TP_OP_STL, -1, t);
                tp_bytes_load(-1, t, 0);
            } else {
                /* Word or larger: LDL is correct (word-aligned). */
                emit_local_op(TP_OP_LDL, sv->c.i);
                abcreg_push(1);
            }
        } else {
            int addr_in_areg = 0;

            if (v == VT_CONST) {
                if (fr & VT_SYM) {
                    tp_emit_sym_reloc(sv, NULL, 0);
                    abcreg_push(1);
                    if (sv->c.i) Tp_ADC(sv->c.i);
                } else {
                    Tp_LDC(sv->c.i);
                }
                addr_in_areg = 1;
            } else if (v == VT_LLOCAL) {
                emit_local_op(TP_OP_LDL, sv->c.i);
                abcreg_push(1);
                addr_in_areg = 1;
            } else if (v < VT_CONST) {
                /* Pointer in workspace slot */
                Tp_LDL(v);
                addr_in_areg = 1;
            }

            if (addr_in_areg) {
                if (size == 1) {
                    if (!is_unsigned && tp_have_t9000_feat(TP_T9K_LOAD8)) {
                        Tp_LBX();
                    } else {
                        Tp_LB();                    /* zero-extends */
                        if (!is_unsigned)
                            tp_sign_extend(8, 1);
                    }
                } else if (size == 2 && tp_t9000_half_ptr_ok(sv->r,
                               (v == VT_CONST) ? (long long)sv->c.i : 0)) {
                    if (is_unsigned) Tp_LS(); else Tp_LSX();
                } else if (size == 2) {
                    /* LDNL word-aligns its address — the emulator masks with
                     * MemWordMask — so LDNL 0 on a halfword sitting at byte
                     * offset 2 mod 4 silently reads the *containing word*, and
                     * the 0xFFFF mask below then yields the neighbouring
                     * field. `struct { u16 a, b; }` passed as p->b returned
                     * p->a, with no diagnostic. Rebuild from two byte loads,
                     * exactly as the VT_LOCAL path above already does for the
                     * same reason. */
                    /* NOT FRAME_SLOT_SCRATCH/+1: those alias
                     * FRAME_SLOT_SPILL_A/SPILL_B, which hold the spilled
                     * A/B registers while a call's arguments are being
                     * staged — and staging arguments is exactly when this
                     * path runs. Use the dedicated 64-bit temp pair. */
                    tp_load_half_bytes(FRAME_SLOT_DBL_TMP);

                    /* two zero-extended bytes: already 0..0xFFFF */
                    if (!is_unsigned)
                        tp_sign_extend(16, 1);
                } else if (fr & VT_UNALIGNED) {
                    /* a word of a packed member: from its bytes */
                    int t = tp_park_new(4);
                    tp_park_op(TP_OP_STL, -1, t);
                    tp_bytes_load(-1, t, 0);
                } else {
                    Tp_LDNL(0);
                }
            }
        }
    } else {
        /* Rvalue */
        if (v == VT_CONST) {
            if (fr & VT_SYM) {
                tp_emit_sym_reloc(sv, NULL, 0);
                abcreg_push(1);
                if (sv->c.i) Tp_ADC(sv->c.i);
            } else {
                tp_ldc_pooled(sv->c.i);
            }
        } else if (v == VT_LOCAL) {
            /* VT_LOCAL rvalue is an address of a local object. */
            emit_local_addr(sv->c.i);
            abcreg_push(1);
        } else if (v == VT_LLOCAL) {
            /* Spilled address value in a local slot. */
            emit_local_op(TP_OP_LDL, sv->c.i);
            abcreg_push(1);
        } else if (v < VT_CONST) {
            /* Value in workspace slot */
            Tp_LDL(v);
        }
    }

    /* Set DUP tag for trackable cases */
    if ((fr & VT_LVAL) && v == VT_LOCAL && size == 4) {
        tp_areg_tag = TP_AREG_TAG_LOCAL_BASE + (int)sv->c.i;
    } else if (!(fr & VT_LVAL) && v < VT_CONST && v >= 0) {
        tp_areg_tag = TP_AREG_TAG_REG_BASE + v;
    } else {
        tp_areg_tag = TP_AREG_TAG_NONE;
    }
}

static int gen_ptradd_wsub_gv2(void);

/* A pointer-arithmetic base must be a genuine runtime value. A literal base
 * (VT_CONST with no symbol) is the offsetof idiom `((T *)0)->m`, which appears
 * inside constant expressions — emitting an instruction sequence for it makes
 * the expression non-constant and the parser rejects it. Let those fall through
 * to the generic lowering, which constant-folds. */
static int tp_operand_is_simple(const SValue *sv);
static void tp_push_simple(const SValue *sv);

static int tp_ptradd_base_ok(const SValue *sv)
{
    /* A symbol address is a real runtime base and is fine; a bare literal is
     * the offsetof idiom and must be left to constant folding. */
    if ((sv->r & VT_VALMASK) == VT_CONST)
        return (sv->r & VT_SYM) != 0;
    return 1;
}

/* Emit the absolute address of a symbol: a relocation plus the 8-nibble
 * placeholder the linker rewrites, matching load()/gen_store_addr. */
static void tp_push_sym_addr(const SValue *sv)
{
    tp_emit_sym_reloc(sv, NULL, 0);
    abcreg_push(1);
    if (sv->c.i)
        Tp_ADC((int)sv->c.i);
}

/* A local array's address can be pushed after the index with LDLP, just
 * as a global array's address can be pushed with a symbol relocation. */
static int tp_ptradd_base_is_simple(const SValue *sv)
{
    if (tp_operand_is_simple(sv))
        return 1;
    if (sv->r & (VT_LVAL | VT_MUSTCAST | VT_MUSTBOUND | VT_BOUNDED))
        return 0;
    if ((sv->r & (VT_VALMASK | VT_SYM)) != VT_LOCAL &&
        (sv->r & (VT_VALMASK | VT_SYM)) != (VT_CONST | VT_SYM))
        return 0;
    return get_primitive_type_size(sv->type.t & VT_BTYPE) == 4;
}

static void tp_push_base(const SValue *sv)
{
    if ((sv->r & (VT_VALMASK | VT_LVAL | VT_SYM)) == VT_LOCAL)
        emit_local_addr(sv->c.i);
    else if ((sv->r & VT_VALMASK) == VT_CONST && (sv->r & VT_SYM))
        tp_push_sym_addr(sv);
    else
        tp_push_simple(sv);
}

/* Can this SValue be pushed onto the eval stack with a single instruction? */
static int tp_operand_is_simple(const SValue *sv)
{
    int v = sv->r & VT_VALMASK;

    if (sv->r & (VT_SYM | VT_MUSTCAST | VT_MUSTBOUND | VT_BOUNDED))
        return 0;
    if (get_primitive_type_size(sv->type.t & VT_BTYPE) != 4)
        return 0;
    if (sv->r & VT_LVAL)
        return v == VT_LOCAL && !(sv->c.i & 3);    /* not a packed member */
    return v == VT_CONST || (v >= TREG_W0 && v < NB_REGS);
}

/* A cast can erase the lvalue's volatile qualifier.  Require the original
 * active local symbol too, and reject address-taken locals: a volatile access
 * could otherwise have been made through a cast pointer to that object. */
static int tp_simple_nonvolatile_local(const SValue *sv)
{
    Sym *s;
    if (sv->type.t & VT_VOLATILE)
        return 0;
    for (s = local_stack; s; s = s->prev)
        if (s == sv->sym && (s->r & (VT_VALMASK | VT_LVAL)) == (VT_LOCAL | VT_LVAL) &&
            s->c == sv->c.i && !(s->type.t & (VT_VOLATILE | VT_BITFIELD)) &&
            (s->type.t & VT_BTYPE) == VT_INT && !s->a.addrtaken)
            return 1;
    return 0;
}

static void tp_push_simple(const SValue *sv)
{
    int v = sv->r & VT_VALMASK;

    if (sv->r & VT_LVAL) {
        emit_local_op(TP_OP_LDL, sv->c.i);
        if (!nocode_wanted && local_patches && tp_simple_nonvolatile_local(sv))
            local_patches->proven_nonvolatile_read = 1;
        abcreg_push(1);
    } else if (v == VT_CONST) {
        tp_ldc_pooled((int)sv->c.i);
    } else {
        Tp_LDL(v);
    }
}

/* Reserve the result before pushing either operand: get_reg can emit a
 * spill. Reuse a consumed operand's slot only if no other value needs it. */
static int tp_ptradd_result_reg(void)
{
    SValue *sv;
    for (sv = vtop - 1; sv <= vtop; sv++) {
        int r = sv->r & VT_VALMASK;
        if (!(sv->r & VT_LVAL) && r >= TREG_W0 && r < NB_REGS &&
            !tp_reg_live_on_vstack(r, sv))
            return r;
    }
    return get_reg(RC_INT);
}

/* `base + constant` costs no code at all when the base is the address of a
 * local (a frame offset) or of a symbol (a link-time constant): the generic
 * lowering scales the index, and gen_opic folds it into the offset, so the
 * element is then one ldl/ldlp, or one ldnl off the symbol.  The indexed fast
 * paths would instead park the base in a register and add to it -
 * `float p[3]; p[0]` cost `ldlp 15; stl 9; ldc 0; stl 10; ldl 10; ldl 9;
 * wsub; stl 9` before the access.  The conditions are gen_opic's own. */
static int tp_ptradd_const_folds(void)
{
    int br = vtop[-1].r;

    if ((vtop[0].r & (VT_VALMASK | VT_LVAL | VT_SYM)) != VT_CONST)
        return 0;
    return (br & (VT_VALMASK | VT_LVAL)) == VT_LOCAL ||
           (br & (VT_VALMASK | VT_LVAL | VT_SYM)) == (VT_CONST | VT_SYM);
}

/* T800 fast path for pointer arithmetic on 4-byte (word) elements. */
static int gen_ptradd_wsub(void) {
    if (tp_ptradd_const_folds())
        return 0;
    /* Push index then base straight onto the eval stack, so `a[i]` becomes
     * LDL i; LDL a; WSUB; STL r. Routing through gv2 first spills both
     * operands to fresh slots and reloads them, doubling the instruction
     * count for the most common addressing form in the kernel and in
     * CoreMark's list and matrix loops. */
    if (!nocode_wanted && tp_ptradd_base_ok(&vtop[-1]) &&
        tp_ptradd_base_is_simple(&vtop[-1]) && tp_operand_is_simple(&vtop[0])) {
        SValue base = vtop[-1];
        SValue index = vtop[0];
        int iv = index.r & VT_VALMASK;
        int index_const = !(index.r & VT_LVAL) && iv == VT_CONST;
        int dest = tp_ptradd_result_reg();

        abcreg_stack_depth = 0;
        if (index_const) {
            /* Compiler writer's guide 5.4.2: the address of component e of a
             * word-addressed structure Wa is either `e; Wa; wsub` or, for a
             * literal subscript, simply `Wa; ldnlp e`. The second form drops
             * the LDC entirely.  Index 0 is just the base (no ldnlp 0). */
            tp_push_base(&base);
            if ((int)index.c.i != 0)
                emit_instr(TP_OP_LDNLP, (int)index.c.i);
        } else {
            /* Variable index: push index then base → `ldl i; ldl a; wsub`
             * so the T9000 grouper packs L2+A1 (textbook a[i] form). */
            tp_push_simple(&index);
            tp_push_base(&base);
            Tp_WSUB();
        }

        vtop--;
        vtop->r = dest;
        Tp_STL(dest);
        return 1;
    }
    return gen_ptradd_wsub_gv2();
}

static int gen_ptradd_wsub_gv2(void) {
    SValue sv_base, sv_index;
    int result_v;

    gv2(RC_INT, RC_INT);
    sv_base = vtop[-1];
    sv_index = vtop[0];

    abcreg_stack_depth = 0;
    tp_push_sv(&sv_index);  /* push index first so it lands in BReg */
    tp_push_sv(&sv_base);   /* AReg = base, BReg = index */

    Tp_WSUB();              /* result = base + index * 4 */
    vtop--;

    result_v = vtop->r & VT_VALMASK;
    if (result_v < VT_CONST) {
        Tp_STL(result_v);
    }
    return 1;
}

/* Pointer arithmetic on 1-byte elements.
 *
 * `bsub` computes AReg + BReg and is numerically identical to `sum`, so this
 * changes nothing about the result.  What it changes is *which pipeline stage
 * pays for it*: the T9000 grouper treats `bsub` as a stage-2 address
 * calculation (2 per cycle) and `sum` as a stage-4 ALU operation (1 per
 * cycle, the narrowest resource in the machine).  Measurement shows stage 2
 * has spare capacity - relaxing its limit from 2 to 4 changes nothing - so
 * moving byte-pointer arithmetic there is close to free, and it releases the
 * stage-4 slot for real work.
 *
 * Unlike WSUB there is no constant-index form: `ldnlp` scales by a word, so
 * it cannot express a byte displacement.  Constant indices therefore fall
 * through to the generic path, which folds them into a single `adc`.
 *
 * `bsub` is commutative, so unlike WSUB the operand order does not matter. */
static int gen_ptradd_bsub(void) {
    SValue sv_base, sv_index;
    int result_v;

    if (tp_ptradd_const_folds())
        return 0;
    if (!nocode_wanted && tp_ptradd_base_ok(&vtop[-1]) &&
        tp_ptradd_base_is_simple(&vtop[-1]) && tp_operand_is_simple(&vtop[0])) {
        SValue base = vtop[-1];
        SValue index = vtop[0];
        int iv = index.r & VT_VALMASK;
        int index_const = !(index.r & VT_LVAL) && iv == VT_CONST;
        int dest;

        if (index_const)
            return 0;           /* `adc n` is shorter; let the generic path run */

        dest = tp_ptradd_result_reg();
        abcreg_stack_depth = 0;
        tp_push_simple(&index);
        tp_push_base(&base);
        Tp_BSUB();

        vtop--;
        vtop->r = dest;
        Tp_STL(dest);
        return 1;
    }

    /* No gv2 fallback.  Unlike WSUB, BSUB buys nothing the generic path
     * cannot already do - `sum` is the same arithmetic - so the only reason
     * to emit it is the free stage-2 slot.  Forcing gv2 here to get it would
     * spill and reload both operands and lose the zero-STL local-to-local
     * lowering that gen_opi already applies, which measured as *more*
     * dynamic instructions than it saved.  Return 0 and let gen_op handle
     * it. */
    (void)sv_base; (void)sv_index; (void)result_v;
    return 0;
}

/* T800 fast path for pointer arithmetic on 8-byte elements.
 * Expects vstack: vtop[-1] = base pointer, vtop[0] = unscaled index.
 * TCWG wsubdb semantics: AReg = base, BReg = index → result = AReg + BReg * 8.
 * Returns 1 when emitted. */
static int gen_ptradd_wsubdb(void);

/* T9000 fast path for pointer arithmetic on 2-byte (16-bit) elements.
 * ssub: AReg := AReg + 2*BReg, i.e. the same operand order as WSUB.
 * Returns 0 without emitting anything when -mt9000 is off or when the
 * operands are not in the simple form, so the caller's generic
 * multiply-then-add lowering still runs. */
static int gen_ptradd_ssub(void) {
    if (!tp_have_t9000_feat(TP_T9K_SSUB))
        return 0;
    if (tp_ptradd_const_folds())
        return 0;
    if (!nocode_wanted && tp_ptradd_base_ok(&vtop[-1]) &&
        tp_ptradd_base_is_simple(&vtop[-1]) && tp_operand_is_simple(&vtop[0])) {
        SValue base = vtop[-1];
        SValue index = vtop[0];
        int iv = index.r & VT_VALMASK;
        int index_const = !(index.r & VT_LVAL) && iv == VT_CONST;
        int dest = tp_ptradd_result_reg();

        abcreg_stack_depth = 0;
        if (index_const) {
            /* Literal subscript: fold 2*k into the address as an ADC.
             * (LDNLP is word-scaled, so it cannot serve here.) */
            tp_push_base(&base);
            if (index.c.i)
                Tp_ADC((int)(2 * index.c.i));
        } else {
            tp_push_simple(&index);
            tp_push_base(&base);
            Tp_SSUB();
        }

        vtop--;
        vtop->r = dest;
        Tp_STL(dest);
        return 1;
    }
    return 0;
}

static int gen_ptradd_wsubdb(void) {
    SValue sv_base, sv_index;
    int result_v;

    if (tp_t414())
        return 0;               /* no wsubdb: the generic multiply */
    if (tp_ptradd_const_folds())
        return 0;

    /* Same direct form as gen_ptradd_wsub. */
    if (!nocode_wanted && tp_ptradd_base_ok(&vtop[-1]) &&
        tp_ptradd_base_is_simple(&vtop[-1]) && tp_operand_is_simple(&vtop[0])) {
        SValue base = vtop[-1];
        SValue index = vtop[0];
        int dest = tp_ptradd_result_reg();

        abcreg_stack_depth = 0;
        tp_push_simple(&index);
        tp_push_base(&base);
        Tp_WSUBDB();

        vtop--;
        vtop->r = dest;
        Tp_STL(dest);
        return 1;
    }

    gv2(RC_INT, RC_INT);
    sv_base = vtop[-1];
    sv_index = vtop[0];

    abcreg_stack_depth = 0;
    tp_push_sv(&sv_index);  /* push index first so it lands in BReg */
    tp_push_sv(&sv_base);   /* AReg = base, BReg = index */

    Tp_WSUBDB();            /* result = base + index * 8 */
    vtop--;

    /* Store result to workspace — matches gen_opi's post-switch STL.
     * Without this, subsequent load()/tp_push_sv() reads stale data
     * from the workspace slot assigned by gv2. */
    result_v = vtop->r & VT_VALMASK;
    if (result_v < VT_CONST) {
        Tp_STL(result_v);
    }
    return 1;
}

/* TCC_TRANSPUTER_WSUB_OPT=0 (bsub=0) or TCC_TRANSPUTER_BSUB_OPT=0 (bsub=1)
   turns an indexed pointer-add lowering off, for A/B measurement.  Read
   once: this is asked on every pointer addition. */
static int tp_ptradd_opt_off(int bsub)
{
    static int off[2] = { -1, -1 };
    if (off[bsub] < 0) {
        const char *e = tp_knob(bsub ? TPK_TCC_TRANSPUTER_BSUB_OPT
                                     : TPK_TCC_TRANSPUTER_WSUB_OPT);
        off[bsub] = e && !strcmp(e, "0");
    }
    return off[bsub];
}

/* base + index*elem_size in one indexed instruction - the whole point of
 * the transputer's indexed addressing.  vtop[-1] is the base, vtop[0] the
 * unscaled index.  Returns 0, with nothing emitted, where the element size
 * has no such instruction on this part or the generic multiply-then-add
 * does better. */
ST_FUNC int gen_ptradd_indexed(int elem_size)
{
    if (tp_ptradd_opt_off(0))
        return 0;
    switch (elem_size) {
    case 4:
        return gen_ptradd_wsub();
    case 8:
        return gen_ptradd_wsubdb();
    case 1:
        /* separately gateable so the A/B can be measured */
        return tp_ptradd_opt_off(1) ? 0 : gen_ptradd_bsub();
    case 2:
        /* T9000 only; a distinct element size from bsub's */
        return gen_ptradd_ssub();
    }
    return 0;
}

/* The add tccgen.c does between tp_ptradd_addr(1) and tp_ptradd_addr(0)
 * yields an address, so it can go on the T9000's spare stage-2 address
 * slot instead of the contended stage-4 ALU (tp_addr_disp_words). */
ST_FUNC void tp_ptradd_addr(int on)
{
    tp_ptr_add_is_addr = on && !tp_ptradd_opt_off(1);
}

static int tp_mul_const_strength(long c, int *out)
{
    if (c == -1 || c == 0 || c == 1 ||
        c == 2 || c == 3 || c == 4 || c == 8) {
        *out = (int)c;
        return 1;
    }
    if (c > 0 && (c & (c - 1)) == 0) {
        int shift = 0;
        long t = c;
        while (t > 1) {
            shift++;
            t >>= 1;
        }
        if (shift >= 4 && shift <= 31) {
            *out = 0x100 + shift;
            return 1;
        }
    }
    return 0;
}

/* ---- strict comparisons at a loop back-edge -----------------------------
 *
 * CJ branches when AReg == 0.  A loop back-edge wants to branch when the
 * condition is TRUE, so gvtst asks gjmp_cond to invert and gjmp_cond emits
 * `EQC 0` - one contended stage-4 slot on every iteration.  Worse, the EQC is
 * itself a stage-4 op, so it cannot share the compare's group and the CJ that
 * follows starts a third: the tail of a counted loop costs two groups where
 * one would do.
 *
 * The non-strict forms already avoid this.  TOK_GE/TOK_LE/TOK_UGE/TOK_ULE
 * compute the *complement* with a plain GT/GTU and tag the result
 * VT_CMP(TOK_EQ) - "zero is true" - which gjmp_cond consumes with no EQC at
 * all.  TOK_GT/TOK_LT/TOK_UGT/TOK_ULT did not, which is why `i <= 9999`
 * measured 25% faster than `i < 10000` for the identical loop.
 *
 * Against a literal the complement is free, because a strict comparison with
 * the bound moved by one is the negation of the same comparison reversed:
 *
 *     A >  K   ===  !(K+1 >  A)        K >  A   ===  !(A >  K-1)
 *
 * Swapping the two vstack entries and moving the literal by one costs no
 * instructions - both operands are already evaluated, exactly as the
 * TOK_LT/TOK_GE swap at the top of gen_opi relies on - and lets the result be
 * tagged "zero is true" like the non-strict forms.  For TOK_ULT it also drops
 * the REV, since the reversed form needs no operand exchange.
 *
 * Getting the polarity wrong is not a correctness bug: VT_CMP carries the
 * convention and both consumers (gjmp_cond and load()) handle either.  A
 * mispredicted consumer only moves the EQC, so the hint below can be
 * approximate. */

/* Set while a loop's controlling expression is generated. */
static int tp_cmp_branch_true;

ST_FUNC void tp_cmp_hint_branch_true(int on)
{
    tp_cmp_branch_true = on;
}

static int tp_cmp_is_word_literal(const SValue *sv)
{
    if ((sv->r & (VT_VALMASK | VT_LVAL | VT_SYM | VT_MUSTCAST |
                  VT_MUSTBOUND | VT_BOUNDED)) != VT_CONST)
        return 0;
    return get_primitive_type_size(sv->type.t & VT_BTYPE) == 4;
}

static void tp_cmp_swap_operands(void)
{
    SValue tmp = vtop[-1];
    vtop[-1] = vtop[0];
    vtop[0] = tmp;
}

/* Rewrite a strict comparison against a literal into its complement.  Returns
 * 1 when the rewrite happened, in which case the result must be tagged
 * VT_CMP(TOK_EQ) rather than VT_CMP(TOK_NE). */
/* TP_NO_CMPFLIP=1 restores the pre-fix lowering, so the change can be A/B
 * measured without rebuilding a different tree - same handle as TP_NO_CALLOPT
 * and TP_POOL. */
/* TP_NO_EQC0=1 keeps the `x == 0` eager inversion.  Separate from
 * TP_NO_CMPFLIP so the two compare changes can be attributed independently -
 * they are different mechanisms and, as it turned out, very different wins. */
static int tp_no_eqc0(void)
{
    static int v = -1;
    if (v < 0)
        v = tp_knob(TPK_TP_NO_EQC0) ? 1 : 0;
    return v;
}

static int tp_no_cmpflip(void)
{
    static int v = -1;
    if (v < 0)
        v = tp_knob(TPK_TP_NO_CMPFLIP) ? 1 : 0;
    return v;
}

static int tp_cmp_complement_vs_literal(int *op)
{
    if (!tp_cmp_branch_true || nocode_wanted || tp_no_cmpflip())
        return 0;

    switch (*op) {
    case TOK_GT:
        /* A > K  ===  !(K+1 > A) */
        if (tp_cmp_is_word_literal(&vtop[0])) {
            int k = (int)vtop[0].c.i;
            if (k == 0x7FFFFFFF)
                return 0;
            vtop[0].c.i = k + 1;
            tp_cmp_swap_operands();
            return 1;
        }
        /* K > A  ===  !(A > K-1) */
        if (tp_cmp_is_word_literal(&vtop[-1])) {
            int k = (int)vtop[-1].c.i;
            if (k == (int)0x80000000)
                return 0;
            vtop[-1].c.i = k - 1;
            tp_cmp_swap_operands();
            return 1;
        }
        return 0;

    case TOK_UGT:
        /* A >u K  ===  !(K+1 >u A) */
        if (tp_cmp_is_word_literal(&vtop[0])) {
            uint32_t k = (uint32_t)vtop[0].c.i;
            if (k == 0xFFFFFFFFu)
                return 0;
            vtop[0].c.i = (int)(k + 1);
            tp_cmp_swap_operands();
            return 1;
        }
        return 0;

    case TOK_ULT:
        /* A <u K  ===  !(A >u K-1), and the reversed form needs no REV. */
        if (tp_cmp_is_word_literal(&vtop[0])) {
            uint32_t k = (uint32_t)vtop[0].c.i;
            if (k == 0)
                return 0;
            vtop[0].c.i = (int)(k - 1);
            *op = TOK_UGT;
            return 1;
        }
        return 0;

    default:
        return 0;
    }
}

/* Take back the code from `off` (the start of an entry still in the
 * history window) so it can be emitted again differently.  The caller
 * adjusts the eval-stack model for what it removed. */
static void tp_roll_back_to(int off)
{
    ind = off;
    cur_text_section->data_offset = ind;
    tp_drop_local_patches_from(ind);
    tp_hist_rollback(ind);
    tp_areg_invalidate();
}

/* gtu (opr 0x5F) arrived with the T9000: the T414/T425/T800/T805
 * instruction sets have none (t4 accepts it in T800 mode regardless), so
 * for -mt800 and -mt425 flip the sign bit of both operands and let the
 * signed gt order them as unsigned values.  Entry AReg = R, BReg = L;
 * leaves AReg = (L >u R), or (R >u L) - what `rev; gtu` gives - when
 * `swapped`.  The extra mint pushes a third word: CReg is dead here, as the
 * operands are the only values gen_opi keeps on the stack.
 *
 * Better, when both operands were pushed by short runs the history window
 * can replay (a load and up to two ldnl/ldnlp/adc on it, tp_hist_value_run):
 * ldiff subtracts with a borrow, so with 0 pushed under them `ldc 0; X; Y;
 * ldiff` leaves the borrow of X - Y, X <u Y, in B, and `rev` brings it up.
 * 5 T800 cycles beside the pushes, where the sign flips cost 11, and every
 * part from the T414 has ldiff.  (Flipping a literal's sign at compile time
 * does not pay: a small K becomes an eight-byte constant.)
 * TCC_TRANSPUTER_LDIFF_CMP=0 keeps the sign flips. */
static int tp_hist_value_run(int i)
{
    int j;

    for (j = i; j < tp_hist_n && j < i + 3; j++) {
        if (j > i && tp_hist[j].end != tp_hist[j - 1].off)
            return -1;
        if (tp_hist_is_pure_push(&tp_hist[j]))
            return j;
        if (tp_hist[j].kind != TP_HIST_INSTR ||
            (tp_hist[j].opc != TP_OP_LDNL && tp_hist[j].opc != TP_OP_LDNLP &&
             tp_hist[j].opc != TP_OP_ADC))
            return -1;
    }
    return -1;
}

static void tp_emit_gtu(int swapped)
{
    static int ldiff_cmp = -1;
    int r_end, l_end;

    if (tp_have_t9000_feat(TP_T9K_GTU)) {
        if (swapped)
            Tp_REV();
        Tp_OPR(TP_OPR_GTU);
        return;
    }
    if (ldiff_cmp < 0) {
        const char *e = tp_knob(TPK_TCC_TRANSPUTER_LDIFF_CMP);
        ldiff_cmp = !(e && *e == '0');
    }
    /* R's run is hist[0..r_end], L's the one under it, ending at l_end. */
    if (ldiff_cmp && !nocode_wanted && tp_hist_n >= 2 && tp_hist[0].end == ind &&
        (r_end = tp_hist_value_run(0)) >= 0 && r_end + 1 < tp_hist_n &&
        tp_hist[r_end + 1].end == tp_hist[r_end].off &&
        (l_end = tp_hist_value_run(r_end + 1)) >= 0) {
        struct tp_hist_ent rr[3], ll[3];
        int nr = r_end + 1, nl = l_end - r_end, k;

        for (k = 0; k < nr; k++)
            rr[k] = tp_hist[r_end - k];         /* oldest first */
        for (k = 0; k < nl; k++)
            ll[k] = tp_hist[l_end - k];
        tp_roll_back_to(tp_hist[l_end].off);
        abcreg_pop(2);
        Tp_LDC(0);                              /* no borrow in */
        /* (L >u R) is R <u L: X = R, Y = L; swapped wants L <u R */
        for (k = 0; k < (swapped ? nl : nr); k++)
            tp_hist_replay(swapped ? &ll[k] : &rr[k]);
        for (k = 0; k < (swapped ? nr : nl); k++)
            tp_hist_replay(swapped ? &rr[k] : &ll[k]);
        Tp_OPR(TP_OPR_LDIFF);                   /* A = X - Y, B = X <u Y */
        Tp_REV();
        abcreg_pop(2);                          /* the 0 and one operand */
        return;
    }
    Tp_LDC(-0x7fffffff - 1);                    /* mint */
    Tp_XOR();                                   /* A = R^M, B = L */
    Tp_REV();
    Tp_LDC(-0x7fffffff - 1);
    Tp_XOR();                                   /* A = L^M, B = R^M */
    if (!swapped)
        Tp_REV();
    Tp_GT();
}

/* (x >> K) & m with a mask that keeps only bits the shift brings down from
 * x itself (m < 2^(32 - K)) is the same for an arithmetic and a logical
 * shift.  A signed >> K is xdble; ldc K; lshr (there is no sign-filling
 * shift), so for these masks it becomes ldc K; shr: one instruction and a
 * long shift fewer.  Doom's texture lookups, (frac >> 16) & 127, do this
 * every pixel.
 *
 * Better still on the T800, T805, T425 and T414, whose shifts take a cycle
 * a place (shr 16 is 18 cycles): when K is 8, 16 or 24, m fits in a byte
 * and x came straight from a workspace word, the answer is byte K / 8 of
 * that word, so ldlp x; adc K/8; lb (7 cycles) replaces ldl x ... shr (21),
 * and the and goes too when m is 255.  The T9000 shifts in a cycle and
 * keeps the shift.
 *
 * Called with `ldc m` just emitted for the `and` that follows.  Returns 0
 * to leave the code alone, 1 with the code rewritten and the and still
 * wanted, 2 when the and is not.  TCC_TRANSPUTER_SHIFT_MASK=0 turns it
 * off for an A/B run. */
static int tp_shift_mask_fold(void)
{
    static int enabled = -1;
    struct tp_hist_ent x;
    unsigned int m, k;
    int i, sar, nwin, roll;

    if (enabled < 0) {
        const char *e = tp_knob(TPK_TCC_TRANSPUTER_SHIFT_MASK);
        enabled = !(e && *e == '0');
    }
    if (!enabled || nocode_wanted || tp_hist_n < 3)
        return 0;
    if (tp_hist[0].kind != TP_HIST_INSTR || tp_hist[0].opc != TP_OP_LDC ||
        tp_hist[0].end != ind)
        return 0;
    m = (unsigned int)tp_hist[0].val;
    if (tp_hist[1].kind == TP_HIST_INSTR && tp_hist[1].opc == TP_OP_OPR &&
        tp_hist[1].val == TP_OPR_LSHR && tp_hist_n >= 4 &&
        tp_hist[3].kind == TP_HIST_INSTR && tp_hist[3].opc == TP_OP_OPR &&
        tp_hist[3].val == TP_OPR_XDBLE)
        sar = 1;
    else if (tp_hist[1].kind == TP_HIST_INSTR && tp_hist[1].opc == TP_OP_OPR &&
             tp_hist[1].val == TP_OPR_SHR)
        sar = 0;
    else
        return 0;
    if (tp_hist[2].kind != TP_HIST_INSTR || tp_hist[2].opc != TP_OP_LDC)
        return 0;
    k = (unsigned int)tp_hist[2].val;
    if (k < 1 || k > 31 || (m >> (32 - k)) != 0)
        return 0;
    nwin = sar ? 4 : 3;                 /* ldc m back to the shift's start */
    for (i = 0; i + 1 < nwin; i++)
        if (tp_hist[i].off != tp_hist[i + 1].end)
            return 0;

    /* The byte form needs x's own load just before the shift. */
    if (!tp_isa_has(TP_EF_T9000) &&
        (k == 8 || k == 16 || k == 24) && m <= 0xff &&
        tp_hist_n > nwin && tp_hist[nwin].end == tp_hist[nwin - 1].off &&
        tp_hist[nwin].opc == TP_OP_LDL &&
        (tp_hist[nwin].kind == TP_HIST_LOCAL ||
         tp_hist[nwin].kind == TP_HIST_INSTR)) {
        x = tp_hist[nwin];
        roll = x.off;
        abcreg_pop(2);                  /* x and m are rolled back */
    } else if (sar) {
        x.kind = -1;
        roll = tp_hist[3].off;
        abcreg_pop(1);                  /* m is; the value x stays in A */
    } else
        return 0;

    tp_roll_back_to(roll);

    if (x.kind >= 0) {
        x.opc = TP_OP_LDLP;
        tp_hist_replay(&x);
        Tp_ADC(k / 8);
        Tp_LB();
        tp_areg_invalidate();
        if (m == 0xff)
            return 2;
    } else {
        Tp_LDC(k);
        Tp_SHR();
    }
    Tp_LDC(m);
    return 1;
}

/* tp_hist[a] is `and` and tp_hist[a + 1] the `ldc k`, 0 <= k <= 31, it
 * masks with, back to back. */
static int tp_hist_masks_31(int a)
{
    return a + 1 < tp_hist_n &&
           tp_hist[a].kind == TP_HIST_INSTR && tp_hist[a].opc == TP_OP_OPR &&
           tp_hist[a].val == TP_OPR_AND &&
           tp_hist[a + 1].kind == TP_HIST_INSTR &&
           tp_hist[a + 1].opc == TP_OP_LDC &&
           tp_hist[a + 1].val >= 0 && tp_hist[a + 1].val <= 31 &&
           tp_hist[a + 1].end == tp_hist[a].off;
}

/* Is the shift count now in AReg already in 0..31?  The count masked in the
 * source, as rol32/ror32 do (`shift & 31`), makes the runtime clamp a no-op.
 * AReg comes from the last instruction emitted, or from the one before a
 * push and `rev`; that is `ldc k; and`, or `ldl r` where the latest write to
 * pseudo-register r was `ldc k; and; stl r` and no `ldlp r` since could have
 * changed it.  The history only holds contiguous code with no label inside
 * it, so those instructions are what produced AReg. */
static int tp_shift_count_masked(void)
{
    int a = 0, s, r;

    if (nocode_wanted || tp_hist_n < 2 || tp_hist[0].end != ind)
        return 0;
    if (tp_hist[0].kind == TP_HIST_INSTR && tp_hist[0].opc == TP_OP_OPR &&
        tp_hist[0].val == TP_OPR_REV) {
        if (tp_hist_n < 4 || !tp_hist_is_pure_push(&tp_hist[1]) ||
            tp_hist[1].end != tp_hist[0].off ||
            tp_hist[2].end != tp_hist[1].off)
            return 0;
        a = 2;
    }
    if (tp_hist_masks_31(a))
        return 1;
    if (tp_hist[a].kind != TP_HIST_INSTR || tp_hist[a].opc != TP_OP_LDL)
        return 0;
    r = tp_hist[a].val;
    if (r < 0 || r >= NB_REGS)
        return 0;
    for (s = a + 1; s < tp_hist_n; s++) {
        if (tp_hist[s].end != tp_hist[s - 1].off)
            return 0;
        if (tp_hist[s].kind != TP_HIST_INSTR || tp_hist[s].val != r)
            continue;
        if (tp_hist[s].opc == TP_OP_LDLP)
            return 0;
        if (tp_hist[s].opc == TP_OP_STL)
            return tp_hist_masks_31(s + 1) &&
                   tp_hist[s + 1].end == tp_hist[s].off;
    }
    return 0;
}

/* What the stages of gen_opi() share: the operator, which the first stage
 * may replace by an equivalent one, and the folds decided while the
 * operands are pushed, which say what the emitters find on the stack. */
struct tp_opi {
    int op;
    int cmp_zero_true;
    int shl_add_opt;
    int mul_const_opt;
    int mul_const_val;
    int eqc_const_opt;
    int eqc_const_val;
    int adc_const_opt;
    int adc_const_val;
    /* x >> K with a literal K: the shift count can be pushed *after* xdble
     * instead of being parked in a workspace slot across it.  See TOK_SAR. */
    int sar_const_opt;
    int sar_const_val;
    int mod_pow2_opt;   /* unsigned modulo power-of-2: x % N -> x & (N-1) */
    int mod_pow2_mask;
    /* Shift count is a literal already in 0..31, so the runtime
     * "LDC 31; AND" clamp is provably a no-op and can be dropped. */
    int shift_const_ok;
};

/* Put the operator in the form that costs least: the operands may be
 * exchanged and the relation replaced by its mirror or complement. */
static void tp_opi_canonical(struct tp_opi *o)
{
    /* UMULL leaves its high word in REG_IRE2, which may still hold the high
     * half of a pending long long call result (`get_u64() + x * y`). */
    if (o->op == TOK_UMULL)
        save_reg(REG_IRE2);

    /* `a < b` and `a >= b` used to emit REV before GT, because GT computes
     * (BReg > AReg) and the operands are pushed first-then-second.  Swapping
     * the two vstack entries and flipping the operator gives the same result
     * with no REV at all:
     *
     *     a <  b   ===   b >  a
     *     a >= b   ===   b <= a
     *
     * Both operands are already evaluated by the time gen_opi runs, so
     * exchanging them cannot reorder side effects - this is the same
     * reasoning behind the existing TOK_EQ/TOK_NE commutation swap further
     * down.  Doing it here rather than at the push sites means every operand
     * path (local/local, local/const, register, and the gv2 fallback) gets it,
     * and the classification below sees the final order.
     *
     * The peephole rule that was supposed to catch this pattern reports zero
     * hits on every function, so it never fired; removing the REV at the
     * source is the fix. */
    if (o->op == TOK_LT || o->op == TOK_GE) {
        SValue tmp = vtop[-1];
        vtop[-1] = vtop[0];
        vtop[0] = tmp;
        o->op = (o->op == TOK_LT) ? TOK_GT : TOK_LE;
    }

    /* Strict compare against a literal at a loop back-edge: produce the
     * complement so the result can be tagged "zero is true" and gjmp_cond
     * needs no EQC 0. */
    o->cmp_zero_true = tp_cmp_complement_vs_literal(&o->op);

    /* The unsigned relations, likewise.  With GTU, a <u b is b >u a with no
     * REV.  Without it the mint/xor sequence is the other way round - REV
     * comes free with a <u b and costs one for a >u b - so there the swap
     * goes towards TOK_ULT/TOK_UGE.  A literal compare the complement has
     * already rewritten is left as it is. */
    if (!o->cmp_zero_true) {
        int to = 0;
        if (tp_have_t9000_feat(TP_T9K_GTU)) {
            if (o->op == TOK_ULT) to = TOK_UGT;
            else if (o->op == TOK_UGE) to = TOK_ULE;
        } else {
            if (o->op == TOK_UGT) to = TOK_ULT;
            else if (o->op == TOK_ULE) to = TOK_UGE;
        }
        if (to) {
            tp_cmp_swap_operands();
            o->op = to;
        }
    }
}

/* Transputer: comparison operations need explicit operand loading.
 *
 * For arithmetic ops (+, -, etc.), the operands are typically already
 * on the eval stack from preceding load/computation instructions.
 * These work "by construction" — TCC's generic layer generates loads
 * that happen to leave values on the Transputer eval stack.
 *
 * For comparison ops (==, !=, >, <, etc.), this doesn't work after
 * function calls because the eval stack is corrupted by the CALL
 * instruction's return value. We must explicitly load operands.
 *
 * We use tp_push_sv() which always generates code to push a value
 * onto the eval stack, handling all SValue types correctly. */
/* Always explicitly push both operands onto the hardware eval stack.
 * TCC's generic layer may leave values in workspace-backed "registers"
 * (workspace slots) rather than on the eval stack. On the Transputer,
 * all binary operations (arithmetic, logic, comparisons) operate on
 * AReg and BReg, so we must ensure both values are pushed. */
/* Transputer-specific gv2: for two word-size local lvalues, emit
 * LDL directly from local offsets and STL to allocated registers.
 * This produces LDL a; STL r1; LDL b; STL r2 (4 insns) which is
 * the same as normal gv2. BUT: tp_push_sv then does LDL r1; LDL r2
 * (2 more insns). With the DUP/areg_tag tracker, the second load
 * might be optimized. The real win comes when we later skip the
 * tp_push_sv reload by keeping values on the eval stack.
 *
 * For now, this function is identical to gv2 for the common case
 * but lays the groundwork for direct eval-stack loading. */
/* Push both operands straight from where they are.  Returns 0, with
 * nothing emitted, when an operand needs gv2() first. */
static int tp_opi_push_direct(struct tp_opi *o)
{
    int v1 = vtop[-1].r & VT_VALMASK;
    int v2 = vtop[0].r & VT_VALMASK;

    /* Classify operands */
    /* (a word of a packed local at a misaligned offset is not one LDL:
     * tp_word_unaligned) */
    int first_local = (vtop[-1].r & VT_LVAL) && v1 == VT_LOCAL &&
                      !(vtop[-1].r & VT_SYM) && !(vtop[-1].c.i & 3) &&
                      get_primitive_type_size(vtop[-1].type.t & VT_BTYPE) == 4;
    int second_local = (vtop[0].r & VT_LVAL) && v2 == VT_LOCAL &&
                       !(vtop[0].r & VT_SYM) && !(vtop[0].c.i & 3) &&
                       get_primitive_type_size(vtop[0].type.t & VT_BTYPE) == 4;
    int first_const = (v1 == VT_CONST) && !(vtop[-1].r & VT_SYM);
    int second_const = (v2 == VT_CONST) && !(vtop[0].r & VT_SYM);

    /* A value already materialised in a general workspace-slot register
     * costs exactly one LDL r to push — the same as a local. Without this,
     * (register op X) forms fell through to gv2 and emitted the
     * five-instruction spill dance `STL a; LDC k; STL b; LDL a; LDL b`
     * ahead of the real operation. That is the single most common waste in
     * kernel code: ~184k sites, ~738 KB of .text.
     *
     * Restricted to TREG_W0..NB_REGS-1: slots 0-2 are the hardware A/B/C
     * eval-stack registers and 3-5 are the FPU slots, none of which follow
     * the plain "value lives in workspace slot r" rule. VT_MUSTCAST must be
     * excluded too — bypassing gv2 would skip a required narrowing cast. */
    int first_reg = !(vtop[-1].r & (VT_LVAL | VT_SYM | VT_MUSTCAST |
                                    VT_MUSTBOUND | VT_BOUNDED)) &&
                    v1 >= TREG_W0 && v1 < NB_REGS &&
                    get_primitive_type_size(vtop[-1].type.t & VT_BTYPE) == 4;
    int second_reg = !(vtop[0].r & (VT_LVAL | VT_SYM | VT_MUSTCAST |
                                    VT_MUSTBOUND | VT_BOUNDED)) &&
                     v2 >= TREG_W0 && v2 < NB_REGS &&
                     get_primitive_type_size(vtop[0].type.t & VT_BTYPE) == 4;


    /* Exclude 3-operand ops and special cases from direct path */
    int exclude = o->op == TOK_UMULL || o->op == TOK_ADDC1 || o->op == TOK_SUBC1 ||
                  o->op == TOK_ADDC2 || o->op == TOK_SUBC2 ||
                  o->op == TOK_UDIV || o->op == TOK_UMOD ||
                  o->op == TOK_A_SHL || o->op == TOK_A_SAR || o->op == TOK_NEG;

    /* Plain shifts are NOT excluded: SHL/SHR/SAR all want AReg=count,
     * BReg=value, which is exactly the operand order CASE 1/2/3 produce.
     * Excluding them forced every shift down the gv2 spill path, which
     * cost 8 extra bytes per constant shift (the dominant kernel form:
     * `x >> LITERAL` in field extracts and bitmap indexing). */
    int is_shift = (o->op == TOK_SHL || o->op == TOK_SHR || o->op == TOK_SAR);
    int second_folded;

    /* A literal count already in range makes the runtime clamp dead. */
    if ((is_shift || o->op == TOK_A_SHL || o->op == TOK_A_SAR) && second_const) {
        unsigned long cnt = (unsigned long)vtop[0].c.i;
        if (cnt <= 31)
            o->shift_const_ok = 1;
    }

    /* Constant-RHS folds, decided once for every direct case below.
     * `direct_ready` jumps past the block that used to compute these, so
     * without this the fast paths emitted `LDC k; SUM` where the slow path
     * would have emitted a single `ADC k`. That matters enormously: every
     * struct field access is lowered by tccgen as `pointer + constant`
     * (see the TOK_ARROW case in tccgen.c), so ADC folding applies to
     * essentially all field addressing in the kernel.
     *
     * When a fold fires, the second operand must NOT be pushed — the
     * folded instruction carries the literal itself. */
    if (!exclude && second_const) {
        long c = (long)vtop[0].c.i;
        switch (o->op) {
        case '+': case TOK_A_ADD:
            if (!tp_adc_would_trap()) {
                o->adc_const_opt = 1; o->adc_const_val = (int)c;
            }
            break;
        case '-': case TOK_A_SUB:
            if (!tp_adc_would_trap()) {
                o->adc_const_opt = 1; o->adc_const_val = -(int)c;
            }
            break;
        case TOK_EQ: case TOK_NE:
            o->eqc_const_opt = 1; o->eqc_const_val = (int)c;
            break;
        case '*': case TOK_A_MUL:
            if (tp_mul_const_strength(c, &o->mul_const_val))
                o->mul_const_opt = 1;
            break;
        case TOK_SHL:
            if (c == 1 || c == 2)
                o->shl_add_opt = (int)c;
            break;
        case TOK_SAR: case TOK_A_SAR:
            /* Only for counts the runtime clamp would have passed
               through unchanged.  Negative counts, and counts above 31,
               keep the generic path and its LDC 31; AND clamp, so
               behaviour there is exactly as before. */
            if (c >= 0 && c <= 31) {
                o->sar_const_opt = 1; o->sar_const_val = (int)c;
            }
            break;
        default:
            break;
        }
    }

    /* True when the constant RHS is folded into the operation and so must
     * not be materialised on the eval stack. */
    second_folded = o->adc_const_opt || o->eqc_const_opt ||
                    o->mul_const_opt || o->shl_add_opt || o->sar_const_opt;

    if (!exclude && first_local && second_local) {
        /* CASE 1: local op local → LDL a; LDL b */
        int off1 = vtop[-1].c.i;
        int off2 = vtop[0].c.i;
        abcreg_stack_depth = 0;
        if (off1 == off2 && !tp_t414()) {
            emit_local_op(TP_OP_LDL, off1);
            Tp_DUP();
        } else {
            emit_local_op(TP_OP_LDL, off1);
            emit_local_op(TP_OP_LDL, off2);
        }
        vtop[-1].r = get_reg(RC_INT);
        return 1;
    }

    if (!exclude && first_local && second_const) {
        /* CASE 2: local op const → LDL a; LDC const */
        abcreg_stack_depth = 0;
        emit_local_op(TP_OP_LDL, vtop[-1].c.i);
        if (!second_folded)
            tp_ldc_pooled((int)vtop[0].c.i);
        vtop[-1].r = get_reg(RC_INT);
        return 1;
    }

    if (!exclude && first_const && second_local) {
        /* CASE 3: const op local → LDC const; LDL b */
        abcreg_stack_depth = 0;
        if ((o->op == '*' || o->op == TOK_A_MUL) &&
            tp_mul_const_strength((long)vtop[-1].c.i, &o->mul_const_val)) {
            emit_local_op(TP_OP_LDL, vtop[0].c.i);
            o->mul_const_opt = 1;
        } else {
            tp_ldc_pooled((int)vtop[-1].c.i);
            emit_local_op(TP_OP_LDL, vtop[0].c.i);
        }
        vtop[-1].r = get_reg(RC_INT);
        return 1;
    }

    /* CASES 4-8: one or both operands already live in a workspace-slot
     * register. Push directly with LDL instead of round-tripping through
     * gv2's freshly allocated spill slots. The first operand's register is
     * reused as the destination — it is consumed by this operation anyway,
     * and reusing it avoids a get_reg() call that could spill a live value
     * after we have already pushed onto the eval stack. */
    if (!exclude && (first_reg || second_reg) &&
        (first_reg || first_local || first_const) &&
        (second_reg || second_local || second_const)) {
        int dest;
        abcreg_stack_depth = 0;

        /* Push first operand. The common shape is that the value was just
         * computed and stored into v1 by the previous operation, so try to
         * cancel that store instead of reloading it. */
        if (first_reg)
            tp_push_reg_or_cancel_stl(v1, &vtop[-1]);
        else if (first_local)
            emit_local_op(TP_OP_LDL, vtop[-1].c.i);
        else
            tp_ldc_pooled((int)vtop[-1].c.i);

        /* Push second operand, unless it was folded into the operation. */
        if (second_folded) {
            /* nothing to push — the literal rides in the instruction */
        } else if (second_reg) {
            if (first_reg && v1 == v2 && !tp_t414())
                Tp_DUP();
            else
                Tp_LDL(v2);
        } else if (second_local) {
            emit_local_op(TP_OP_LDL, vtop[0].c.i);
        } else {
            tp_ldc_pooled((int)vtop[0].c.i);
        }

        /* Plain assignment, as CASES 1-3 do: the result is a fresh value
         * in a register, so every other r-flag (VT_LVAL, VT_SYM,
         * VT_MUSTCAST, ...) must be cleared, not carried over. */
        dest = first_reg ? v1 : (second_reg ? v2 : get_reg(RC_INT));
        vtop[-1].r = dest;
        return 1;
    }
    return 0;
}

/* The standard path: gv2(), then push both operands. */
static void tp_opi_push_general(struct tp_opi *o)
{
    SValue sv_first, sv_second;
    int first_is_imm_const, second_is_imm_const;

    gv2(RC_INT, RC_INT);
    sv_first = vtop[-1];
    sv_second = vtop[0];
    first_is_imm_const = ((sv_first.r & VT_VALMASK) == VT_CONST) && !(sv_first.r & VT_SYM);
    second_is_imm_const = ((sv_second.r & VT_VALMASK) == VT_CONST) && !(sv_second.r & VT_SYM);

    /* Spill/reload desync guard:
     * For arithmetic/comparison, a scalar local can transiently appear as
     * VT_LOCAL without VT_LVAL. Treat it as a value load in this context.
     * Keep pointer/array/function operands untouched (address semantics). */
    if (!(sv_first.r & VT_LVAL) && (sv_first.r & VT_VALMASK) == VT_LOCAL) {
        int bt1 = sv_first.type.t & VT_BTYPE;
        if (!(sv_first.type.t & (VT_ARRAY | VT_VLA)) && bt1 != VT_PTR && bt1 != VT_FUNC) {
            tp_debug_log_vt_local_normalize(&sv_first, "lhs");
            sv_first.r |= VT_LVAL;
        }
    }
    if (!(sv_second.r & VT_LVAL) && (sv_second.r & VT_VALMASK) == VT_LOCAL) {
        int bt2 = sv_second.type.t & VT_BTYPE;
        if (!(sv_second.type.t & (VT_ARRAY | VT_VLA)) && bt2 != VT_PTR && bt2 != VT_FUNC) {
            tp_debug_log_vt_local_normalize(&sv_second, "rhs");
            sv_second.r |= VT_LVAL;
        }
    }

    if (o->op == '*' || o->op == TOK_A_MUL) {
        /* PROD latency depends on AReg; keep immediate constant in AReg. */
        if (first_is_imm_const && !second_is_imm_const) {
            SValue tmp = sv_first;
            sv_first = sv_second;
            sv_second = tmp;
            first_is_imm_const = 0;
            second_is_imm_const = 1;
        }
        /* Strength reduction for multiply by constants.
         * DUP+SUM = 2 cycles vs PROD's 30+ cycles.
         * x*0 → 0
         * x*1 → x
         * x*(-1) → -x
         * x*2 → DUP;SUM
         * x*3 → DUP;SUM;(orig);SUM (save x, x*2, add x)
         * x*4 → DUP;SUM;DUP;SUM
         * x*8 → DUP;SUM;DUP;SUM;DUP;SUM
         * x*(1<<N), N>=4 → LDC N; SHL */
        if (second_is_imm_const) {
            long c = sv_second.c.i;
            if (tp_mul_const_strength(c, &o->mul_const_val)) {
                o->mul_const_opt = 1;
            }
        }
    }

    /* Modulo power-of-2: x % N -> x & (N-1).
     * Only valid for unsigned modulo. Signed C remainder must keep the
     * dividend sign, e.g. -9 % 8 == -1, while -9 & 7 == 7. */
    if (o->op == TOK_UMOD && second_is_imm_const) {
        long c = sv_second.c.i;
        if (c > 0 && (c & (c - 1)) == 0) {  /* power of 2 check */
            o->mod_pow2_opt = 1;
            o->mod_pow2_mask = (int)(c - 1);
        }
    }

    if ((o->op == TOK_A_SHL || o->op == TOK_SHL) && second_is_imm_const) {
        if (sv_second.c.i == 1) {
            o->shl_add_opt = 1;
        } else if (sv_second.c.i == 2) {
            o->shl_add_opt = 2;
        }
    }

    if (o->op == TOK_EQ || o->op == TOK_NE) {
        /* Commutative: if left is constant, swap so right is constant */
        if (first_is_imm_const && !second_is_imm_const) {
            SValue tmp = sv_first;
            sv_first = sv_second;
            sv_second = tmp;
            first_is_imm_const = 0;
            second_is_imm_const = 1;
        }
        if (second_is_imm_const) {
            o->eqc_const_opt = 1;
            o->eqc_const_val = sv_second.c.i;
        }
    }

    if (o->op == '+' || o->op == TOK_A_ADD || o->op == '-' || o->op == TOK_A_SUB) {
        /* ADC optimization: a + const → load a; ADC const (1 instruction)
         * Also works for subtraction: a - const → ADC(-const).
         * Commutative for +: if left is constant, swap. */
        if ((o->op == '+' || o->op == TOK_A_ADD) && first_is_imm_const && !second_is_imm_const) {
            SValue tmp = sv_first;
            sv_first = sv_second;
            sv_second = tmp;
            first_is_imm_const = 0;
            second_is_imm_const = 1;
        }
        if (second_is_imm_const && !tp_adc_would_trap()) {
            int cval = (o->op == '-' || o->op == TOK_A_SUB) ? -(int)sv_second.c.i : (int)sv_second.c.i;
            o->adc_const_opt = 1;
            o->adc_const_val = cval;
        }
    }

    if (tp_genopi_debug) {
        fprintf(stderr, "[GEN_OPI] func=%s op=0x%x('%c') ind=0x%x "
                "first: r=0x%x v=0x%x c.i=%ld lval=%d  "
                "second: r=0x%x v=0x%x c.i=%ld lval=%d\n",
                funcname ? funcname : "?", o->op, (o->op > 0x20 && o->op < 0x7f) ? o->op : '?',
                ind,
                sv_first.r, sv_first.r & VT_VALMASK, (long)sv_first.c.i,
                (sv_first.r & VT_LVAL) != 0,
                sv_second.r, sv_second.r & VT_VALMASK, (long)sv_second.c.i,
                (sv_second.r & VT_LVAL) != 0);
    }
    abcreg_stack_depth = 0;
    /* Prime CReg for 3-operand instructions (LMUL, LSUM, LDIFF, LDIV).
     * Push the CReg value before operands so it naturally falls to CReg
     * after two operand pushes. Eliminates STL/REV/LDL workspace thrash. */
    if (o->op == TOK_UMULL || o->op == TOK_ADDC1 || o->op == TOK_SUBC1
        || o->op == TOK_UDIV || o->op == TOK_UMOD) {
        Tp_LDC(0);
        abcreg_push(1);
    } else if (o->op == TOK_ADDC2 || o->op == TOK_SUBC2) {
        Tp_LDL(FRAME_SLOT_CARRY);
        abcreg_push(1);
    }
    /* Direct LDL chaining: DISABLED — gv2() already emits load
     * instructions and updating the vstack. Adding extra LDLs on top
     * corrupts the eval stack state. Proper fix requires either:
     * a) Preventing gv2() from emitting loads for the direct path
     * b) Replacing gv2() entirely with a transputer-specific loader
     * Both are major TCC refactors. See STL-LDL-PLAN.md. */
    tp_push_sv(&sv_first);
    if (!o->shl_add_opt && !o->mul_const_opt && !o->eqc_const_opt && !o->adc_const_opt
        && !o->mod_pow2_opt && !o->sar_const_opt) {
        int same_operand = 0;
        if (sv_first.r == sv_second.r && sv_first.c.i == sv_second.c.i)
            same_operand = 1;
        if (same_operand && !tp_t414()) {
            Tp_DUP();
        } else {
            tp_push_sv(&sv_second);
        }
    }
}

/* The emitters.  Each returns 0 for an operator that is not its own.
 * On entry AReg is the second operand and BReg the first, less whatever
 * the folds left out. */
static int tp_opi_arith(struct tp_opi *o)
{
    switch (o->op) {
    case TOK_A_ADD:  /* += compound assignment */
    case '+':
        if (o->adc_const_opt) {
            int w = tp_addr_disp_words(o->adc_const_val);
            if (w)
                emit_instr(TP_OP_LDNLP, w);   /* stage 2, not stage 4 */
            else
                emit_instr(TP_OP_ADC, o->adc_const_val);
        } else {
            Tp_SUM();
        }
        vtop--; break;
    case TOK_A_SUB:  /* -= compound assignment */
    case '-':
        if (o->adc_const_opt) {
            int w = tp_addr_disp_words(o->adc_const_val);
            if (w)
                emit_instr(TP_OP_LDNLP, w);   /* already negated; stage 2 */
            else
                emit_instr(TP_OP_ADC, o->adc_const_val);
        } else {
            Tp_DIFF();
        }
        vtop--; break;
    case TOK_A_MUL:  /* *= compound assignment */
    case '*':
        if (o->mul_const_opt && o->mul_const_val == 0) {
            if (tp_no_t805_ops()) {
                Tp_LDC(0);              /* no pop: x & 0 */
                Tp_AND();
            } else {
                Tp_POP();
                Tp_LDC(0);
            }
            vtop--; break;
        }
        if (o->mul_const_opt && tp_t414() && (o->mul_const_val == 2
            || o->mul_const_val == 4 || o->mul_const_val == 8)) {
            /* the dup; sum chains below need dup */
            Tp_LDC(o->mul_const_val == 2 ? 1 : o->mul_const_val == 4 ? 2 : 3);
            Tp_SHL();
            vtop--; break;
        }
        if (o->mul_const_opt && tp_t414() && o->mul_const_val == 3) {
            Tp_LDC(3);
            Tp_OPR_BINARY(TP_OPR_PROD);
            vtop--; break;
        }
        if (o->mul_const_opt && o->mul_const_val == 1) {
            vtop--; break;
        }
        if (o->mul_const_opt && o->mul_const_val == -1) {
            /* ~x + 1, but not with adc, the error-checking add: negating
             * MinInt overflows.  That is undefined for an int, but this is
             * also how a 64-bit multiply's cross terms take a low word of
             * all ones (`x * -1LL`, `(u64)(i32)-1 * y`), where the words
             * are signed or unsigned ints with the 64-bit type's sign and
             * any value is defined (c-testsuite 09117). */
            Tp_OPR(TP_OPR_NOT);
            Tp_LDC(1);
            Tp_SUM();
            vtop--; break;
        }
        if (o->mul_const_opt && o->mul_const_val == 2) {
            /* x * 2 → DUP; SUM */
            Tp_DUP(); Tp_SUM();
            vtop--; break;
        }
        if (o->mul_const_opt && o->mul_const_val == 3) {
            /* x * 3 → DUP; SUM; x; SUM (need original x)
             * But x is on eval stack — we can't easily get it back.
             * Alternative: DUP; DUP; SUM; SUM = x + x + x = 3x */
            Tp_DUP(); Tp_DUP();  /* AReg=x, BReg=x, CReg=x */
            Tp_SUM();            /* AReg=2x, BReg=x */
            Tp_SUM();            /* AReg=3x */
            vtop--; break;
        }
        if (o->mul_const_opt && o->mul_const_val == 4) {
            /* x * 4 → DUP; SUM; DUP; SUM */
            Tp_DUP(); Tp_SUM(); Tp_DUP(); Tp_SUM();
            vtop--; break;
        }
        if (o->mul_const_opt && o->mul_const_val == 8) {
            /* x * 8 → DUP; SUM; DUP; SUM; DUP; SUM */
            Tp_DUP(); Tp_SUM(); Tp_DUP(); Tp_SUM(); Tp_DUP(); Tp_SUM();
            vtop--; break;
        }
        if (o->mul_const_opt && (o->mul_const_val & 0x100) != 0) {
            /* x * (1 << N) → x << N.  Entry has AReg=x; push N as
             * shift count so SHL sees AReg=count, BReg=value. */
            Tp_LDC(o->mul_const_val & 0xff);
            Tp_SHL();
            vtop--; break;
        }
        Tp_OPR(TP_OPR_PROD); abcreg_pop(1); vtop--; break;
    case TOK_A_DIV:  /* /= compound assignment */
    case '/':
    case TOK_PDIV:   /* pointer difference / element size: signed */
        Tp_OPR(TP_OPR_DIV); abcreg_pop(1); vtop--; break;
    case TOK_UDIV:   /* unsigned division */
        /* T4 DIV is signed-only. Use LDIV's CReg==0 fast path for unsigned
         * 32-bit division. CReg already primed with 0 in preamble.
         * Entry: AReg=divisor, BReg=dividend, CReg=0. */
        Tp_OPR(TP_OPR_LDIV);           /* AReg=quotient, BReg=remainder */
        abcreg_pop(1); vtop--; break;
    case TOK_A_MOD:  /* %= compound assignment */
    case '%':
        if (o->mod_pow2_opt) {
            Tp_LDC(o->mod_pow2_mask);
            Tp_AND();
            vtop--; break;
        }
        Tp_OPR(TP_OPR_REM); abcreg_pop(1); vtop--; break;
    case TOK_UMOD:   /* unsigned modulo */
        if (o->mod_pow2_opt) {
            Tp_LDC(o->mod_pow2_mask);
            Tp_AND();
            vtop--; break;
        }
        /* CReg already primed with 0 in preamble.
         * Entry: AReg=divisor, BReg=dividend, CReg=0. */
        Tp_OPR(TP_OPR_LDIV);           /* AReg=quotient, BReg=remainder */
        Tp_REV();                      /* AReg=remainder */
        abcreg_pop(1); vtop--; break;
    case TOK_A_AND:  /* &= compound assignment */
    case '&':
        if (tp_shift_mask_fold() == 2) {
            vtop--;
            break;
        }
        Tp_AND(); vtop--; break;
    case TOK_A_OR:   /* |= compound assignment */
    case '|': Tp_OR(); vtop--; break;
    case TOK_A_XOR:  /* ^= compound assignment */
    case '^': Tp_XOR(); vtop--; break;
    case TOK_NEG: Tp_OPR(TP_OPR_NOT); emit_instr(TP_OP_ADC, 1); break;  /* ~x + 1 = -x (two's complement) */
    case '~': Tp_OPR(TP_OPR_NOT); break;  /* Unary - no vtop-- */
    default:
        return 0;
    }
    return 1;
}

static int tp_opi_shift(struct tp_opi *o)
{
    switch (o->op) {
    case TOK_A_SHL:  /* <<= compound assignment */
    case TOK_SHL:
        if (o->shl_add_opt && tp_t414()) {
            Tp_LDC(o->shl_add_opt);        /* no dup for x + x */
            Tp_SHL();
            vtop--;
            break;
        }
        if (o->shl_add_opt == 1) {
            /* x << 1 == x + x */
            Tp_DUP();
            Tp_SUM();
            vtop--;
            break;
        } else if (o->shl_add_opt == 2) {
            /* x << 2 == ((x + x) + (x + x)) */
            Tp_DUP();
            Tp_SUM();
            Tp_DUP();
            Tp_SUM();
            vtop--;
            break;
        }
        /* Clamp shift count to 0-31: TCWG warns count >= 32 can lock
         * the processor for minutes.  AReg=count, BReg=value.
         * A literal count already in range makes the clamp dead code. */
        if (!o->shift_const_ok && !tp_shift_count_masked()) {
            Tp_LDC(31); Tp_AND();
        }
        Tp_SHL(); vtop--; break;
    case TOK_SHR:    /* >> logical shift right */
        /* Clamp shift count to 0-31 (same safety as SHL) */
        if (!o->shift_const_ok && !tp_shift_count_masked()) {
            Tp_LDC(31); Tp_AND();
        }
        Tp_SHR(); vtop--; break;
    case TOK_A_SAR:  /* >>= compound assignment (arithmetic shift) */
    case TOK_SAR:    /* arithmetic shift right using XDBLE+LSHR (Compiler Guide p28) */
        if (o->sar_const_opt) {
            /* Literal count: the count was NOT pushed (second_folded), so the
             * value alone is in AReg and the workspace round trip that the
             * general case needs disappears.
             *
             * ISM 21 FD (xdble):  Areg unchanged, Breg' = sign word, Creg' =
             * Breg'.  ISM 23 F5 (lshr): shifts the double word Creg:Breg (most
             * significant word in Creg) right by the count *in Areg*.
             *
             *   value in AReg
             *   xdble   -> AReg=value, BReg=sign, CReg=sign
             *   ldc K   -> AReg=K,     BReg=value, CReg=sign   (ldc pushes)
             *   lshr    -> AReg = (sign:value) >> K            (arithmetic)
             *
             * which is register-for-register identical to what
             * `ldc K; stl s; xdble; ldl s; lshr` produced, minus the stl/ldl.
             * K == 0 is the identity, so emit nothing at all. */
            if (o->sar_const_val != 0) {
                Tp_OPR(TP_OPR_XDBLE);
                Tp_LDC(o->sar_const_val);   /* abcreg_push(1) */
                Tp_OPR(TP_OPR_LSHR);
                /* Net the LDC back out so the eval-stack depth on exit is 1,
                 * exactly what the general path's trailing abcreg_pop(1)
                 * leaves.  Tp_OPR does not touch the AReg tag cache and,
                 * unlike the general path, there is no Tp_STL/Tp_LDL here to
                 * do it, so invalidate explicitly. */
                abcreg_pop(1);
                tp_areg_invalidate();
            }
            vtop--; break;
        }
        /* Entry: AReg=shift, BReg=value.
         * XDBLE sign-extends AReg into BReg:AReg (64-bit).
         * LSHR shifts CReg:BReg right by AReg bits, result in AReg. */
        if (!o->shift_const_ok && !tp_shift_count_masked()) {
            Tp_LDC(31); Tp_AND();                /* clamp shift 0-31 */
        }
        Tp_STL(FRAME_SLOT_SCRATCH2);   /* save shift; AReg=value */
        Tp_OPR(TP_OPR_XDBLE);         /* BReg=sign(-1/0), AReg=value */
        Tp_LDL(FRAME_SLOT_SCRATCH2);  /* AReg=shift, BReg=value, CReg=sign */
        Tp_OPR(TP_OPR_LSHR);          /* CReg:BReg >> AReg → AReg=result */
        abcreg_pop(1); vtop--; break;
    default:
        return 0;
    }
    return 1;
}

static int tp_opi_compare(struct tp_opi *o)
{
    switch (o->op) {
    /* Comparisons: Result 1(True) or 0(False) in AReg.
     *
     * All comparisons produce "non-zero = true" in AReg.  gvtst() calls
     * gjmp_cond(op ^ inv, t).  gjmp_cond adds EQC 0 when (op & 1) == 1.
     * CJ jumps when AReg == 0.
     *
     * With TOK_NE (0x95, odd):
     *   inv=0 → gjmp_cond(odd) → EQC 0 + CJ → jumps when was non-zero (true)
     *   inv=1 → gjmp_cond(even) → CJ → jumps when zero (false)
     *
     * So vset_VT_CMP(TOK_NE) is correct for "non-zero = true" results. */
    case TOK_EQ:
        if (o->eqc_const_opt && o->eqc_const_val == 0 && !tp_no_eqc0()) {
            /* `x == 0` is exactly what VT_CMP(TOK_EQ) already means - "zero is
             * true" - so the value can be left raw and no instruction emitted
             * at all.  This is the mirror of the `x != 0` case below, which
             * was already deferred; the equality side was not, and it is the
             * one that pays more.
             *
             * Emitting `EQC 0` and tagging TOK_NE ("nonzero is true") costs an
             * inversion that every consumer then has to undo:
             *
             *   branch when true   EQC 0 ; EQC 0 ; CJ   ->  CJ
             *   branch when false  EQC 0 ; CJ           ->  EQC 0 ; CJ
             *   used as a value    EQC 0 ; EQC 0 ; EQC 0 ->  EQC 0
             *
             * so deferring is better or equal in every context, never worse -
             * no consumer hint is needed.  `return !x` was three EQC 0 and is
             * now one; the second operand of `a == 0 && b == 0` was two and is
             * now none.  Each EQC is a stage-4 ALU op that cannot share the
             * group of the compare feeding it, so a removed pair also lets the
             * CJ group with its operand. */
            vtop--;
            vset_VT_CMP(TOK_EQ);
        } else if (o->eqc_const_opt) {
            /* EQC K: single instruction, AReg = (AReg == K) ? 1 : 0.
             * Replaces LDC K + DIFF + EQC 0 (3 instrs) with 1 instr. */
            emit_instr(TP_OP_EQC, o->eqc_const_val);
            /* EQC produces 1 if equal → non-zero = true */
            vtop--;
            vset_VT_CMP(TOK_NE);
        } else {
            /* DIFF produces 0 when equal, nonzero otherwise.
             * vset_VT_CMP(TOK_EQ) defers normalization: gjmp_cond and
             * load() know "zero = true" and add EQC 0 only when needed. */
            Tp_DIFF();
            vtop--;
            vset_VT_CMP(TOK_EQ);
        }
        break;
    case TOK_NE:
        if (o->eqc_const_opt) {
            if (o->eqc_const_val == 0) {
                /* `x != 0` is exactly what VT_CMP(TOK_NE) already means -
                 * "nonzero is true" - so the value can be left raw.  The
                 * previous `EQC 0; EQC 0` normalised it to 1/0 and then
                 * inverted twice, costing 2 instructions and 2 stage-4 slots
                 * for nothing.  Both consumers cope: gjmp_cond keys only on
                 * op & 1, and load()'s VT_CMP handler re-normalises when a
                 * 0/1 value is actually needed.  Fires on every `if (p)`,
                 * `while (n)`, `if (x != 0)`. */
            } else {
                /* EQC K gives 1 if equal: "zero is true" for x != K.  As
                 * for `x == 0` above, leaving it raw and tagging TOK_EQ is
                 * better or equal for every consumer than inverting now:
                 *   branch when false  EQC K ; EQC 0 ; CJ  (unchanged)
                 *   branch when true   EQC K ; EQC 0 ; EQC 0 ; CJ  ->  EQC K ; CJ
                 *   used as a value    EQC K ; EQC 0 ; EQC 0 ; EQC 0  ->  EQC K ; EQC 0 */
                emit_instr(TP_OP_EQC, o->eqc_const_val);
                vtop--;
                vset_VT_CMP(TOK_EQ);
                break;
            }
            vtop--;
            vset_VT_CMP(TOK_NE);
        } else {
            /* DIFF produces 0 when equal, nonzero when not equal.
             * vset_VT_CMP(TOK_NE) defers normalization to 0/1 until
             * materialization in load()'s VT_CMP handler. For branching,
             * gvtst/gjmp_cond only tests zero vs nonzero. */
            Tp_DIFF();
            vtop--;
            vset_VT_CMP(TOK_NE);
        }
        break;
    case TOK_GT:
        Tp_GT();                   /* 1 if a > b, 0 otherwise */
        vtop--;
        vset_VT_CMP(o->cmp_zero_true ? TOK_EQ : TOK_NE);
        break;
    case TOK_LT:
        Tp_REV();
        Tp_GT();                   /* 1 if a < b (swapped), 0 otherwise */
        vtop--;
        vset_VT_CMP(TOK_NE);
        break;
    case TOK_GE:
        /* a >= b  ===  !(a < b)  ===  !(b > a)
         * REV so GT tests b > a (= a < b).  The result is then inverted for
         * free by deferring with VT_CMP(TOK_EQ), i.e. "zero is true" - the
         * same mechanism TOK_EQ's DIFF path above uses.  The explicit
         * `EQC 0` that used to do the inversion is pure waste. */
        Tp_REV();
        Tp_GT();                   /* 1 when a < b, so 0 means a >= b */
        vtop--;
        vset_VT_CMP(TOK_EQ);
        break;
    case TOK_LE:
        /* a <= b  ===  !(a > b).  Same deferred inversion as TOK_GE. */
        Tp_GT();                   /* 1 when a > b, so 0 means a <= b */
        vtop--;
        vset_VT_CMP(TOK_EQ);
        break;

    /* Unsigned comparisons - XOR both operands with MinInt (0x80000000)
     * to convert unsigned ordering to signed ordering, then use GT.
     *
     * Entry state: AReg = right (vtop[0]), BReg = left (vtop[-1])
     * GT semantics: AReg = (BReg > AReg) ? 1 : 0  (signed comparison)
     *
     * MINT (OPR 0x42) loads 0x80000000 in a single byte — much more
     * efficient than LDC 0x80000000 which needs 8 prefix bytes.
     *
     * Core XOR-both-operands sequence (from entry AReg=R, BReg=L):
     *   MINT    → AReg=MINT, BReg=R, CReg=L
     *   XOR     → AReg=R^MINT, BReg=L        (XOR pops CReg→BReg)
     *   REV     → AReg=L, BReg=R^MINT
     *   MINT    → AReg=MINT, BReg=L, CReg=R^MINT
     *   XOR     → AReg=L^MINT, BReg=R^MINT   (XOR pops CReg→BReg)
     *   (no REV)
     *   GT      → (R^MINT > L^MINT) = unsigned(R > L) = unsigned(L < R)
     *
     * With final REV before GT:
     *   REV     → AReg=R^MINT, BReg=L^MINT
     *   GT      → (L^MINT > R^MINT) = unsigned(L > R)
     */
    case TOK_ULT:
        /* unsigned left < right: REV + GTU → (right >u left) */
        tp_emit_gtu(1);
        vtop--;
        vset_VT_CMP(TOK_NE);
        break;

    case TOK_UGE:
        /* unsigned left >= right = !(left < right) = !(right >u left).
         * REV + GTU yields 1 when left < right; defer the invert via
         * VT_CMP(TOK_EQ) ("zero is true"), matching signed TOK_GE.
         * Eager EQC 0 here + load()'s odd-op double-EQC 0 used to emit
         * three EQC 0 bytes for `return a + b <= c` (seq_buf_can_fit). */
        tp_emit_gtu(1);
        vtop--;
        vset_VT_CMP(TOK_EQ);
        break;

    case TOK_UGT:
        /* unsigned left > right: GTU → (BReg >u AReg) = (left >u right) */
        tp_emit_gtu(0);
        vtop--;
        vset_VT_CMP(o->cmp_zero_true ? TOK_EQ : TOK_NE);
        break;

    case TOK_ULE:
        /* unsigned left <= right = !(left >u right). Same deferred invert
         * as TOK_LE / TOK_UGE: GTU is 1 when left > right, so 0 means <=.
         * ISM App B: gtu = secondary 0x5F (25 FF). Never emit non-IMS OPR. */
        tp_emit_gtu(0);
        vtop--;
        vset_VT_CMP(TOK_EQ);
        break;
    default:
        return 0;
    }
    return 1;
}

static int tp_opi_multiword(struct tp_opi *o)
{
    switch (o->op) {
    /* Multi-precision arithmetic (64-bit operations decomposed by gen_opl)
     *
     * Entry state after gv2 + tp_push_sv: AReg=R (vtop[0]), BReg=L (vtop[-1])
     *
     * LMUL semantics: result = BReg * AReg + CReg → AReg=low32, BReg=high32
     * LSUM semantics: AReg = BReg + AReg + (CReg & 1), BReg = carry_out
     * LDIFF semantics: AReg = BReg - AReg - (CReg & 1), BReg = borrow_out
     *
     * Key insight: CReg must be explicitly set up because gv2/load between
     * ADDC1 and ADDC2 clobbers the entire eval stack. We save carry/borrow
     * to FRAME_SLOT_CARRY and restore it in the C2 operation.
     */
    case TOK_UMULL:
        /* Unsigned 32x32→64 multiply. CReg already primed with 0 in preamble.
         * Entry: AReg=R, BReg=L, CReg=0. LMUL: result = BReg*AReg+CReg. */
        Tp_OPR(TP_OPR_LMUL);         /* AReg=low(L*R), BReg=high(L*R) */
        Tp_REV();                     /* AReg=high, BReg=low */
        Tp_STL(REG_IRE2);            /* save high word; AReg←low */
        abcreg_pop(1);
        vtop--;
        vtop->r2 = REG_IRE2;         /* tell TCC where high word is */
        break;

    case TOK_ADDC1:
        /* Add low halves, generate carry. CReg already primed with 0.
         * Entry: AReg=R_lo, BReg=L_lo, CReg=0. */
        Tp_OPR(TP_OPR_LSUM);         /* AReg=sum_lo, BReg=carry_out */
        Tp_REV();                     /* AReg=carry, BReg=sum_lo */
        Tp_STL(FRAME_SLOT_CARRY);    /* save carry; AReg←sum_lo */
        abcreg_pop(1);
        vtop--;
        break;

    case TOK_ADDC2:
        /* Add high halves with saved carry. CReg primed with carry in preamble.
         * Entry: AReg=R_hi, BReg=L_hi, CReg=carry. */
        Tp_OPR(TP_OPR_LSUM);         /* AReg=sum_hi, BReg=carry_out (discarded) */
        abcreg_pop(1);
        vtop--;
        break;

    case TOK_SUBC1:
        /* Subtract low halves, generate borrow. CReg already primed with 0.
         * Entry: AReg=R_lo, BReg=L_lo, CReg=0. */
        Tp_OPR(TP_OPR_LDIFF);        /* AReg=diff_lo, BReg=borrow_out */
        Tp_REV();                     /* AReg=borrow, BReg=diff_lo */
        Tp_STL(FRAME_SLOT_CARRY);    /* save borrow; AReg←diff_lo */
        abcreg_pop(1);
        vtop--;
        break;

    case TOK_SUBC2:
        /* Subtract high halves with saved borrow. CReg primed with borrow in preamble.
         * Entry: AReg=R_hi, BReg=L_hi, CReg=borrow. */
        Tp_OPR(TP_OPR_LDIFF);        /* AReg=diff_hi, BReg=borrow_out (discarded) */
        abcreg_pop(1);
        vtop--;
        break;
    default:
        return 0;
    }
    return 1;
}

ST_FUNC void gen_opi(int op) {
    struct tp_opi o;

    memset(&o, 0, sizeof o);
    o.op = op;
    tp_opi_canonical(&o);
    if (!tp_opi_push_direct(&o))
        tp_opi_push_general(&o);
    if (!tp_opi_arith(&o) && !tp_opi_shift(&o) && !tp_opi_compare(&o) &&
        !tp_opi_multiword(&o))
        tcc_error("unsupported integer op 0x%x", o.op);

    /* After gen_opi, the result is in AReg. For non-comparison operations,
     * store it back to the target workspace slot so later LDL reads the
     * correct value. Comparisons use vset_VT_CMP which sets vtop->r to
     * VT_CMP (>= VT_CONST), so the check below naturally skips them. */
    {
        int result_v = vtop->r & VT_VALMASK;
        if (tp_genopi_debug) {
            fprintf(stderr, "[GEN_OPI_RESULT] func=%s vtop->r=0x%x result_v=0x%x %s ind=0x%x\n",
                    funcname ? funcname : "?", vtop->r, result_v,
                    result_v < VT_CONST ? "→ STL" : "→ skip(VT_CMP)", ind);
        }
        if (result_v < VT_CONST) {
            Tp_STL(result_v);
        }
    }
}

/* 64-bit shift using native LSHL/LSHR instructions.
 *
 * Called from tccgen.c gen_opl() for variable-count 64-bit shifts.
 * Entry vstack: [..., 64-bit_value (VT_LLONG), shift_count (VT_INT)]
 *
 * Transputer LSHL/LSHR semantics:
 *   Input:  AReg = shift count, BReg = lo word, CReg = hi word
 *   Output: AReg = result lo word, BReg = result hi word
 *
 * For SAR (arithmetic right shift), LSHR is logical so we call the
 * __ashrdi3 runtime helper (SAR on signed long long is uncommon in
 * kernel code; most 64-bit values are unsigned).
 */
ST_FUNC void gen_opl_shift(int op)
{
    int t, shift_slot, lo_slot, hi_slot;

    t = vtop[-1].type.t;  /* type of the 64-bit value */

    if (op == TOK_SAR) {
        /* Arithmetic right shift: LSHR is logical only.
         * Fall back to __ashrdi3 runtime helper. */
        int func = TOK___ashrdi3;
        vpush_helper_func(func);
        vrott(3);
        gfunc_call(2);
        vpushi(0);
        vtop->r = REG_IRET;
        vtop->r2 = REG_IRE2;
        return;
    }

    /* Step 1: Swap so 64-bit value is on top, shift below */
    vswap();
    /* vstack: [..., shift_count, 64-bit_value] */

    /* Step 2: Expand 64-bit value to lo, hi */
    lexpand();
    /* vstack: [..., shift_count, lo (VT_INT), hi (VT_INT)] */

    /* Step 3: Rotate shift to top: shift lo hi → lo hi shift */
    vrotb(3);
    /* vstack: [..., lo, hi, shift_count] */

    /* Materialize shift count into a workspace slot, save to scratch */
    gv(RC_INT);
    shift_slot = vtop->r & VT_VALMASK;

    /* Clamp shift count to 0-63: TCWG warns that shift > 2*wordlength
     * can lock the processor for a significant time. */
    abcreg_stack_depth = 0;
    Tp_LDL(shift_slot);
    Tp_LDC(63);
    Tp_AND();
    Tp_STL(FRAME_SLOT_SCRATCH2);
    shift_slot = FRAME_SLOT_SCRATCH2;
    vpop();

    /* vstack: [..., lo, hi] */

    /* Materialize both halves into workspace slots */
    gv2(RC_INT, RC_INT);
    hi_slot = vtop[0].r & VT_VALMASK;
    lo_slot = vtop[-1].r & VT_VALMASK;

    /* Reset eval stack tracking — we'll manually manage it */
    abcreg_stack_depth = 0;

    /* SHL or SHR: use native LSHL / LSHR
     *
     * Push order onto eval stack (last push = AReg):
     *   1. LDL hi_slot   → goes to CReg after subsequent pushes
     *   2. LDL lo_slot   → goes to BReg after subsequent push
     *   3. LDL shift_slot → stays in AReg
     *
     * LSHL/LSHR: CReg=hi, BReg=lo, AReg=shift
     * Result: AReg=result_lo, BReg=result_hi
     */
    Tp_LDL(hi_slot);
    Tp_LDL(lo_slot);
    Tp_LDL(shift_slot);

    if (op == TOK_SHL)
        Tp_OPR_BINARY(TP_OPR_LSHL);  /* 3 in → 2 out, net -1 like binary */
    else
        Tp_OPR_BINARY(TP_OPR_LSHR);

    /* After LSHL/LSHR: AReg=result_lo, BReg=result_hi
     * LSHL/LSHR consume all 3 eval stack entries, produce 2.
     * Store results back to workspace slots. */
    Tp_STL(lo_slot);   /* STL stores AReg=result_lo; AReg←BReg=result_hi */
    Tp_STL(hi_slot);   /* STL stores AReg=result_hi (was BReg) */

    /* The vstack still has [..., lo, hi] from lexpand + gv2.
     * lo_slot and hi_slot now contain the shifted values.
     * Rebuild the 64-bit result (inline lbuild). */
    gv2(RC_INT, RC_INT);
    vtop[-1].r2 = vtop[0].r;
    vtop[-1].type.t = t;
    vpop();
}
