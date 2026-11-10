/*
 *  Transputer code generator: function calls and returns.
 *
 *  Code, not declarations: transputer-gen.c includes this once.  TinyCC's
 *  front end enters through gfunc_call() and gfunc_return().  A call's
 *  arguments are expanded into words, staged (three in the registers, the
 *  rest in the workspace words the callee reads) and the result words
 *  captured.
 */

static void tp_call_words_reserve(struct tp_call_words *cw, int extra_words)
{
    int need, new_cap;

    need = cw->count + extra_words;
    if (need <= cw->cap)
        return;

    new_cap = cw->cap ? cw->cap : 16;
    while (new_cap < need) {
        if (new_cap > (1 << 20))
            tcc_error("too many call argument words");
        new_cap <<= 1;
    }
    cw->words = tcc_realloc(cw->words, new_cap * sizeof(*cw->words));
    cw->cap = new_cap;
}

/* A volatile member of an aggregate is an observable source access too. */
static int tp_call_type_volatile(CType *type)
{
    Sym *field;
    if (type->t & (VT_VOLATILE | VT_VLA))
        return 1;
    if (type->t & VT_ARRAY)
        return tp_call_type_volatile(pointed_type(type));
    if ((type->t & VT_BTYPE) == VT_STRUCT)
        for (field = type->ref->next; field; field = field->next)
            if (tp_call_type_volatile(&field->type))
                return 1;
    return 0;
}

static void tp_expand_struct_call_words(struct tp_call_words *cw, SValue *arg, int arg_index, int callarg_debug)
{
    int align, size, words, w;

    size = type_size(&arg->type, &align);
    words = (size + 3) >> 2;

    /*
     * GCC's ABI gives an empty struct no argument words.  Linux uses this
     * deliberately for feature-dependent token parameters (for example,
     * kmalloc_token_t when partitioned kmalloc caches are disabled).  The
     * callee layout already consumes zero words for such a parameter, so
     * manufacturing one here shifts every following argument and can turn
     * kzalloc(size, flags) into kzalloc(token, flags).
     */
    if (size == 0) {
        if (callarg_debug) {
            fprintf(stderr,
                    "[CALLARG_EMPTY_STRUCT] func=%s arg_index=%d skipped\n",
                    funcname ? funcname : "<unknown>", arg_index);
        }
        return;
    }

    if (arg->r & VT_LVAL) {
        /* Reserve complete ABI words, including the padded final word.
         * Rounding only (loc - size) can leave its final word overlapping
         * a narrow local when loc is not word-aligned. */
        int spill_base = loc - words * 4;
        SValue src_addr = *arg;
        SValue dst_addr;

        spill_base &= ~3;
        loc = spill_base;
        src_addr.r &= ~VT_LVAL;

        dst_addr.type = int_type;
        dst_addr.r = VT_LOCAL;
        dst_addr.sym = NULL;
        if (tp_call_type_volatile(&arg->type)) {
            /* Preserve the original per-word source accesses for volatile
             * objects, including aggregates with volatile subobjects. */
            for (w = 0; w < words; w++) {
                int off = w * 4;
                int n = size - off < 4 ? size - off : 4;
                Tp_LDC(0);
                emit_local_op(TP_OP_STL, spill_base + off);
                abcreg_stack_depth = 0;
                dst_addr.c.i = spill_base + off;
                tp_push_sv(&src_addr);
                if (off)
                    Tp_ADC(off);
                tp_push_sv(&dst_addr);
                Tp_LDC(n);
                Tp_OPR(TP_OPR_MOVE);
                abcreg_pop(3);
                abcreg_stack_depth = 0;
            }
        } else {
            /* Only the last partial ABI word needs zero padding. */
            if (size & 3) {
                Tp_LDC(0);
                emit_local_op(TP_OP_STL, spill_base + (words - 1) * 4);
                abcreg_stack_depth = 0;
            }
            dst_addr.c.i = spill_base;
            tp_push_sv(&src_addr);
            tp_push_sv(&dst_addr);
            Tp_LDC(size);
            Tp_OPR(TP_OPR_MOVE);
            abcreg_pop(3);
            abcreg_stack_depth = 0;
        }
    } else {
        /* Keep current fallback semantics for struct rvalues during refactor. */
        words = 1;
    }

    for (w = 0; w < words; w++) {
        tp_call_words_reserve(cw, 1);
        cw->words[cw->count] = *arg;
        cw->words[cw->count].type.t =
            (cw->words[cw->count].type.t & ~(VT_BTYPE | VT_UNSIGNED | VT_VOLATILE)) | VT_INT;

        if (cw->words[cw->count].r & VT_LVAL) {
            cw->words[cw->count].r = VT_LOCAL | VT_LVAL;
            cw->words[cw->count].c.i = loc + (w * 4);
        }

        if (callarg_debug) {
            fprintf(stderr,
                    "[CALLARG_WORD] func=%s arg_index=%d word=%d abi_word=%d r=0x%x v=0x%x bt=0x%x c.i=%ld lval=%d\n",
                    funcname ? funcname : "<unknown>", arg_index, w, cw->count,
                    cw->words[cw->count].r,
                    cw->words[cw->count].r & VT_VALMASK,
                    cw->words[cw->count].type.t & VT_BTYPE,
                    (long)cw->words[cw->count].c.i,
                    (cw->words[cw->count].r & VT_LVAL) != 0);
        }
        cw->count++;
    }
}

static void tp_expand_scalar_call_words(struct tp_call_words *cw, SValue *arg, int arg_index, int callarg_debug)
{
    int bt, size;

    bt = arg->type.t & VT_BTYPE;
    size = get_primitive_type_size(bt);
    if (size == 8) {
        SValue lo = *arg;
        SValue hi = *arg;
        int v = arg->r & VT_VALMASK;

        lo.type.t = (lo.type.t & ~(VT_BTYPE | VT_UNSIGNED)) | VT_INT;
        hi.type.t = (hi.type.t & ~(VT_BTYPE | VT_UNSIGNED)) | VT_INT;

        if ((arg->r & VT_LVAL) &&
            ((v < VT_CONST && v != VT_LOCAL) || v == VT_LLOCAL)) {
            int spill_lo, spill_hi;
            /* STL addresses whole workspace words, while later VT_LOCAL
             * loads honor byte offsets.  Narrow locals can leave loc off a
             * word boundary, so align the pair before emitting either. */
            loc = (loc - 8) & -4;
            spill_lo = loc + 4;
            spill_hi = loc;
            if (v == VT_LLOCAL)
                emit_local_op(TP_OP_LDL, arg->c.i);
            else
                Tp_LDL(v);
            if (tp_t414()) {
                /* no dup: load the pointer again for the high word */
                Tp_LDNL(0);
                emit_local_op(TP_OP_STL, spill_lo);
                if (v == VT_LLOCAL)
                    emit_local_op(TP_OP_LDL, arg->c.i);
                else
                    Tp_LDL(v);
            } else {
                Tp_DUP();
                Tp_LDNL(0);
                emit_local_op(TP_OP_STL, spill_lo);
            }
            Tp_LDNL(1);
            emit_local_op(TP_OP_STL, spill_hi);
            lo.r = VT_LOCAL | VT_LVAL;
            lo.c.i = spill_lo;
            hi.r = VT_LOCAL | VT_LVAL;
            hi.c.i = spill_hi;
        } else if (arg->r & VT_LVAL) {
            hi.c.i += 4;
        } else if (v == VT_CONST && !(arg->r & VT_SYM)) {
            uint64_t bits;
            if (bt == VT_DOUBLE || bt == VT_LDOUBLE) {
                union { double d; uint64_t u; } u;
                u.d = arg->c.d;
                bits = u.u;
            } else {
                bits = arg->c.i;
            }
            lo.r = VT_CONST;
            lo.c.i = (uint32_t)bits;
            lo.sym = NULL;
            hi.r = VT_CONST;
            hi.c.i = (uint32_t)(bits >> 32);
            hi.sym = NULL;
        } else if (v == VT_CONST && (arg->r & VT_SYM)) {
            lo.r |= VT_LVAL;
            hi.r |= VT_LVAL;
            hi.c.i += 4;
        } else if (v < VT_CONST) {
            /* The high word is in r2, as in load(): after lbuild() the pair
             * can be any two registers (e.g. r=11, r2=9).  Assuming v+1 read
             * a slot nothing had written - (off + bs) >> bits passed garbage
             * as __ashrdi3's high word.  v+1 stays the fallback when r2 names
             * no register. */
            int v2 = arg->r2 & VT_VALMASK;
            hi.r = (hi.r & ~VT_VALMASK) | (v2 < VT_CONST ? v2 : v + 1);
        }

        tp_call_words_reserve(cw, 2);
        cw->words[cw->count++] = lo;
        cw->words[cw->count++] = hi;

        if (callarg_debug) {
            fprintf(stderr,
                    "[CALLARG_WORD64] func=%s arg_index=%d lo{r=0x%x v=0x%x c.i=%ld} hi{r=0x%x v=0x%x c.i=%ld}\n",
                    funcname ? funcname : "<unknown>", arg_index,
                    lo.r, lo.r & VT_VALMASK, (long)lo.c.i,
                    hi.r, hi.r & VT_VALMASK, (long)hi.c.i);
        }
    } else {
        tp_call_words_reserve(cw, 1);
        cw->words[cw->count] = *arg;
        if (callarg_debug) {
            fprintf(stderr,
                    "[CALLARG_WORD] func=%s arg_index=%d word=0 abi_word=%d r=0x%x v=0x%x bt=0x%x c.i=%ld lval=%d\n",
                    funcname ? funcname : "<unknown>", arg_index, cw->count,
                    cw->words[cw->count].r,
                    cw->words[cw->count].r & VT_VALMASK,
                    cw->words[cw->count].type.t & VT_BTYPE,
                    (long)cw->words[cw->count].c.i,
                    (cw->words[cw->count].r & VT_LVAL) != 0);
        }
        cw->count++;
    }
}

static void tp_expand_call_words(struct tp_call_words *cw, int nb_args, int callarg_debug)
{
    int i;

    for (i = 0; i < nb_args; i++) {
        SValue *arg = &vtop[-(nb_args - 1 - i)];
        int bt = arg->type.t & VT_BTYPE;

        if (bt == VT_STRUCT)
            tp_expand_struct_call_words(cw, arg, i, callarg_debug);
        else
            tp_expand_scalar_call_words(cw, arg, i, callarg_debug);
    }
}

/* Does tp_push_sv() materialise this value as a *pure push* - one that reads
 * nothing already on the evaluation stack, writes no memory and touches no
 * backend scratch slot?
 *
 * Only such values can be pushed straight into A/B/C in reverse ABI order
 * (see tp_stage_call_words below).  This is a deliberate whitelist: anything
 * not named here - narrow loads, which rebuild a halfword through
 * FRAME_SLOT_SCRATCH, and the VT_CMP / VT_JMP forms, which are not values at
 * all yet - falls back to the spill path.  Being absent is always safe.
 *
 * Word-size lvalues qualify even though they take two instructions
 * (`ldl p; ldnl 0`): the second consumes only what the first pushed, so the
 * pair is still net +1 and reads nothing underneath. */
static int tp_push_sv_is_pure(const SValue *sv)
{
    int fr = sv->r;
    int v  = fr & VT_VALMASK;
    int bt = sv->type.t & VT_BTYPE;
    int size = get_primitive_type_size(bt);

    if (bt == VT_STRUCT)
        return 0;
    if (fr & VT_LVAL) {
        /* Narrow and unaligned word loads need extra stack slots and
           temporaries. A wider value pushes its low word here. */
        if (size < 4 || tp_word_unaligned(sv))
            return 0;
        return v == VT_CONST || v == VT_LOCAL || v == VT_LLOCAL ||
               (v >= 0 && v < VT_CONST);
    }
    return v == VT_CONST || v == VT_LOCAL || v == VT_LLOCAL ||
           (v >= 0 && v < VT_CONST);
}

/* Whether materialising `sv` reads or writes a workspace slot in [lo, hi).
 * A pseudo-register r is slot r; an impure load may go through the scratch
 * slots [TP_SCRATCH_BASE, FRAME_SLOT_MIN_LOCALS) (halfword loads use
 * SCRATCH/+1 or DBL_TMP/+1). */
static int tp_push_sv_touches(const SValue *sv, int lo, int hi)
{
    int v = sv->r & VT_VALMASK;

    if (v >= 0 && v < VT_CONST && v >= lo && v < hi)
        return 1;
    return !tp_push_sv_is_pure(sv) &&
           lo < FRAME_SLOT_MIN_LOCALS && hi > TP_SCRATCH_BASE;
}

/* Push one call word while others already sit in workspace slots: the spills
 * and parked words [spill_lo, spill_hi), the stacked words stored so far
 * [area_lo, area_hi) and an indirect target in fn_slot (-1: none).  The
 * staging rules below - fresh spill slots, parking, late_impure - exist so
 * that the push touches none of them; emit_instr checks it, so a push that
 * breaks them (a new path through the scratch slots, say) stops the compile
 * instead of passing a clobbered argument, as c-testsuite 09003 once did. */
static void tp_stage_push(SValue *sv, int spill_lo, int spill_hi,
                          int area_lo, int area_hi, int fn_slot)
{
    tp_staged_lo[0] = spill_lo;
    tp_staged_hi[0] = spill_hi;
    tp_staged_lo[1] = area_lo;
    tp_staged_hi[1] = area_hi;
    tp_staged_lo[2] = fn_slot;
    tp_staged_hi[2] = fn_slot < 0 ? fn_slot : fn_slot + 1;
    tp_staging_push = 1;
    tp_push_sv(sv);
    tp_staging_push = 0;
}

/* Materialise the call words.  Returns the slot holding an indirect target
 * (-1 for a direct call).  *abc_slot is -1 when the register words are left
 * in A/B/C, else the first of the three slots holding them. */
static int tp_stage_call_words(struct tp_call_words *cw, int nb_args,
                               int callarg_debug, int *abc_slot)
{
    int i, arg_index;
    int reg_words = cw->count > 3 ? 3 : cw->count;
    int stack_words = cw->count > 3 ? (cw->count - 3) : 0;
    int spill_base = stack_words;
    int call_setup_words;
    int indirect_fn_slot = -1;
    SValue *fn_sv;
    int is_direct;
    int *parked = NULL;
    int park_base, nparked = 0, park_fn = 0, late_impure = 0;

    *abc_slot = -1;

    /* Fast path: the first three ABI words go to A/B/C, and the hardware
     * evaluation stack is exactly three deep, so when every one of them is a
     * pure push they can simply be pushed in reverse ABI order and left where
     * they land.  No spill, no reload.
     *
     * The slow path below exists because evaluating argument n generally needs
     * the whole evaluation stack and would destroy arguments 0..n-1, so each
     * one is written out to a workspace word (`stl s`) and read back
     * (`ldl s`) once they are all in place.  That is two extra instructions
     * and one extra stage-5 write per argument - and over a kernel corpus most
     * arguments are a local, a constant or a value already sitting in a
     * workspace slot, none of which need the stack at all.
     *
     * Reordering the pushes is sound precisely because they are pure: no push
     * can observe or disturb another, so left-to-right and right-to-left
     * produce the same three values.  (The arguments were fully evaluated by
     * the front end long before this point; all that happens here is
     * materialisation.)
     *
     * Restricted to calls with no stacked arguments: with stack_words > 0 the
     * outgoing area is written before the register words are materialised, and
     * keeping that order avoids reasoning about whether a stacked argument's
     * store can alias a later argument's load. */
    if (stack_words == 0 && reg_words > 0 && !tp_no_callopt()) {
        int all_pure = 1;
        for (i = 0; i < reg_words; i++) {
            if (!tp_push_sv_is_pure(&cw->words[i])) {
                all_pure = 0;
                break;
            }
        }
        if (all_pure) {
            SValue *fn_sv = &vtop[-nb_args];
            int is_direct = (fn_sv->r & VT_SYM) && !(fn_sv->r & VT_LVAL);

            /* An indirect target has to be parked before the arguments go up,
             * because materialising it needs the stack they would occupy. */
            if (!is_direct) {
                indirect_fn_slot = FRAME_SLOT_DBL_TMP;
                tp_push_sv(fn_sv);
                Tp_STL(indirect_fn_slot);
            }
            /* The DUP shortcut in tp_push_sv() compares against whatever was
             * last left in AReg, which here is either nothing or the indirect
             * target - neither of which is an argument. */
            tp_areg_invalidate();
            for (i = reg_words - 1; i >= 0; i--) {
                if (callarg_debug) {
                    fprintf(stderr,
                            "[CALLARG_DIRECT] func=%s abi_word=%d r=0x%x bt=0x%x\n",
                            funcname ? funcname : "<unknown>", i,
                            cw->words[i].r, cw->words[i].type.t & VT_BTYPE);
                }
                tp_stage_push(&cw->words[i], 0, 0, 0, 0, indirect_fn_slot);
            }
            return indirect_fn_slot;
        }
    }

    fn_sv = &vtop[-nb_args];
    is_direct = (fn_sv->r & VT_SYM) && !(fn_sv->r & VT_LVAL);

    if (stack_words > 0 && spill_base < func_max_slot_used + 1)
        spill_base = func_max_slot_used + 1;

    /* Stacked word a is stored to Wptr[a-3], top down, so no word pushed
     * after it may touch Wptr[a-2 .. stack_words-1]: a pseudo-register
     * holding a later word's value or address, or the scratch slots an
     * impure load goes through, can lie inside the staging area once it
     * is large enough.  Such words are parked above the spills before the
     * first store, and so is an indirect target that would read the area,
     * or be written into FRAME_SLOT_DBL_TMP inside it or over a spill. */
    if (stack_words > 0) {
        parked = tcc_malloc(cw->count * sizeof(int));
        for (arg_index = 3; arg_index < cw->count; arg_index++) {
            parked[arg_index] = -1;
            if (tp_push_sv_touches(&cw->words[arg_index], arg_index - 2,
                                   stack_words))
                parked[arg_index] = nparked++;
        }
        park_fn = !is_direct &&
                  (tp_push_sv_touches(fn_sv, 0, stack_words) ||
                   FRAME_SLOT_DBL_TMP < stack_words ||
                   (FRAME_SLOT_DBL_TMP >= spill_base &&
                    FRAME_SLOT_DBL_TMP < spill_base + reg_words));
        if (park_fn)
            nparked++;

        /* Spills and parked words must also survive every push after the
         * first spill, and an impure one may use any scratch slot.
         * spill_base came from func_max_slot_used *so far*, which says
         * nothing about the scratch a push still to come will use: when a
         * two-sb 16-bit store (slot 15 only) was the function's one earlier
         * scratch use, c-testsuite 09003's printf spills landed on
         * FRAME_SLOT_DBL_TMP and the -mt800 halfword loads of the next
         * arguments overwrote them.  Keep them out of the scratch region. */
        for (i = 1; i < cw->count; i++)
            if (!tp_push_sv_is_pure(&cw->words[i]))
                late_impure = 1;
        if (!is_direct && !tp_push_sv_is_pure(fn_sv))
            late_impure = 1;
        if (late_impure && spill_base < FRAME_SLOT_MIN_LOCALS &&
            spill_base + reg_words + nparked > TP_SCRATCH_BASE)
            spill_base = FRAME_SLOT_MIN_LOCALS;
    }
    park_base = spill_base + reg_words;

    call_setup_words = park_base + nparked;
    if (call_setup_words < stack_words)
        call_setup_words = stack_words;
    if (call_setup_words > func_max_outgoing_words)
        func_max_outgoing_words = call_setup_words;
    if (stack_words > func_max_stack_words)
        func_max_stack_words = stack_words;

    for (i = 0; i < reg_words; i++) {
        int spill_slot = spill_base + i;
        if (callarg_debug) {
            fprintf(stderr,
                    "[CALLARG_REG_MAT] func=%s abi_word=%d spill=%d r=0x%x v=0x%x bt=0x%x c.i=%ld lval=%d\n",
                    funcname ? funcname : "<unknown>", i, spill_slot,
                    cw->words[i].r, cw->words[i].r & VT_VALMASK,
                    cw->words[i].type.t & VT_BTYPE, (long)cw->words[i].c.i,
                    (cw->words[i].r & VT_LVAL) != 0);
        }
        tp_stage_push(&cw->words[i], spill_base, spill_slot, 0, 0, -1);
        Tp_STL(spill_slot);
    }

    if (parked) {
        int k = 0;

        for (arg_index = 3; arg_index < cw->count; arg_index++) {
            if (parked[arg_index] < 0)
                continue;
            parked[arg_index] = park_base + k;
            tp_stage_push(&cw->words[arg_index], spill_base, park_base + k,
                          0, 0, -1);
            Tp_STL(parked[arg_index]);
            k++;
        }
        if (park_fn) {
            indirect_fn_slot = park_base + k;
            tp_stage_push(fn_sv, spill_base, indirect_fn_slot, 0, 0, -1);
            Tp_STL(indirect_fn_slot);
        }
    }

    for (arg_index = cw->count - 1; arg_index >= 3; arg_index--) {
        int slot_off = (arg_index - 3) * 4;
        if (callarg_debug) {
            fprintf(stderr,
                    "[CALLARG_STACK] func=%s abi_word=%d slot_off=%d r=0x%x v=0x%x bt=0x%x c.i=%ld lval=%d\n",
                    funcname ? funcname : "<unknown>", arg_index, slot_off,
                    cw->words[arg_index].r, cw->words[arg_index].r & VT_VALMASK,
                    cw->words[arg_index].type.t & VT_BTYPE,
                    (long)cw->words[arg_index].c.i,
                    (cw->words[arg_index].r & VT_LVAL) != 0);
        }
        if (parked && parked[arg_index] >= 0)
            Tp_LDL(parked[arg_index]);
        else
            tp_stage_push(&cw->words[arg_index], spill_base,
                          park_base + nparked, arg_index - 2, stack_words, -1);
        Tp_STL(slot_off / 4);
    }
    tcc_free(parked);

    if (!is_direct && !park_fn) {
        indirect_fn_slot = FRAME_SLOT_DBL_TMP;
        tp_stage_push(fn_sv, spill_base, park_base + nparked, 0, stack_words,
                      -1);
        Tp_STL(indirect_fn_slot);
    }

    /* An indirect call stores the register words below Wptr from slots;
     * with stacked words, hand it the spills, which lie clear of the
     * staging area, rather than reloading them for it to park again at
     * FRAME_SLOT_SPILL_A..C, which the area can cover. */
    if (!is_direct && stack_words > 0) {
        *abc_slot = spill_base;
        goto ready;
    }

    for (i = reg_words - 1; i >= 0; i--) {
        int spill_slot = spill_base + i;
        if (callarg_debug) {
            fprintf(stderr,
                    "[CALLARG_REG_LDL] func=%s abi_word=%d spill=%d\n",
                    funcname ? funcname : "<unknown>", i, spill_slot);
        }
        Tp_LDL(spill_slot);
    }

ready:
    if (callarg_debug) {
        fprintf(stderr,
                "[CALLARG_READY] func=%s nb_args=%d call_words=%d reg_words=%d stack_words=%d vtop_r=0x%x\n",
                funcname ? funcname : "<unknown>", nb_args, cw->count,
                reg_words, stack_words, vtop ? vtop->r : -1);
        fflush(stderr);
    }

    return indirect_fn_slot;
}

/* Bytes reserved for a direct call's prefixes and opcode. */
#define TP_CALL_SLOT 6

/* Width of the slot for a direct call to `sym`, storing the callee's section
 * offset in *target when it is narrower than TP_CALL_SLOT.
 *
 * A callee this object already holds earlier in the same section gets only
 * the width its displacement needs, and nothing afterwards can lengthen that
 * displacement: tp_relax_function() treats the call as a branch to the fixed
 * target (near_call_log), the linker keeps an input section's bytes in
 * order, and ICF never folds a function a narrowed call reaches
 * (tp_icf_fold).  Left out are inline functions, whose per-object outlines
 * are what ICF folds into one copy: a narrowed call would keep every outline
 * alive.  (An earlier attempt, before the ICF guard, narrowed those too and
 * the kernel link failed with 32 IPREL_PATCH_FAIL, e.g. current_thread_info.)
 * Also left out are weak definitions, which a strong one elsewhere replaces.
 * Forward and external callees keep TP_CALL_SLOT.
 *
 * Nothing is emitted under nocode_wanted (typeof, sizeof, dead code), so
 * nothing may be logged either: an entry at the frozen `ind` would describe
 * whatever is emitted there next - in the kernel, a full-width call to
 * another function, which the relaxer then re-targeted. */
static int tp_call_slot_width(Sym *sym, int *target)
{
    ElfSym *es;
    int w;

    if (nocode_wanted || !sym->c || (sym->type.t & VT_INLINE) || sym->a.weak)
        return TP_CALL_SLOT;
    es = elfsym(sym);
    if (!es || es->st_shndx != cur_text_section->sh_num
        || ELFW(ST_BIND)(es->st_info) == STB_WEAK
        || (int)es->st_value > ind)
        return TP_CALL_SLOT;
    for (w = 1; w < TP_CALL_SLOT; w++)
        if (tp_encode_len(TP_OP_CALL, (int)es->st_value - (ind + w)) <= w)
            break;
    *target = (int)es->st_value;
    return w;
}

static void tp_emit_call_target(int call_word_count, int indirect_fn_slot,
                                int abc_slot, int callarg_debug)
{
    if ((vtop->r & VT_SYM) && !(vtop->r & VT_LVAL)) {
        int i, target = 0;
        int w = tp_call_slot_width(vtop->sym, &target);
        greloc(cur_text_section, vtop->sym, ind, R_TRANSPUTER_IPREL32);
        if (tp_debug_check()) {
            fprintf(stderr, "[CALL_DEBUG] func=%s call_to=%s at ind=0x%x\n",
                    funcname ? funcname : "<unknown>",
                    get_tok_str(vtop->sym->v, NULL), ind);
        }
        if (tp_gen_sym_filter) {
            const char *name = get_tok_str(vtop->sym->v, NULL);
            if (name && strstr(name, tp_gen_sym_filter)) {
                int bt = vtop->type.t & VT_BTYPE;
                fprintf(stderr,
                        "GEN_RELOC: kind=call sym=%s type=%d bt=0x%x file=%s line=%d\n",
                        name, R_TRANSPUTER_IPREL32, bt,
                        file ? file->filename : "<unknown>",
                        file ? file->line_num : -1);
            }
        }
        /* Six bytes carry a 24-bit displacement, +/-16 MB, which reaches any
         * call within one image; a full 32-bit operand would take eight, and
         * calls are a quarter of all kernel code, so the two spare bytes were
         * 6% of it.  The linker re-encodes the slot and fails the link for a
         * target out of reach, except an unresolved one at address 0, which
         * it handles itself (tp_patch_iprel).  A callee already behind us in
         * this section gets a narrower slot, holding the real displacement
         * (front-padded with pfix 0 where the shortest encoding is shorter
         * still); see tp_call_slot_width. */
        if (w < TP_CALL_SLOT) {
            uint8_t enc[16];
            int len = tp_encode_instr(enc, TP_OP_CALL, target - (ind + w));
            near_call_add(ind, target);
            for (i = len; i < w; i++)
                g((TP_OP_PFIX << 4) | 0);
            for (i = 0; i < len; i++)
                g(enc[i]);
        } else {
            for (i = 0; i < TP_CALL_SLOT - 1; i++)
                g((TP_OP_PFIX << 4) | 0);
            g((TP_OP_CALL << 4) | 0);
        }
        if (callarg_debug) {
            fprintf(stderr, "[CALLARG_CALL_DIRECT] func=%s sym=%s ind=0x%x\n",
                    funcname ? funcname : "<unknown>",
                    vtop->sym ? get_tok_str(vtop->sym->v, NULL) : "<null>",
                    ind);
            fflush(stderr);
        }
    } else {
        int num_reg_args = call_word_count > 3 ? 3 : call_word_count;

        if (abc_slot < 0) {
            abc_slot = FRAME_SLOT_SPILL_A;
            if (num_reg_args >= 1)
                Tp_STL(FRAME_SLOT_SPILL_A);
            if (num_reg_args >= 2)
                Tp_STL(FRAME_SLOT_SPILL_B);
            if (num_reg_args >= 3)
                Tp_STL(FRAME_SLOT_SPILL_C);
        }

        if (num_reg_args >= 3) {
            Tp_LDL(abc_slot + 2);
            Tp_LDLP(0);
            Tp_STNL(-1);
        }
        if (num_reg_args >= 2) {
            Tp_LDL(abc_slot + 1);
            Tp_LDLP(0);
            Tp_STNL(-2);
        }
        if (num_reg_args >= 1) {
            Tp_LDL(abc_slot);
            Tp_LDLP(0);
            Tp_STNL(-3);
        }

        emit_instr(TP_OP_AJW, -4);

        {
            uint8_t enc_buf[8];
            int fn_slot_shifted = indirect_fn_slot + 4;
            int ldl_len = tp_encode_instr(enc_buf, TP_OP_LDL, fn_slot_shifted);
            int skip = 1 + ldl_len + 1;
            emit_instr(TP_OP_LDC, skip);
            Tp_OPR(TP_OPR_LDPI);
            Tp_STL(0);
        }

        Tp_LDL(indirect_fn_slot + 4);
        Tp_OPR(TP_OPR_GCALL);

        if (callarg_debug) {
            fprintf(stderr, "[CALLARG_CALL_INDIRECT] func=%s fn_slot=%d ind=0x%x\n",
                    funcname ? funcname : "<unknown>", indirect_fn_slot, ind);
            fflush(stderr);
        }
    }
}

ST_FUNC void gfunc_call(int nb_args) {
    /* Function call - TCWG calling convention:
     * - First 3 args already in Areg, Breg, Creg
     * - Additional args on workspace (not implemented yet)
     * - CALL instruction:
     *   1. Stores Iptr, Areg, Breg, Creg at new Wptr
     *   2. Adjusts Wptr down by 4 words
     *   3. Jumps to target address (in Areg before CALL)
     * - Function address should be in vtop (TOS)
     *
     * vstack management:
     * - TCC's frontend pushes all arguments including any hidden struct return pointer
     * - nb_args includes the hidden pointer if present
     * - We pop ALL arguments (nb_args entries)
     * - We pop the function address (1 entry)
     * - TCC's frontend will push the return value after gfunc_call returns
     *   (see tccgen.c lines 6275-6289: vsetc() after gfunc_call)
     */

    int i;
    int callarg_debug = tp_callarg_debug;
    struct tp_call_words call_words;
    int indirect_fn_slot, abc_slot;
    /* The callee's return type, read before the argument pops move vtop.
     * VT_INT is the conservative fallback: it captures AReg only, which is
     * what R_RET() names for every non-float, non-64-bit type. */
    int ret_type_t = VT_INT;
    /* An old-style callee's declared type says nothing about what the caller
     * reads back: gen_opl() and the conversion helpers call __divdi3 & co.
     * through func_old_type ("int f()") and then read REG_IRET/REG_IRE2 or
     * the float slots themselves.  Capture every result word for those. */
    int capture_all = 1;

    {
        SValue *fsv = vtop - nb_args;
        if (fsv >= vstack && (fsv->type.t & VT_BTYPE) == VT_FUNC &&
            fsv->type.ref) {
            ret_type_t = fsv->type.ref->type.t;
            capture_all = fsv->type.ref->f.func_type == FUNC_OLD;
        }
    }

    call_words.words = NULL;
    call_words.count = 0;
    call_words.cap = 0;

    if (callarg_debug) {
        fprintf(stderr, "[CALLARG_ENTER] func=%s nb_args=%d vtop_r=0x%x\n",
                funcname ? funcname : "<unknown>", nb_args, vtop ? vtop->r : -1);
        fflush(stderr);
    }
    if (tp_try_inline_memcpy(nb_args)) {
        if (callarg_debug) {
            fprintf(stderr, "[CALLARG_MEMCPY_INLINE] func=%s ind=0x%x nb_args=%d\n",
                    funcname ? funcname : "<unknown>", ind, nb_args);
            fflush(stderr);
        }
        return;
    }
    if (tp_try_inline_local_zero_memset(nb_args)) {
        if (callarg_debug) {
            fprintf(stderr, "[CALLARG_MEMSET_INLINE] func=%s ind=0x%x nb_args=%d\n",
                    funcname ? funcname : "<unknown>", ind, nb_args);
            fflush(stderr);
        }
        return;
    }

    /* Save any register-held values to the stack before the call.
     * This matches x86 (save_regs(nb_args+1)) and ARM behavior.
     * Without this, values cached in registers would be clobbered by CALL. */
    save_regs(nb_args + 1);

    tp_expand_call_words(&call_words, nb_args, callarg_debug);
    indirect_fn_slot = tp_stage_call_words(&call_words, nb_args, callarg_debug,
                                           &abc_slot);

    /* Pop all arguments from vstack */
    for (i = 0; i < nb_args; i++) {
        vtop--;
    }

    /* Now vtop points to the function address.
     * For direct calls with a symbol, emit a relocation so the linker can patch the offset.
     * For indirect calls (function pointers), we would need GCALL instead.
     */
    tp_emit_call_target(call_words.count, indirect_fn_slot, abc_slot,
                        callarg_debug);

    /* Pop function address from vstack */
    vtop--;

    /* Capture the ABI result words from A/B into workspace slots.
     *
     * Only the slots the front end will actually read are worth writing.
     * tccgen labels the call's result with PUT_R_RET(), i.e. R_RET(t) and
     * R2_RET(t) of the callee's *return type*, so those two macros decide
     * exactly which of REG_IRET / REG_IRE2 / REG_FRET / REG_FRET+1 can ever
     * be read afterwards:
     *
     *   float            -> FRET
     *   double           -> FRET, FRET+1
     *   long long        -> IRET, IRE2
     *   anything else    -> IRET
     *
     * This used to store all four unconditionally - six instructions, four of
     * them writes, after every single call - and a write is the T9000's
     * scarcest pipeline resource: stage 5 issues one per cycle, so each one
     * closes an instruction group, and an STL burns a stage-2 address
     * calculation on top.  Over a kernel corpus that was ~10% of the whole
     * instruction stream spent copying words nothing reads.
     *
     * Anything left in BReg afterwards is dead: save_regs() above has already
     * spilled every vstack entry that is not an argument to this call, so no
     * live value names a register here, and the value the front end pushes
     * next names only the slots written below.
     *
     * The eval-stack scheduler models the depth after a call as 2 and it stays
     * correct: dropping a store leaves the corresponding word in place, which
     * is exactly the depth the model already assumes. */
    if (tp_no_callopt() || capture_all) {
        /* Old-style callee (see capture_all), or the bisection escape hatch:
         * the unconditional four-slot capture. */
        emit_instr(TP_OP_STL, REG_IRET);
        emit_instr(TP_OP_STL, REG_IRE2);
        Tp_LDL(REG_IRET);
        emit_instr(TP_OP_STL, REG_FRET);
        Tp_LDL(REG_IRE2);
        emit_instr(TP_OP_STL, REG_FRET + 1);
    } else {
        int bt = ret_type_t & VT_BTYPE;

        if (is_float(ret_type_t)) {
            emit_instr(TP_OP_STL, REG_FRET);
            if (bt == VT_DOUBLE || bt == VT_LDOUBLE)
                emit_instr(TP_OP_STL, REG_FRET + 1);
        } else {
            emit_instr(TP_OP_STL, REG_IRET);
            if (bt == VT_LLONG)
                emit_instr(TP_OP_STL, REG_IRE2);
        }
    }
    abcreg_stack_depth = 0;

    /* A call clobbers the hardware A/B/C evaluation stack.  The return-word
     * capture above reconstructs only the ABI result, not any value that was
     * cached before the call.  Leaving this tag live can make the next
     * tp_push_sv() emit DUP instead of reloading its local; Linux rwsem then
     * forwards a post-call workspace value as the semaphore pointer. */
    tp_areg_invalidate();

    if (callarg_debug) {
        fprintf(stderr, "[CALLARG_EXIT] func=%s ind=0x%x\n",
                funcname ? funcname : "<unknown>", ind);
        fflush(stderr);
    }

    tcc_free(call_words.words);
}

/* Leave a canonical double's bits in ABI A=low, B=high without a copy
 * through IRET/IRE2.  Decline before emitting anything if gv() still has
 * work to do (bounds/casts/unaligned accesses in particular). */
static int tp_push_double_return(SValue *sv, int bt)
{
    int fr, v;

    if ((bt != VT_DOUBLE && bt != VT_LDOUBLE) ||
        (sv->type.t & VT_BTYPE) != bt)
        return 0;
    fr = sv->r;
    v = fr & VT_VALMASK;
    if ((fr == VT_CONST || fr == (VT_CONST | VT_NONCONST)) && !sv->sym) {
        union { double d; uint64_t u; } bits;
        /* gv() would first put this already-converted literal in rodata,
         * then load and park its two words.  Return the identical bits
         * directly; do not evaluate it with the target FPU (not even zero,
         * whose sign must survive).  Symbol/flagged operands still use gv. */
        bits.d = sv->c.d;
        tp_areg_invalidate();
        abcreg_stack_depth = 0;
        Tp_LDC((uint32_t)(bits.u >> 32));
        Tp_LDC((uint32_t)bits.u);
        return 1;
    }
    if (fr >= TREG_FA && fr < NB_REGS &&
        sv->r2 >= TREG_FA && sv->r2 < NB_REGS && fr != sv->r2) {
        /* Use the actual high slot, including nonadjacent rvalue pairs. */
        tp_areg_invalidate();
        abcreg_stack_depth = 0;
        Tp_LDL(sv->r2);
        Tp_LDL(fr);
        return 1;
    }
    if (!(fr & VT_LVAL) ||
        (fr & ~(VT_VALMASK | VT_LVAL | VT_SYM)) ||
        (sv->type.t & (VT_VOLATILE | VT_BITFIELD)) ||
        tp_word_unaligned(sv) ||
        ((fr & VT_SYM) && v != VT_CONST) ||
        (v == VT_CONST && sv->sym && !(fr & VT_SYM)) ||
        (v != VT_LOCAL && v != VT_LLOCAL && v != VT_CONST && v >= NB_REGS) ||
        (v != VT_LOCAL && tp_t414()))
        return 0;
    if (v == VT_LOCAL) {
        Sym *s;
        /* gen_cast() strips qualifiers.  As for tp_push_simple(), require
         * the original unescaped local symbol before reversing its reads:
         * the scheduler can fold two LDLs followed by REV into swapped
         * LDLs, so emitting low/high/REV alone is not a volatile barrier. */
        for (s = local_stack; s; s = s->prev)
            if (s == sv->sym &&
                (s->r & (VT_VALMASK | VT_LVAL)) == (VT_LOCAL | VT_LVAL) &&
                s->c == sv->c.i && !(s->type.t & (VT_VOLATILE | VT_BITFIELD)) &&
                (s->type.t & VT_BTYPE) == bt && !s->a.addrtaken)
                break;
        if (!s)
            return 0;
    }

    tp_areg_invalidate();
    abcreg_stack_depth = 0;
    if (v == VT_LOCAL) {
        /* Proven nonvolatile storage needs no address or parked pair. */
        emit_local_op(TP_OP_LDL, sv->c.i + 4);
        abcreg_push(1);
        emit_local_op(TP_OP_LDL, sv->c.i);
        abcreg_push(1);
    } else {
        /* Share the canonical/spilled/symbol address handling with FP
         * operands.  Alignment was checked above, so no byte-copy scratch
         * is needed.  Read low then high, retaining both on the stack.
         * T414 has no DUP and retains load()'s existing staging path. */
        tp_emit_float_addr(sv);
        Tp_DUP();
        Tp_LDNL(0);
        Tp_REV();
        Tp_LDNL(1);
        Tp_REV();
    }
    return 1;
}

ST_FUNC void gfunc_return(CType *func_type) {
    /* Handle struct returns - structs may occupy multiple vstack entries */
    if ((func_type->t & VT_BTYPE) == VT_STRUCT) {
        CType type, ret_type;
        int ret_align, ret_nregs, regsize;

        /* Query how many registers are needed for this struct */
        ret_nregs = gfunc_sret(func_type, func_var, &ret_type,
                               &ret_align, &regsize);

        if (ret_nregs < 0) {
            /* Struct returned via hidden pointer (already handled) */
            /* vstore() already consumed vstack, just pop final entry */
        } else if (ret_nregs == 0) {
            /* Copy to implicit first pointer arg location.
             * vstore() calls gen_struct_copy() which now properly manages vstack. */
            type = *func_type;
            mk_pointer(&type);
            vset(&type, VT_LOCAL | VT_LVAL, func_vc);
            indir();
            vswap();
            vstore();
            /* vstore+gen_struct_copy consumed both source and dest, leaving dest_copy.
             * The final vtop-- at the end of this function will pop it. */
        } else {
            /* For Transputer, return values are typically in AReg */
            gv(RC_INT);
        }
    } else {
        /* Simple return: load into return register
         *
         * CRITICAL FIX (2026-01-17): gv(r) stores the value into
         * workspace slot r (slot 7 for int), but RET expects the return
         * value in AReg (hardware A register). We must explicitly load
         * from the workspace slot into AReg before returning.
         */
        int bt = func_type->t & VT_BTYPE;
        if (bt != VT_VOID && !tp_push_double_return(vtop, bt)) {
            /* Cleanups / prior calls may have left a stale AReg tag or
             * clobbered slot 7.  Force a full rematerialize into IRET and
             * then into hardware A so RET sees the true return value. */
            tp_areg_invalidate();
            gv(RC_IRET);  /* Load return value into REG_IRET (workspace slot 7) */

            /* Load from slot back to Hardware Stack */
            if (bt == VT_LLONG || bt == VT_DOUBLE || bt == VT_LDOUBLE) {
                /* 64-bit return: Needs 2 stack entries.
                   Order depends on calling convention (A=Low vs A=High).
                   Assuming A=Low, B=High:
                */
                Tp_LDL(REG_IRE2); /* Load High (Slot 8) -> A */
                Tp_LDL(REG_IRET); /* Load Low  (Slot 7) -> A, pushes High to B */
            } else {
                /* The caller takes the result from AReg, and this entry
                 * is popped below, so nothing reads slot 7 again: the
                 * `stl 7` gv() just parked it with cancels, and `return
                 * x;` is `ldl x` rather than `ldl x; stl 7; ldl 7`. */
                tp_push_reg_or_cancel_stl(REG_IRET, vtop);
            }
        }
    }

    /* Pop final vstack entry - do this once at the end like ARM64 */
    vtop--;
}
