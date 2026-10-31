# T9000 ISA opportunities — ranked action list

Generated 2026-08-04 by a 13-agent survey of `transputer/t9000ism.txt` and
`transputer/t9000hrm.txt` cross-checked against `transputer-gen.c`, the T4
emulator and real disassembly. Six instruction families were surveyed in
parallel, every candidate was then put to an adversarial verifier whose
default was to reject it, and only candidates the verifier personally
re-checked appear here. 31 of the original candidates survived.

Rejected items are kept, with reasons, so they are not re-proposed.

# Ranked action list — TCC transputer backend

Ordering is by (impact / effort). Sites are given as function + nearest anchor text in `/home/djs/tinycc-transputer/transputer-gen.c`; line numbers drift, the anchors do not.

## Measurement harness (use the same one for every item)

- **Bytes** (the fetch-bound metric this project trusts): `LD_LIBRARY_PATH="" ./transputer/build.sh --kernel` then compare `.text` from `readelf -S` on `transputer/linux-7/vmlinux.o`, plus a per-file `size` diff on CoreMark/fbwm objects. Also re-run `transputer/ttools/dast/dast -s 0x34 -O 0` on the specific test function to confirm the emitted shape.
- **Cycles**: run under `T4_T9000=1` (grouper model in `transputer/t4/t9000_group.h`). Do **not** judge by CoreMark Iterations/Sec — its timer is broken. Judge by cycle count and `.text`.
- Every item below needs the narrow-access regression tests (commit `0db5bbbd7`, `transputer/tests/`) plus `t4 -se` clean (Error flag), since several touch checked-vs-unchecked arithmetic.

---

## (A) Do now — contained and clearly positive

**A1. Replace the 37-instruction byte-store RMW with `sb`.** `store()`, `case 1:` — the `if (is_volatile)` arm already emits `Tp_SB()`; the `else` RMW below it (~lines 2396-2475) is what to delete. Use the volatile arm verbatim: it already proves the eval-stack depth-2 discipline. `sb` is base-set (0x3B, `23 FB`), emulated at `t4/p.c:5580` with no target guard, no gating needed. The in-source "coherence" comment does not survive scrutiny — a word RMW rewrites three neighbouring bytes and is *less* coherent — and git history shows no bug-fix commit behind it, only the squashed import. Measured today: `void sbf(char*a,int i,char v){a[i]=v;}` is 106 bytes at `-mt800` **and 96 at `-mt9000`**; expect ~10. Biggest byte win on the list for the smallest diff. **Do this first.**

**A2. Build the T800 16-bit store from two `sb`s.** `store()`, `case 2:`, the `else` arm of `tp_t9000_store16_ok()` (~2476-2530), which today runs the A1 RMW *twice*. Replace with `ldl val; ldl addr; sb` then `ldl val; ldc 8; shr; ldl addr; adc 1; sb`. Verified against emulator semantics (`sb` wants A=addr/B=val; `adc` touches AReg only, so `val>>8` stays in BReg; byte order matches `ss` at `p.c:6144`). 181 bytes → ~15 on the test case. Land and test together with A1 — it depends on A1's finding that `sb` on plain RAM is safe. `-mt9000` is untouched (keeps `ss`).

**A3. Turn on the `xword` path for sign extension.** `tp_sign_extend_8_in_areg` / `tp_sign_extend_16_in_areg` (~1553-1600): `USE_XWORD` is defined *nowhere in the tree*, so `-mt800` always takes the 6-instruction `and/xor/diff` fallback. `xword` is 0x3A, base set, in `transputer-opcodes.h:93`, has a working `Tp_XWORD()` wrapper and peephole entry, and is fully emulated at `p.c:5553` including the ACWG precondition. Frequency on the current `linux-7/vmlinux.o`: 11512 occurrences of the byte-form 8-bit sequence, 1209 of the 16-bit. 11 → 8 bytes and 17 → 12 bytes per site, two stage-4 ops → one.
  - **Keep the `ldc mask; and`.** The "provably dead AND" claim is only true on the LB-sourced load paths (~1755, ~1863), where the value is already 0..255. `gen_cvt_csti` (~3238, ~3245) reaches the same helper with an arbitrary int, and dropping the mask there makes `(signed char)0x100` yield `0x100`. If you want the extra 2 instructions on the load paths, add a `masked_already` flag parameter — do not edit blanket.

**A4. Delete the double `eqc` for `x != 0`.** `gen_opi`, `case TOK_NE`, the `if (eqc_const_opt)` arm (~7969-7975). When `eqc_const_val == 0`, skip both `emit_instr(TP_OP_EQC, ...)` calls and fall through to the deferred form the non-const path already uses (`vset_VT_CMP(TOK_NE)` with the raw value live). Both consumers are safe: `gjmp_cond` keys purely on `op&1`, and `load()`'s VT_CMP handler re-normalises. Strictly 2 instructions / 2 bytes / 2 stage-4 slots, never a loss. Fires on every `if (p)`, `if (n)`, `while (n)`.

**A5. Delete the inverting `eqc` in `TOK_GE` / `TOK_LE`.** `gen_opi` (~7997-8016). Drop `emit_instr(TP_OP_EQC, 0)` and set `vset_VT_CMP(TOK_EQ)` ("zero is true") instead of `TOK_NE` — the exact mechanism `TOK_EQ`'s `Tp_DIFF()` path above already uses. Branch context saves 0 or 2 depending on sense; value context is better than advertised: `int ge(int a,int b){return a>=b;}` currently emits **three** `eqc`s, this leaves one.

**A6. Drop the unconditional `rev` before `gt` for `TOK_LT` / `TOK_GE`.** Same switch. Swap `sv_first`/`sv_second` before `tp_push_sv` (~7788-7797), next to the existing `TOK_EQ`/`TOK_NE` commutation swap at ~7725-7738 — that is the established side-effect-safe pattern in this file. Do **not** try to "vswap before gv2"; gv2 has already run. Note the existing peephole rule S7 that was supposed to catch this reports `S7=0` on every function — it is silently failing on exactly the shape its comment describes, so fix it at the source rather than in the peephole.

**A7. One `move` per struct-by-value argument, not one per word.** `tp_expand_struct_call_words` (~6370, `for (w = 0; w < words; w++)`). `spill_base` is already computed once and word-aligned, slots strictly contiguous at `spill_base + w*4`, so a single `move` of `size` bytes before the loop is semantically identical. Delete the per-word `Tp_LDC(0); STL spill_off` and the per-word `MOVE`; the loop then only builds SValue descriptors. Keep the pre-zero for the tail slot only when `size % 4 != 0`. 32-byte struct arg: 139 → ~55 bytes, 8 block-move setups (8 cycles fixed each in `t9000_group.h:470`) → 1, and it removes 8 group-closing STLs. Effort is *deleting* a loop body. Frequency of struct-by-value in this codebase is low, which caps the payoff.

**A8. Emit `ldnlp` instead of `adc` for constant pointer displacement.** `gen_opi`, the `adc_const_opt` branch (~7805-7815), gated on `tp_ptr_add_is_addr`: when the constant is a positive multiple of 4, emit `ldnlp (n/4)`. `ldnlp` is primary 5, every target, `p.c:1687` `index(a,b) = a + 4b`, no alignment precondition and no error signal — bit-exact substitution. Measured on fbwm: 99 `adc` sites with operand ≥16 currently pay a pfix that `ldnlp` would not, so ≤99 bytes there (upper bound — the disassembly count includes integer adds the gate will exclude). Bonus: it sidesteps `adc-traps-on-unsigned` for address arithmetic. Three lines.

**A9. (correctness, do with A-anything) Delete zero-offset `j`.** The relaxation fixpoint already recomputes every offset, so a `j 0` that survives is emitted as the single byte `0x00`. On T9000 hardware that is a **breakpoint trap** (ISM:2626, 12020-12021); the emulator hides it behind `EnableJ0BreakFlag`, initialised to 0. Confirmed present: one at offset 0x30 of a 5-case switch in my build. Cheap, and it is a prerequisite for trusting any `-mt9000` measurement.
  Related and *not* T9000-only, contrary to how it is usually stated: `j` is a descheduling **and** timeslicing point on every part (`D_check()` at `t4/p.c:8187-8218` is called from `j` at `p.c:4285`, and descheduling saves only Iptr/Wptr), yet TCC carries live values in AReg across `j` (`ldc 1; j L; ldc 0`). That is a latent correctness bug on real T414/T800 too and should be tracked separately.

**A10. Two-line peephole tidy (do it because it cannot be wrong, not for the win).** Add `TP_OPR_BSUB` to the commutative-operand list (~5488-5491, currently SUM/ADD/PROD/MUL/AND/OR/XOR) — `bsub` is plain commutative addition (`p.c:4703`), so the preceding `rev` is provably dead; and allow `TP_ENC_LDLP` alongside LDL/LDC in the operand-swap test (~5429-5431). Do **not** add `wsub`/`wsubdb`/`ssub` — all three use AReg as the base and are order-sensitive. Honest measurement: 7 bytes out of 10,298 on fbwm. Negligible.

---

## (B) Worth doing, needs design work

**B1. Inline `move` for `memcpy` / `__builtin_memcpy`.** Largest single win on the list; parked in B only because it is a new lowering, not a one-liner. Hook next to `tp_try_inline_local_zero_memset` (~4366, called from ~6756) — the only name-match hook that exists today. Emit exactly what `gen_struct_copy` (~3041) already emits: push src, push dest, evaluate len, `OPR 0x4A`, then materialise dest into `REG_IRET`. Works for variable length too (`move` takes the count from AReg). `move` is base set, no gating (`p.c:5800` has no `BADCODE`, unlike the `move2d*` family), and no-ops safely for count ≤ 0, so `memcpy(d,s,0)` is fine. Call site 33 bytes → ~7 (including 7 pfix-0 pad bytes, i.e. directly against the pfix-padding problem), and the uClibc word loop disappears. Copy `gen_struct_copy`'s `abcreg_pop(3)` / `abcreg_stack_depth = 0` model update. **Not for `memmove`** — see D2.

**B2. Constant-initialised local arrays / general `memset`.** Today `char buf[16]="hello"` costs ~40 bytes *per literal byte*: a full word load, mask, shift, `not`, `and`, `or`, word store, with the shift amount recomputed from `(addr & 3)` at **runtime** from a compile-time-known local offset — ~7 stage-4 ops per byte, 418 bytes for that one declaration. Two mechanisms, both needing no new opcode:
  - emit a `.data.ro` template and `ldc tpl; ldlp local; ldc size; move`;
  - generalise `tp_try_inline_local_zero_memset`, whose three guards (~4396-4400) restrict it to literal `memset(&local, 0, K)`. Fold the fill byte to `c * 0x01010101`, and use **`move`-doubling** as the primary mechanism (copy `[d,d+k)` → `[d+k,d+2k)`, strictly disjoint so the overlap-undefined rule holds; `log2(n)` moves, alignment-agnostic, any fill byte; precedent in `transputer/fbwm/fbwm.c:__move`). **Do not** extend the existing byte-store prologue to arbitrary pointers — it works only because `offset & 3` is a compile-time constant for a VT_LOCAL.

**B3. Frequency-based local slot renumbering.** `RC_INT` is 9..14, scratch fixed at 15..19, `FRAME_SLOT_MIN_LOCALS=20` (~889, 915-930), and `tp_frame_reserved_words` (~5848) only shrinks to `func_max_slot_used+1`, so real locals sit at ≥16 and every access costs the 2-byte `pfix`+`ldl` form while slots 2..8 go unused. Largest fetch-bound win available, but: `local_patch`/`emit_local_op` covers *locals only* — pseudo-registers are emitted as hardcoded `Tp_STL(r)`/`Tp_LDL(r)`, so this needs a register-slot remap too. Address-taken/aggregate locals and the `Wptr[0..n-1]` outgoing-arg words must keep fixed positions. Discount the headline 15.9%-of-`.text` estimate: it assumes near-perfect packing of the 16 hottest slots.

**B4. Short-displacement local calls.** `tp_emit_call_target` (~6641-6665) unconditionally emits `greloc(R_TRANSPUTER_IPREL32)` + 7× `pfix 0` + `call`, and `tp_relax_function` deliberately pins any run with a relocation into it (~5601-5620). For a **backward** reference within the same section the displacement is already known at emit time — that case is straightforward and covers most static helpers. Forward references need the existing per-function patch machinery or the ISM 6.2.2 grow-from-zero fixpoint. Measured: 14 of a 42-byte caller were pure padding for two same-section calls. This absorbs ~79% of the residual pfix-0 padding; what is left after it is the LDC_ABS placeholder tail.

**B5. `lend` for counted loops.** `lend` (0x21) appears nowhere in `transputer-gen.c`; it is in the *ungated* base secondary table in `transputer-asm.c:126` (so T414/T800/T9000, no `-mt9000` gate) and fully emulated at `p.c:5307-5323`. Today `for(i=0;i<100;i++)` is 16 instructions / 19 bytes / 4 control transfers per iteration, of which 10 instructions and 3 transfers are pure scaffolding. Two hard requirements: **(a)** a zero/negative-trip-count guard is mandatory, not optional — the emulator's count is `uint32_t` (`p.c:3149`) so a non-positive count runs ~2^32 iterations; **(b)** `lend` is a timeslicing point (ISM:3167), so it must be gated out of code running with descheduling disabled — ordinary code loses nothing, since today's `j` back-edge deschedules too. High effort (loop-shape recognition + the `le.Index`/`le.Count` two-word block), large payoff.

**B6. For-loop block layout: increment after body.** Confirmed 4 control transfers per iteration versus a single back-edge for `while`. But the layout is **upstream generic TCC** (`tccgen.c:8538-8557`), not the transputer backend, and it exists because TCC is single-pass and must parse the increment before the body. Fixing it means buffering/re-parsing the increment tokens or a backend block-reorder pass over `tp_relax_insn` — not a small edit at `block_for`. Complementary to B5; worth 2 jumps / 3-4 bytes / 2 group closures / 2 deschedule points per iteration.

**B7. Composed `wsub`/`wsubdb` for element sizes 12 and 16.** `tccgen.c:3801-3816` dispatches the fast path only for sizes 4/8/1/2; size 12 falls to `ldc 12; prod` + a workspace round-trip. `ldl i; ldl i; base; wsub; wsubdb` gives `base + 12i` (verified against `p.c:4874` and `p.c:6413`) using the 3-deep stack exactly; `wsubdb; wsubdb` gives 16; and any multiple of 4 can divide the `prod` constant by 4 and finish with `wsub`. Caveats: `wsubdb` is not a T414 instruction (`BADCODE(IsT414)` in the emulator), and since 2026-11-03 `-mt414` is a real target that lowers `wsubdb` to a multiply, so the new paths must be off under `tp_t414()`; the new paths inherit the `tp_ptradd_base_is_simple` restriction; and they must respect `nocode_wanted`/`CONST_WANTED` like the existing ones.

**B8. Fold the multi-dimensional subscript chain (ISM 7.5.3).** `m[i][j]` on `int m[][20]` is 10 instructions / 11 bytes today with the intermediate row address materialised; the ISM's `ldc S2; e1; prod; e2; add; ...` + one final `wsub` gives 8 / 9. Arithmetically identical, `prod` is unchecked so no trap change. Rated below B7 because it needs the per-`[` `gen_op('+')` at `tccgen.c:7280-7284` deferred and folded (side effects in index expressions, VLAs, array-of-array vs pointer-to-array decay, the CONST_WANTED guards), A8 already reclaims one of the two bytes for free, and the residual `rev; wsub` cannot be removed.

**B9. Constant-count 64-bit shifts via `lshl`/`lshr`.** `gen_opl()`'s constant branch (`tccgen.c:2725-2758`) synthesises 64-bit shifts from `gv_dup` + 32-bit ops — 15-16 instructions / 19-21 bytes — and only the variable branch reaches `gen_opl_shift`. The fix is a constant fast path in `gen_opl_shift` (~8195-8253) that skips its own `ldc 63; and; stl` clamp. Win per site is large; **site count is unmeasured** — the `lshr` counts usually cited are overwhelmingly the 32-bit SAR sequence (B10), not 64-bit shifts. Measure first.

**B10. Spill-free 32-bit `>>`.** `gen_opi` (~7934-7939) emits `STL SCRATCH2; XDBLE; LDL SCRATCH2; LSHR`; the ISM (2958-2960) gives the spill-free `ldl X; xdble; ldl n; lshr`. 1368 of 1379 `xdble` sites in `linux-7/vmlinux.o` are this shape, so ~2700 instructions and 1368 group-closing stores. **Only valid when the count is a literal on T414/T800/T805**: ISM 7.3:2189 says `and`/`or`/`xor` leave CReg *undefined*, which destroys the sign word `xdble` parked there — so the variable-count form needs C2 and is `-mt9000`-only. (The usual justification for this restriction is stated as a timing issue; the real reason is CReg lifetime.)

**B11. `__builtin_clz` / `ctz` / `ffs` via `norm`.** Per-site win is genuinely huge (21 instructions / 29 bytes of call site today, and the kernel's own fallback in `arch/transputer/kernel/stubs.c:149-170` is a bit-serial loop of up to 64 iterations). `norm` is 0x19, emulated at `p.c:5191`, and the operand order is correct as proposed (high word in BReg, so pushing value then `LDC 0` yields `clz32` in CReg). But: extracting CReg costs three STLs so the site is ~6 instructions not 5, `norm` is latency 3 / `T9K_SLOW`, and **there are 8 call sites in the whole kernel** (zero in the 2.6.39 tree). Do it only if you are in that code anyway. `clzll` needs real hi/lo push work.

---

## (C) `-mt9000` only

Nothing here is banked until the default target moves — `transputer/build.sh:849` compiles with `-mt800`, so every C item currently buys the kernel and all userspace exactly zero.

**C1. Alignment guards for the T9000 narrow path — correctness, and a prerequisite for trusting A1/A2 results.** `tp_t9000_half_ok` (~1419) tests `(byte_off & 1)`, but `tp_t9000_half_ptr_ok` (~1426) returns the feature bit with no alignment reasoning at all and `tp_t9000_store16_ok` (~1430) takes no argument and tests nothing. A packed struct reproduces the fault on **both** paths: `ldl 13; adc 1; ldlp 12; lsx; rev; ss` for the store and `ldl 11; adc 1; lsx` for the load — both would take Unalign on hardware. The emulator cannot catch it: `p.c:6144/6151/6160` implement `ss`/`ls`/`lsx` as byte pairs with no Unalign signal, so **no `t4` test can currently distinguish correct from broken `-mt9000` narrow code**. Fix the load path too, and add the Unalign check to the emulator — otherwise the `-mt9000` half of the A1/A2 regression suite proves nothing.

**C2. Drop the `ldc 31; and` shift clamp.** Three sites (~7923, 7927, 7934) plus the 64-bit `ldc 63; and` at ~8204-8209. ISM 2194-2198 says an out-of-range shift yields zero, and the dagger footnote at 2248-2249 attaches the constant-time-vs-proportional distinction to that very paragraph — the clamp is a T4/T8-only defence. The emulator already implements `if (AReg < BitsPerWord) ... else AReg = 0` (`p.c:5635-5650`), so nothing breaks. Correct, safe, currently unbanked. Also unblocks the variable-count half of B10.

**C3. `ldnlp` for constant narrow-field displacement.** `ldnlp 1` (0x51) and `adc 4` (0x84) are both one byte, so this is byte-neutral: it moves one op from stage 4 to stage 2 per constant-offset narrow field access, and stops the add being a trapping ADC. Two sub-cases: the `ssub` even-index path (~7335-7340, whose comment "LDNLP is word-scaled, so it cannot serve here" is over-broad — it is exact for even `k`), and the general narrow-field address formation, where `fld()` still emits `ldl; adc 4; lsx`. Small, low effort, but see the always-STL note below. **Reject the associated `gv2` fallback idea** — the identical thing was tried for `bsub` and measured *worse* (comment at ~7300-7307).

**C4. Double-`bsub` for T800 `short*` subscripts.** The `-mt800` fallback when `tp_ptradd_ssub` declines is `ldl i; dup; sum; ldl a; rev; bsub` — `dup` and `sum` both stage 4. `ldl i; ldl i; ldl a; bsub; bsub` gives `a+2i` with both adds at stage 2. Correction to the usual pitch: it is 10 bytes → 8, not 6 → 4, and it occupies A/B/C simultaneously so it must be integrated with the `abcreg_stack_depth` tracking the `ssub` path uses. Worth doing as the ISM-endorsed grouping pattern, not as a size win. (Listed under C because on `-mt9000` `ssub` already does this; it is the T800 path that would change.)

**C5. Switch jump tables: `ldc n; prod` → `wsub`/`wsubdb`, and the 9-byte slot.** ISM 2823-2826 licenses this verbatim. The `ldc/prod` → `wsub` half is free and safe. The `TP_SWITCH_JUMP_SLOT_SIZE 9` → 4 half is **not**: the slot size is a compile-time constant chosen before any displacement is known, and an overlong entry hits `tcc_error("transputer switch patch overflow")` today — shrinking it without a two-pass sizing scheme converts working code into a compile error on large functions. And the whole thing is currently **dead**: `gen_switch_jump_table` is gated on `T4_SWITCH_JT` (`tccgen.c:8606-8619`) because fixed-slot dispatch was implicated in maple_tree BUGs during kernel boot. Bottom of the list.

---

## (E) T9 FPU Temps T0-T7 — done in part, the rest is a real refactor

Added 2026-08-11.  The T9 core has eight 64-bit architectural FP Temps with
their own block fill/spill (`f2ldm`/`f4ldm`/`f2ldma`/`f2stm`) and the T&L
microcode that reads them (`fdot4`/`fmvmul4`/`fpoly3`).  Until now
`transputer-opcodes.h` did not even define the encodings, so no compiled
program could touch any of it.

**E1. Done — the encodings and a builtin surface.** `transputer-opcodes.h`
now carries the whole family with the operand conventions taken from
`ip/cpu/rtl/t9_fpu_bridge.sv`, and `transputer-gen.c` exposes `__tld`,
`__tst`, `__tclr`, `__f2ldm`, `__f4ldm`, `__f2ldma`, `__f2stm`, `__fmvmul4`,
`__fmvmul4st` and `__fpoly3`.  Regression test: `transputer/tests/
test_fpu_temps.c` (needs the real FPU RTL; `ip/cpu/rtl/t9_fpu_model.sv` does
not implement the Temps pool).  Measured on the RTL, a mat4 x vec4 transform
over 256 vertices: **462,076 → 192,620 cycles (2.40x)**, 82,757 → 38,133
instructions, and the FP workspace traffic (`fpldnlsn`/`fpstnlsn`/
`fpldnlmulsn`/`fpldnladdsn`) drops from 22,512 ops to 1,872 (**-91.7%**),
bit-identical results.

**E2. Not done — back the FP "registers" with Temps.** `TREG_FA/FB/FC` are
workspace slots 3/4/5, and about 62% of the FP memory traffic in float code
is spill/reload of exactly those three slots.  Pointing them at `T0/T1/T2`
(`ldc i; tst` / `ldc i; tld` instead of `ldlp n; fpstnlsn` / `ldlp n;
fpldnlsn`) is worth an estimated ~15% on the same kernel.  Four things block
it and all four are real:

- `load()`/`store()` move floats as **raw integer words** (`ldl r` / `stl r`)
  on their fallback paths, so a value can arrive in slot 3/4/5 without ever
  passing through `tp_store_fpu_result_to_slot()`.  Both would have to become
  Temp-aware or the Temp and the slot silently diverge.
- The fused `fpldnladdsn`/`fpldnlmulsn` forms take a memory **address**.  A
  Temp has no address, so every fused op against a Temp-resident operand
  becomes `ldc i; tld; fpadd` — one instruction longer.
- Temps are **global**, workspace slots are per-frame.  Every call clobbers
  T0..T2, so the whole set needs flushing at call sites where today nothing
  has to happen at all.
- `REG_FRET` *is* `TREG_FA`, and the float return value is delivered by an
  integer `stl 3` after the call (`~line 8014`).  T0 cannot back the ABI
  return slot without changing that convention too.

Doing E2 properly means giving this backend a real float value model rather
than "compute in FA, spill to a fixed slot".  Do not attempt it as a
peephole.

**E3. Not done — block fill/spill for vec2/vec4 from plain C.** Recognising
`float x=v[0],y=v[1],z=v[2],w=v[3]` and emitting one `f4ldm` only pays if the
destination is a Temp, which is E2.  As compiled today those four loads are
*integer* word copies into locals, and `f4ldm` cannot target a local.  E3 is
therefore blocked on E2, not independent of it.

**E4. Measured and negative — `f2ldma` prefetch.** A vec2 fill/spill loop
double-buffered across T0/T1, issuing the async fill for element i+1 before
draining element i: 717,498 cycles sync vs **719,866 async** (+0.3%).  The
overlap is real — the prefetched version absorbs 34,432 extra instructions
for only 2,368 extra cycles — but the fence (`is_fp_fill_fence`) puts the
consumer right behind the producer again, and the prefetch bookkeeping costs
more than the latency it hides.  `f2ldma` needs a kernel with spare Temps and
a long independent run between fill and use; a mat4 transform that already
occupies all eight has nowhere to hoist it to.

---

## (D) Rejected — do not re-propose

**D1. "`gen_struct_copy` emits a needless `rev`."** Factually false. `gen_struct_copy` (~3041-3072) contains no `Tp_REV()`; the push order is already src→CReg, dest→BReg, len→AReg, matching `movebytes_int(BReg, CReg, AReg)` at `p.c:5803`. The `rev` seen in the parameter case is *manufactured by the peephole* (rule S2, `STL a; STL b; LDL a; LDL b → REV`, ~5265-5300) as the result of folding four instructions into one — a net 3-instruction saving, not waste. Swapping the push order would break the C=src/B=dest invariant on the direct path.

**D2. 2D block moves / `devmove` / `memmove` fast path.** The proposed `memmove` guard `dest <= src || dest >= src+n` admits the overlapping `dest < src` case, where the ISM (17972-17990) says the effect is undefined and the T9000 is explicitly licensed to use word-granular reads that could corrupt. Loop recognition for `move2d` is not implementable: TCC is strictly one-pass with no IR, and the enclosing `for` header has already been emitted by the time the inner `memcpy` is lowered. `devmove` (0x62F4) has zero occurrences in `p.c` — not emulated. Gating "off T414" is `tp_t414()` since `-mt414` became a real target (2026-11-03; it already rejects `__move2d*`). And `move`/`move2d` are charged a flat 8-cycle latency in `t9000_group.h`, so the headline win is unmeasurable in this emulator anyway. A correctly-guarded `memmove` fast path is a strictly weaker duplicate of B1.

**D3. `add` (1 byte) instead of `sum` (2 bytes) for signed/pointer addition.** Trades an unchecked add for a **checked** one — `p.c:4735` calls `t4_eadd32` and `SetError`s on overflow, i.e. exactly the `adc-traps-on-unsigned` failure mode already on record (values correct, Error flag set, `t4 -se` broken). Scope is also smaller than claimed: pointer addition already goes through `wsub`, not `sum`. ~1% of `.text`, zero instruction-count or stage-4 change. Not worth converting well-defined wrapping arithmetic into an Error trap.

**D4. "Retire T4_COMPACT, pursue link-time relaxation."** The *diagnostic* is valuable and should be banked: `tp_relax_function` runs by default and produces **zero self-inflicted padding** — a fresh object had exactly 14 pfix-0 bytes for exactly 2 call relocations, 7 per reloc. So residual padding is 100% relocation placeholders. But as an optimisation it delivers nothing: deleting dead `T4_COMPACT` code is a cleanup, and the actionable payoff *is* B4. What remains after B4 is the LDC_ABS tail (~21% of ~2.2% of `.text`), and capturing it means implementing `R_TRANSPUTER_SYM_DIFF` (33) and `R_TRANSPUTER_ALIGN` (34) — reserved in `elf.h:3353-3354`, entirely unimplemented — across linker, `elf2flt` and `flat.h`. Unquantified payoff behind a large cross-tool change.

**D5. Strength-reduce signed `/` and `%` by a power of two.** Verified correct, safe, and unimplemented — and still not worth it. 39 `div` / 17 `rem` sites in `kernel/built-in.o`, and the replacement is 11 instructions where there was 1, on a machine judged by `.text` size and a 4-byte/cycle fetch bound. (The claimed trap-behaviour change for `x/0` cannot occur — the fold only fires for a nonzero constant divisor.)

---

## Interaction with always-STL — read before measuring

Always-STL is the dominant standing cost: ldl+stl are ~60-62% of instructions, 87% of stl go to pseudo-registers, 72% are reloaded within 3 instructions, and **every stl closes a T9000 group**.

- **Worthless until always-STL is addressed** (measure them in bytes only, expect ~0 cycles): A8, A10, C3, C4, B7, B8 — everything whose entire thesis is "move an op from stage 4 to stage 2". Stage 2 is measured **100% idle**, but a group that is closed by a spill-store cannot absorb the freed slot. The bound from depth scheduling is 1.6× cycles; these items are cashing a fraction of that fraction. Do them because they are cheap and correct, not because the grouper model says they help.
- **Independent of always-STL, real cycle wins now**: A1, A2, A3, A4, A5, A7, B1, B2 — these delete *instructions and bytes outright*, including the STLs themselves. A7 removes 8 group-closing STLs directly; B10 removes 1368 of them.
- **Reduces always-STL pressure structurally**: B3 (slot renumbering) and B10 (spill-free shift) attack the round-trip itself; B3 is the only item on the list that changes the cost of the round-trip rather than its count.
- **A9 and C1 are correctness prerequisites** for believing any `-mt9000` cycle number at all. Land C1 (and the emulator Unalign check) before quoting `-mt9000` results for A1/A2.