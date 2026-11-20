# CoreMark: dynamic instructions per iteration — 20 November 2026

Goal (from the tp repo's CoreMark/MHz plan, phases P4/P5): fewer executed
instructions per CoreMark iteration from tinycc-transputer, for the
T800-compatible FPGA core in `C:\projects\tp`, which runs the `-mt9000`
binary (it executes `ls`, `lsx`, `ss`, `ssub`, `xsword`, `gtu`, `lbx`).

**Result: `-mt9000` 1,301,505 → 1,027,543 instructions per iteration
(−21.0%); `-mt800` 1,836,982 → 1,609,630 (−12.4%).**  Every build prints
`Correct operation validated` with seedcrc 0xe9f5, crclist 0xe714,
crcmatrix 0x1fd7, crcstate 0x8e3a.

## How it is measured

`transputer/coremark/transputer/cm-measure.sh` (`TCC_DIR=<toolchain>
CPU=t9000|t800 OUT=<dir>`) builds CoreMark twice, with 10 and 30
iterations, and runs both on t4 with `T4_EST_CYCLES=2`, which makes
`ldtimer` return the executed-instruction count: CoreMark's "Total ticks" is
then the instructions in its timed region, and (ticks(30) − ticks(10)) / 20
is exactly one iteration.  The host-clock timer is never used.  Each image
also runs on a counting t4 (`T4_OPHIST`, per-address execution counts; t4
HEAD with `cm-ophist.patch.py` applied) and `cm-mix.py` takes the
difference of the two histograms over 20 iterations: the opcode mix,
functions and straight-line sequences of one iteration, start-up and
report cancelled out.  The two methods agree to 13 instructions in 1.3 M.

An "instruction" is one with its `pfix`/`nfix` prefixes folded in, which is
what the tp RTL's 1.84 M counts: this repo's `-mt800` baseline, 1,836,982,
is the RTL's figure for `test/coremark-t800.btl`.  The `-mt9000` base image
built here differs from the tp repo's `test/coremark-t9000.btl` in one byte
(same size, same code).  Counting every prefix byte as an instruction
instead: `-mt9000` 1,705,957 → 1,376,444 bytes an iteration.

## Baseline (compiler a1951730)

| | `-mt9000` | `-mt800` |
|---|---|---|
| instructions / iteration | 1,301,505 | 1,836,982 |
| with prefix bytes | 1,705,957 | 2,528,381 |
| `ldl`+`stl`+`ldlp` | 40.8% | 39.4% |

`-mt9000` mix: ldl 23.1%, ldlp 10.1%, stl 7.6%, ldc 7.5%, cj 6.6%, lb 5.6%,
eqc 5.1%, bsub 3.3%, ldnl 2.7%, lsx 2.5%, and 2.4%, j 2.4%, sb 2.3%, prod
2.3%.  By function: crcu8 21.9%, core_state_transition 19.8%,
matrix_mul_matrix_bitextract 12.6%, matrix_mul_matrix 9.9%, ee_isdigit
7.8%, core_list_find 7.3%, core_list_reverse 5.4%, matrix_sum 4.2%.

The idioms in the tp profile, here:

- **signed 16-bit load** `ldl p; adc 1; lb; ldc 8; shl; ldl p; lb; or;
  ldc 32768; xword`: 26,838 times an iteration at `-mt800` (268 K
  instructions, 14.6%).  `-mt9000` already loads with `lsx`/`ls`; that
  alone is most of the 1.84 M → 1.30 M difference.  It stays in `-mt800`
  for loads through pointers (there is no `ls` on a T800).
- **`ldiff; rev; eqc 0` unsigned compares**: `-mt800` only (15,237 `ldiff`
  an iteration); `-mt9000` uses `gtu`.
- **`ldl; ldl; prod; ...; bsub` index scaling** `i * N + j`: 21,060 times an
  iteration in the matrix loops, `i * N` recomputed in every inner pass.
- **workspace traffic**: 40.8% of instructions are `ldl`/`stl`/`ldlp`; a
  quarter of the `ldlp` (and most of `lb`/`sb`/`ls`/`ss`) were `char` and
  `short` locals accessed through their address.

## What landed (in order), instructions per iteration

| change | `-mt9000` | Δ | `-mt800` |
|---|---:|---:|---:|
| baseline | 1,301,505 | | 1,836,982 |
| 1. `char`/`short` locals kept as words | 1,228,666 | −72,839 | 1,778,530 |
| 1b. … also when read as their low byte | 1,201,543 | −27,123 | 1,770,690 |
| 2. dead post-increment value deleted | 1,184,303 | −17,240 | |
| 3. block-local copy propagation | 1,158,153 | −26,150 | 1,718,279 |
| 4. `c ? 1 : 0` without branches | 1,131,473 | −26,680 | 1,690,967 |
| 5. value ranges: constant slots, masks, shifts | 1,092,951 | −38,522 | 1,670,886 |
| 5b. `eqc 0; eqc 0` of a 0/1 value | 1,085,111 | −7,840 | |
| 6. loop-invariant arithmetic hoisted | **1,027,543** | −57,568 | **1,609,630** |

Each switched off on the final compiler (`cm-knob`, same images otherwise):

| switch | `-mt9000` | `-mt800` |
|---|---:|---:|
| all on | 1,027,543 | 1,609,630 |
| `TCC_TRANSPUTER_NARROW_LOCAL=0` (1, 1b, 5, 5b) | +166,407 | +111,465 |
| `TP_SCHED_MASK=0x3f7f` (3) | +26,151 | +35,170 |
| `TCC_TRANSPUTER_BOOL_SELECT=0` (4) | +34,520 | +35,152 |
| `TCC_TRANSPUTER_HOIST=0` (6) | +57,568 | +53,416 |

1. **Narrow locals as words** (`tp_narrow_locals`, `transputer-fnopt.h`).
   A `char` or `short` local has a workspace word of its own; when nothing
   else names that word (no LDL/STL, no other use of its address, no
   escaping LDLP whose object reaches it) the word holds the value
   extended to 32 bits: `ldlp s; lb` → `ldl s`, `ldlp s; sb` → `stl s`,
   with `ldc 255; and` / `ldc #ffff; and` / `xbword` / `xsword` only when
   the stored value is not known to be in range.  Ranges come from an
   interval simulation of the straight-line code before the store (byte
   loads, constants, masks, comparisons, shifts of non-negative values,
   `xdble; lshr`).  A parameter's entry copy is truncated (the caller's
   word is not vouched for).  Converted only when it saves instructions,
   counting an access inside k loops 4^k times.  1b lets a `short` be read
   by `lb` as its low byte (`(ee_u8)crc` in crcu8).
2. **Dead post-increment value** (`inc()` in `tccgen.c`,
   `tp_hist_mark_pure`, `tp_stmt_end`).  `p->n++;` as a statement loaded
   the saved value and stored it to a dead slot; the load is now marked
   pure and deleted at the statement's end.  core_state_transition's
   `transition_count[x]++` went from 14 instructions to 9 (6 with 3).
3. **Block-local copy propagation** (`tps_block_copies`, scheduler bit 14,
   plus S6 through an LDNLP).  After `ldl x; stl y` or `ldl x; ldnlp k;
   stl y`, reads of y to the end of the basic block read x (folding k into
   the `ldnl`/`stnl` that consumes it); the copy goes when y is dead.
   Needs no single-definition proof, so it reaches loop temporaries.
4. **Boolean select** (`tp_bool_select`).  The diamond that stores 1 or 0
   on a condition, in any of TCC's layouts, becomes `eqc 0; [eqc 0;] stl
   t`; a truth value tested twice (`gt; eqc 0; eqc 0`) is itself.
   ee_isdigit 26 → 16 instructions.
5. **Ranges** (`tpn_peephole`).  Slots only ever written by `ldc k; stl
   t` (the loop constant pool) and the narrow locals have known ranges;
   `<v ∈ [0,m]>; ldc m; and` and `eqc 0; eqc 0` of a 0/1 value are
   deleted, `xdble; ldc k; lshr` of a non-negative value becomes `ldc k;
   shr`.
6. **Loop-invariant hoisting** (`tp_hoist`, `tph_copies`).  In each loop
   entered only by its head or one `j` before it, straight-line runs that
   compute one value from frame words the loop never writes, with
   operations that cannot trap (prod, bsub, wsub, ssub, sum, diff, and,
   or, xor, shifts, ldnlp, ldlp, ldc), are computed once before the loop
   into a new frame word on top of the locals.  `&C[i*N+j]` and `i*N` leave
   the matrix k loops: matrix_mul_matrix 128,636 → 97,028.

The ISA-OPPORTUNITIES items named for this task had landed before
(2026-08-05 .. 11-03): A3/A4/A5/A6, A8, B1, B3, and the T9000 base ops at
`-mt9000`.  Their worth on CoreMark now, each switched off on the final
compiler (`-mt9000` / `-mt800`): A8 `ldnlp` (`TP_NO_LDNLP`) +1,304 /
+1,299; A4 (`TP_NO_EQC0`) +793 / +792; comparisons against literals
(`TP_NO_CMPFLIP`) +5,696 / +5,696; the `ldiff` unsigned compare
(`TCC_TRANSPUTER_LDIFF_CMP=0`) 0 / +28,485; B3 slot remap
(`TP_NO_SLOTMAP`) 0 instructions (it shortens encodings, not
instruction counts); indexed adds (`TCC_TRANSPUTER_WSUB_OPT=0`) +88,884 /
+56,484; the byte read for shift-and-mask (`TCC_TRANSPUTER_SHIFT_MASK=0`)
+26,305 / +25,939; the scheduler (`TP_NO_SCHED`) +310,880 / +297,260; all
post-passes (`TP_NO_RELAX`) +492,019 / +441,244.

Measured and not taken: inlining `ee_isdigit` (source trial, `static
inline` with a one-line body, through the existing tiny-function inliner)
gives 1,032,194 an iteration on the counting t4, 4,640 more than the
call it replaces (1,027,554 there), now that the callee is 16
instructions.  Jump threading (`j` to `j`) would save about 8 K
but removes `j` timeslice points from loop paths, so it was left alone.

Final `-mt9000` mix: ldl 30.6%, stl 10.0%, ldc 9.5%, cj 7.9%, eqc 6.0%,
bsub 3.7%, and 3.6%, ldnl 3.1%, lsx 2.7%, j 2.5%; `ldlp` 0.9% (was 10.1%),
`lb` 1.2% (5.6%), `sb` 0.01% (2.3%), `prod` 1.6% (2.3%).  By function:
core_state_transition 207,296, crcu8 187,430 (285,531),
matrix_mul_matrix_bitextract 132,020 (163,628), matrix_mul_matrix 97,028
(128,636), core_list_find 95,611, core_list_reverse 69,768, ee_isdigit
62,720 (101,920), matrix_sum 46,128.

## Validation

Every change, and the whole set as committed: CoreMark CRCs above on
`-mt9000` and `-mt800`; c-testsuite 244/244 at `-mt9000`, `-mt800` and
`-mt425` (strict `-s425`); the local suite at `-mt9000` and `-mt800` with
no failure beyond the four that fail at the baseline too (`static_pthread`,
`t4_smp_secondary` ×2, `test_linux7_memset`: compile failures of this
environment); difftest seeds 31001–31150 (narrow locals alone) and
33001–33150 (all), pads 0 and 1027, all pass; Doom's eleven benchmark
phases (`tests/bench/doom.sh`, t800 and t9000) give the baseline's hashes
(palblit 812,508 → 684,508 instructions, the others equal or slightly
fewer); Doom and fbwm compile at `-mt9000` and `-mt800` with no new
failure (142 compiles).  New tests:
`tests/test_narrow_local.c`, `test_block_copy.c`, `test_bool_select.c`;
each fails on a mutation of its pass (no truncation, no kill on a write of
the copy's source, the peephole applied unconditionally).  The local suite
caught two bugs in the first cut of 3 that c-testsuite and 300 difftest
seeds did not: a negative `ldnlp` displacement taken for "no displacement"
(test_vla and eight others), and an FP store into the copy's destination,
which the annotation counts as a use (test_softfloat,
test_double_local_store).

## Binaries for the RTL

`C:\projects\tinycc_transputer\transputer\coremark\transputer\cm-instr\`
(not tracked; also in the side-session notes folder), each with its ELF,
`.elf.tccsym`, `.build.txt` and t4 UART output:

| image | iterations | instructions / iteration (t4) |
|---|---:|---:|
| `coremark-t9000-opt.btl` | 60 | 1,027,420 |
| `coremark-t800-opt.btl` | 40 | 1,609,667 |
| `coremark-t9000-base.btl` | 60 | 1,301,366 |
| `coremark-t800-base.btl` | 40 | 1,837,017 |

The base images are the tp repo's `test/coremark-t9000.btl` and
`coremark-t800.btl` rebuilt here, with the iteration counts raised: at 40
and 30 (the tp images' counts) the optimised images finish in under
CoreMark's 10 s on the RTL's modelled 20 MHz timer and report "Errors
detected".

**On the tp RTL** (MODERNISE, main e62ed76, direct-load harness, modelled
20 MHz timer), these four images, md5s matching, all validated:

| | base | opt | CoreMark/MHz |
|---|---|---|---|
| `-mt9000`, 60 iterations | 15.73 s | 12.44 s | 0.191 → **0.241** (+26.5%) |
| `-mt800`, 40 iterations | 14.74 s | 12.89 s | 0.136 → **0.155** (+14.3%) |

crcfinal 0xa14c (`-mt9000`) and 0x65c5 (`-mt800`), the same for base and
opt.  The cycle gain exceeds the instruction gain (−21%): the
instructions removed were the slower ones.

**Round 2 images** (same folder, same rules), validated on t4:

| image | iterations | md5 | instructions / iteration |
|---|---:|---|---:|
| `coremark-t9000-opt2.btl` | 70 | 06685467… | 895,789 |
| `coremark-t9000-base70.btl` | 70 | 65167aaf… | 1,301,505 |
| `coremark-t800-opt2.btl` | 40 | a051dd3f… | 1,596,542 |

`-mt9000` runs 70 iterations, not 60, to stay above 10 s: by instructions
it should run about as long as the round 1 image's 60.  The base compiler
at 70 iterations is there for a like-for-like ratio.  At 40 iterations
`coremark-t800-base.btl` (round 1) is still the `-mt800` base.

**On the tp RTL** (MODERNISE, tp main 02359ce, md5s matching), all at
least 10 s and validated:

| image | time | iterations/s | CoreMark/MHz |
|---|---:|---:|---:|
| `coremark-t9000-base70.btl` | 18.35 s | 3.814 | 0.191 |
| `coremark-t9000-opt2.btl` | 12.61 s | 5.550 | **0.278** (+45% on base, +15% on round 1) |
| `coremark-t800-opt2.btl` | | | 0.156 (+0.5% on round 1; base 0.136) |

crcfinal 0x3978 for both 70-iteration `-mt9000` images.  The RTL runs at
about 4.0 cycles an instruction, so every instruction removed is a cycle
saved.

## Round 2 (compiler only, no new opcodes)

MODERNISE's grouper costing put the proposed instructions (Step 3 below)
at about 6.7% of groups, so the second round went on cutting the
instruction count further with existing instructions only.  All of the
passes below live in `transputer-fnopt.h`, each with a knob that turns it
off:

| step | `-mt9000` instructions / iteration | change |
|---|---:|---:|
| round 1 result | 1,027,543 | |
| range rules after the scheduler (`tpn_late`) | 975,623 | −51,920 |
| jumps to jumps (`tp_thread_jumps`) | 967,498 | −8,125 |
| the scheduler again after the range rules | 963,578 | −3,920 |
| a flag tested just after it is set (`tp_thread_flags`) | 942,571 | −21,007 |
| tiny leaves inlined at their calls (`tp_inline_leaves`) | 915,131 | −27,440 |
| short blocks copied in place of jumps (`tp_tail_dup`) | 903,180 | −11,951 |
| loop tests `n > j` inverted (`tp_loop_invert`) | **895,789** | −7,391 |

That is −131,754 (−12.8%) on round 1, and −31.2% on the baseline's
1,301,505.  `-mt800` moved only from 1,609,630 to 1,596,542: see the end of
this section.

- **`tpn_late`** (`TCC_TRANSPUTER_RANGE_PEEP`).  The range rules of round 1
  ran before the scheduler, whose deleted round trips are what put their
  shapes side by side, so they now run again after it.  Each slot's range
  is the union of what its stores store, over two rounds.  New rules:
  - a switch's bounds check `ldc 8; ldl state; gtu; cj` goes when `state`
    only ever holds case values (core_state_transition: 4 instructions a
    character);
  - `eqc 1` of a 0/1 value goes;
  - `lo <= x & x <= hi` becomes `ldc hi-lo+1; ldl x; adc -lo; gtu`, with
    `ldc lo; diff` instead of the `adc` when x's range does not rule out
    overflow (T9000: `gtu`);
  - `(a & m) op (b & m)` becomes `(a op b) & m`.
- **`tp_thread_jumps`** (`TCC_TRANSPUTER_THREAD`).  A `j` or `cj` to a `j`
  goes to that jump's target.  It runs after the hoisting, so no new way
  into a loop stops that pass.  Round 1 left this alone for the `j`
  timeslice points it removes from loop paths.  It is taken now because a
  loop whose back edge is a `cj` does not timeslice in any case (CLAUDE.md,
  "loops do not timeslice"), and a `j` that jumps to a `j` still executes
  one.
- **`tp_thread_flags`** (`TCC_TRANSPUTER_FLAGS`).  Some ways into a block
  `L: S; ldl v; cj M` have just stored a constant to v.  Each such way gets
  a copy of S and a jump straight to M or past the `cj`.  The store goes
  too when nothing reads v from there before writing it.  This is
  crcu8's `carry`.
- **`tp_inline_leaves`** (`TCC_TRANSPUTER_INLINE_LEAF`).  It targets a
  direct call to a leaf earlier in the same section whose body is
  `[ajw -N;] ldl N+1; <up to 12 pure stack operations>; [ajw N;] ret`.  The
  call becomes the body: the argument is already in A, and a call leaves B
  and C undefined.  It skips a weak callee, a callee holding a relocation,
  and a call that is a jump target.  The call's relocation is deleted on
  commit.  This is `ee_isdigit` (3,920 calls an iteration); the range
  rules then drop its mask, as the caller's character is known to be
  0–255.
- **`tp_tail_dup`** (`TCC_TRANSPUTER_TAIL_DUP`).  A `j L` to a block of at
  most 6 straight instructions takes a copy of it.  If the block ends in a
  `j`, the copy saves that jump.  If it falls into another block, the copy
  is made only where it puts a `stl x` against the block's `ldl x` for the
  scheduler.
- **`tp_loop_invert`** (`TCC_TRANSPUTER_LOOP_INVERT`).  It handles
  `j T; B: body; T: ldl n; <j>; gt|gtu; eqc 0; cj B`.  The way in gets a
  test of its own, and the bottom test becomes `<j>; ldl n; ldc 1; diff;
  gt|gtu; cj B` (j > n−1).  n−1 cannot wrap there, because the body only
  runs when n > j.  The hoisting then computes n−1 once, so each pass
  round the matrix loops costs one instruction less.

Two bugs found by the gate on the way, and fixed:
- A jump lands on the PFIX 0 padding in front of a local access's
  placeholder, so the flag pass's fall-through copy has to go in front of
  that padding too (`test_reloc_static_data`, `test_reloc_cross_multi`,
  `test_reloc_extern_static`).
- A jump straight to the `j` that a tail copy replaces must run the copy.
  The copy's first record now takes the `j`'s place, and the scheduler's
  offset map names only real records (c-testsuite 09105, 09108, 09112).

`tests/test_range_flags.c` covers each rule at the edges of its ranges,
including both bug shapes.

Measured and not kept: hoisting loads out of loops that store and call
nothing (with volatile accesses tracked from `gv`/`vstore`) found nothing to
hoist in CoreMark.  After the scheduler, core_list_find's
`info->idx` address is split from its `lsx` by a store, so it was taken
out again.

`-mt800`: crcu8 is 357 K of its 1.60 M.  There its `unsigned short crc`
and `unsigned char i` live in memory: `ldlp; sb` twice to store a short,
`ldlp; lb` to read a char, because round 1's narrow-locals pass knows only
T9000's `ss`/`ls`.  Converting a T800 short's two-`sb` store is the
obvious next lever there, along with a `gtu`-free range test.

Validation (all at once, compiler as committed):
- the CoreMark CRCs and "Correct operation validated" at both parts;
- c-testsuite 244/244 at `-mt9000`, `-mt800` and `-mt425`;
- the local suite at `-mt9000` (282 pass) and `-mt800` (266), failing only
  the four compile failures the round 1 compiler shows here too;
- difftest seeds 34001–34150, pads 0 and 1027, all pass;
- objdiff against round 1: 0 return-code mismatches at all three parts;
- Doom's eleven benchmark phases: the baseline's hashes at t800 and t9000.

## Round 3 (compiler only, no new opcodes)

`-mt9000` is the target that matters (the tp core executes the T9000 ops);
the T800 halfword form was cheap, so it is in too.

| step | `-mt9000` | `-mt800` |
|---|---:|---:|
| round 2 result | 895,789 | 1,596,542 |
| T800 halfword locals as words (narrow locals, two-`sb` form) | 895,789 | 1,335,752 (−260,790) |
| T800 range test by `ldiff`, so `ee_isdigit` inlines there too | 895,789 | 1,295,064 (−40,688) |
| late rules: two pushes and `rev` swapped; `!(K > x)` as `x > K-1`; inlining a body that starts `ldc K` | 889,313 (−6,476) | 1,290,972 (−4,092) |
| a char loop counter's mask, bounded by the loop's own test (`tp_induction_mask`) | **879,969** (−9,344) | **1,281,628** (−9,344) |

`-mt9000` is now −1.8% on round 2 and −32.4% on the baseline's
1,301,505.  `-mt800` is −19.7% on round 2 and −30.2% on its baseline's
1,836,982.

- **T800 halfwords.**  Without `ss` or `ls`, `store()` writes a halfword
  as two `sb` through a parked address:
  `ldlp s; stl SCR; ldl v; ldl SCR; sb; ldl v; ldc 8; shr; ldl SCR; adc 1;
  sb`.  Reads are `ldl s; ldc 65535; and`.  `tp_narrow_locals` now knows
  both forms (`TPN_W_SB2`, `n_ldlm`).  The store becomes
  `ldl v; ldc 65535; and; stl s` and the reads lose their mask.  The
  parked `ldlp` had also counted as an escaping address, which kept
  crcu8's `unsigned char i` beside it in memory too; that local is now a
  word as well.  crcu8 at `-mt800` went from 357 K to 141 K.
- **T800 range test.**  `lo <= x & x <= hi` becomes
  `ldc 0; ldl x; adc -lo; ldc N; ldiff; rev`.  `ldiff` leaves the borrow
  of `x - lo - N` in B, and `rev` brings it up.  The inliner takes
  `ldiff` as a pure operation.
- **Late rules.**  `push a; push b; rev` becomes `push b; push a`; this
  is the inlined `ldc 10; rev` meeting the caller's `ldl c`.
  `ldc K; ldl x; [adc c]; gt|gtu; eqc 0` becomes
  `ldl x; [adc c]; ldc K-1; gt|gtu`.  Both run only in `tpn_late`.  Run
  inside narrow locals, before the scheduler, the second took half of
  `ee_isdigit`'s range test and stopped the range test and the inlining
  (942 K, caught by the measurement).  The late rules now repeat while
  they find more.
- **`tp_induction_mask`** handles
  `j T; X: ...; ldl s; adc c; ldc m; and; stl s; ...; T: ldl s; ldc B;
  gt; cj X`.  The body is entered only by the cj, so s <= B there, and
  s >= 0 because every store to s is a constant >= 0 or that increment.
  It also checks that no inner loop repeats the increment without the
  test.  B + c <= m then makes the mask a no-op.

What is left at `-mt9000`:
- core_state_transition, 181 K, about 32 instructions a character: the
  bottom test reloads `*str` that the top reads again (−1 a character),
  and the merge block keeps a copy of a pointer (−2 on 936 a run).
- crcu8, 131 K.
- The matrix kernels, about 300 K together; the next real step there is
  strength reduction of `A[i*N+k]`/`B[k*N+j]` to stepped pointers, worth
  about 3 instructions an inner iteration.
- core_list_find, 95 K, and core_list_reverse, 70 K, which is already
  tight.

Calls are down to about 3,240 an iteration, mostly crcu8 from crcu16
(584), cmp_idx and calc_func through the merge sort.  Inlining any of
those would need a real inliner, not a post-pass one.

Round 3 images (in `cm-instr/` and the notes folder), validated on t4:

| image | iterations | md5 | instructions / iteration |
|---|---:|---|---:|
| `coremark-t9000-opt3.btl` | 70 | d3eb6989… | 879,969 |
| `coremark-t800-opt3.btl` | 50 | cd9024d7… | 1,281,628 |
| `coremark-t800-base50.btl` | 50 | dc06741a… | 1,836,982 |

`-mt800` goes up from 40 to 50 iterations for the 10 s rule, with its base
rebuilt at 50.  `coremark-t9000-base70.btl` (round 2) is still the
`-mt9000` base.  The gate is the same as in round 2: c-testsuite 244/244
at three targets; the local suite unchanged; difftest 35001-35150 pass;
Doom's eleven hashes unchanged at t800 and t9000; objdiff 0
return-code mismatches.

**On the tp RTL** (MODERNISE, tp main 08a5bfa, md5s matching, all
validated and at least 10 s):

| image | time | iterations/s | CoreMark/MHz |
|---|---:|---:|---:|
| `coremark-t9000-opt3.btl` | 12.52 s | 5.590 | **0.280** (+0.7% on round 2) |
| `coremark-t800-base50.btl` | 18.42 s | | 0.136 |
| `coremark-t800-opt3.btl` | 12.71 s | 3.933 | **0.197** (+26% on round 2, +45% on base) |

crcfinal 0x3978 (`-mt9000`) and 0x0158 (`-mt800`), the same as the base
images'.

## Round 4 (`-mt9000`; each step its own image)

| step | `-mt9000` instructions / iteration | image (70 iterations) |
|---|---:|---|
| round 3 result | 879,969 | `coremark-t9000-opt3.btl` |
| 1. strength reduction in loops (`tp_strength_reduce`) | **846,165** (−33,804) | `coremark-t9000-opt4a.btl`, md5 f158866e… |
| 2. the bottom test's load kept for the loop top (`tp_loop_cse`) | **841,525** (−4,640) | `coremark-t9000-opt4b.btl`, md5 1dfbb287… |

**1. `tp_strength_reduce`** (`TCC_TRANSPUTER_STRENGTH`, after
`tp_hoist`).  It works on a loop in the shape `tp_loop_invert` leaves:
entered only by `j B`, left by the bottom `cj B`, and its counter k
updated once, in the block of that cj.  Before that update, a run of pure
arithmetic over k and invariant words, ending in an address op, is affine
in k.  A symbolic stack gives its coefficient: a constant, or a constant
times one invariant word.
- The run becomes `ldl p`.  At the entry, the run itself (k at its first
  value) is stored to p, and `N*a*c` to a step word when the coefficient
  is a word's.  After the update comes `ldl p; adc step; stl p` or
  `ldl p; ldl S; bsub; stl p`.
- Linear test replacement: when a constant-stepped run exists and k is
  read only by its update, the test and the runs, and is dead wherever the
  loop is left, the test `ldl k; ldl H; gt(u)` becomes `ldl p; ldl pH; gt`.
  pH is the run computed with k = H, at the entry.  An address does not
  wrap inside its object, so the signed gt keeps the order.  The update
  of k then goes.
- The new words go on top of the frame, as the hoisting's do (`tpg_room`,
  `tp_hoist_w`).

matrix_mul_matrix's inner loop went from 28 instructions to 20; the
bitextract one likewise.  CoreMark's inner loops are short (9 passes an
entry), so the entry copies cost back about a quarter of that.
`tests/test_range_flags.c` checks a matrix product against another loop
order (N = 0..5), and checks counters read after the loop or beside the
address.  Gate: c-testsuite 244/244 at three targets; the local suite
unchanged; difftest 36001–36150; objdiff 0 return-code mismatches.

**2. `tp_loop_cse`** (`TCC_TRANSPUTER_LOOP_CSE`, after the late range
rules).  It handles a loop top X reached only by the bottom `cj X` that
starts with a short pure run R and `stl t`, where the bottom block
computes the same R and tests it with a `cj`, and nothing between the two
stores anything.  The test then keeps the value (`R; stl t; ldl t; cj`)
and the top's `R; stl t` goes, provided t is dead wherever the test
leaves the loop.  R may contain loads only when the function names
nothing volatile.  For that, `gv` and `vstore` now report volatile
accesses to the backend (`tp_note_volatile`, `func_has_volatile`;
`tccgen.c`, fenced).  In core_state_transition, `*str` was loaded by the
loop test and again by the top, at about 32 instructions a character: one
fewer now.  Tests: the string walk, a character read after the loop, and
a loop that writes memory between the two loads.  Gate as for step 1,
with difftest 37001–37150; Doom's eleven hashes unchanged at t800 and t9000
(with both steps).

Not taken this round:
- **A real inliner** (crcu8 from crcu16, the merge sort's comparators).
  The post-pass names a branch target by its offset in the function being
  compiled, so it cannot copy in a callee with a loop of its own.  This
  needs `tccgen.c` to replay a static callee's tokens at the call, with
  parameters bound and `return` turned into a jump.  The stake is about
  3,240 calls an iteration at about 6 instructions each: around 2%.
- **core_list_find**: `info->idx` is loop-invariant, but after the
  scheduler its address is split from its `lsx` by a store, so the
  hoisting cannot take it (measured in round 2).

## Handoff: compiler track paused after round 4 step 2 (20 November 2026)

CHIEF decided (D37, D45) to defer the inliner and pause here.  The next
large factor is the pipelined core and its grouper (P2/P3).  Resume when
PIPELINE's core runs, by re-measuring the grouping yield of the opt images
below.

**Where it stands** (`-mt9000` instructions per iteration, t4, with
`T4_EST_CYCLES=2`):

| state | `-mt9000` | `-mt800` | commit | image (md5) |
|---|---:|---:|---|---|
| baseline (a1951730) | 1,301,505 | 1,836,982 | | `coremark-t9000-base70.btl` (65167aaf…), `coremark-t800-base50.btl` (dc06741a…) |
| round 1 | 1,027,543 | 1,609,630 | cffd8fd2 | `coremark-t9000-opt.btl` (bd51bc67…, 60 iterations) |
| round 2 | 895,789 | 1,596,542 | 07102019 | `coremark-t9000-opt2.btl` (06685467…) |
| round 3 | 879,969 | 1,281,628 | 41cd891e | `coremark-t9000-opt3.btl` (d3eb6989…), `coremark-t800-opt3.btl` (cd9024d7…) |
| round 4 step 1, strength reduction | 846,165 | | 949166fb | `coremark-t9000-opt4a.btl` (f158866e…) |
| round 4 step 2, loop CSE | **841,525** | 1,276,988 | d6af5126 | `coremark-t9000-opt4b.btl` (1dfbb287…) |

The `-mt9000` images are at 70 iterations unless noted; the `-mt800`
ones at 50.  They are in
`transputer/coremark/transputer/cm-instr/` (untracked) and the side-session
notes folder, each with its ELF, `.elf.tccsym`, build log and t4 UART.
The RTL figures are in tp's `docs/coremark_plan.md` §6: 0.191 → 0.280
CoreMark/MHz at `-mt9000` by round 3, and 0.136 → 0.197 at `-mt800`.

**To re-measure:** `transputer/coremark/transputer/cm-measure.sh`.  It needs
`TCC_DIR` set to a built tree, `CPU` and `OUT`.  The per-instruction
counts need the counting t4: t4 HEAD with `cm-ophist.patch.py` applied
(`OPH_T4`).  To build an image, add `ITER_A=<n> ITER_B=<n+10> NO_OPH=1`
and take `coremark-<cpu>-i<n>.btl`.  Every pass added in rounds 2–4 has
an off-switch knob in `transputer-knobs.h`
(`TCC_TRANSPUTER_RANGE_PEEP`, `_THREAD`, `_FLAGS`, `_INLINE_LEAF`,
`_TAIL_DUP`, `_LOOP_INVERT`, `_STRENGTH`, `_LOOP_CSE`); bisect with them
first.  The regression gate:
- the CRCs and "Correct operation validated" at both parts;
- c-testsuite at `-mt9000`, `-mt800` and `-mt425` (strict `-s425`);
- the local suite at `-mt9000` and `-mt800`;
- difftest, 150 seeds;
- Doom's eleven hashes (`tests/bench/doom.sh`);
- `tests/test_range_flags.c`.

The four local-suite failures every compiler shows here (`static_pthread`,
`t4_smp_secondary` ×2, `test_linux7_memset`) are compile failures of this
environment.

**The core_list_find blocker.**  `transputer/coremark/transputer/
listfind-repro.c` reproduces it:
`while (list && list->info->idx != info->idx) list = list->next;`.  The
code generator computes both operand addresses before either load:

    ldl list; ldnl 1; adc 2; stl t1      (&list->info->idx)
    ldl info; adc 2; stl t2              (&info->idx, invariant)
    ldl t1; lsx; stl t1; ldl t2; lsx; stl t2; ldl t1; ldl t2; diff

The scheduler gets that to

    ldl list; ldnl 1; adc 2; ldl info; adc 2; stl t2; lsx; ldl t2; lsx; diff

The invariant `ldl info; adc 2; lsx` is never contiguous, so `tp_hoist`
cannot take it.  It finds runs of at least 3 pure records, and a run
stops at the `stl`.  Two ways to fix it, either worth about 14 K an
iteration:
- A scheduler rule: `A; B; stl t; L; ldl t; L; op` becomes
  `B; L; A; L; [rev;] op` when A and B are independent pure runs and t
  has no other use.  `diff` under `eqc 0` needs no `rev`.
- Hoist loads, guarded by `func_has_volatile`, which now exists.  A
  round 2 attempt was reverted only because it found nothing in this
  shape.

**The inliner (deferred).**  The post-pass cannot copy in a callee that
has a loop of its own: a branch target is an old byte offset of the
function being compiled.  So crcu8 (from crcu16, 584 calls an iteration),
and cmp_idx/calc_func (called through function pointers in the merge
sort), need a front-end inliner.  The design:
- **Where:** in `tccgen.c`, fenced under TCC_TARGET_TRANSPUTER, at the
  direct call in `unary()`/`gfunc_call` preparation.  The callee must be a
  `static` function defined earlier in the translation unit.  Its body is
  kept as a token string (`tok_str`), as TCC already keeps `inline`
  bodies (`struct InlineFunc` in `tcc_state->inline_fns`, `tccgen.c`).  It qualifies when the address is
  never taken and the body is small (a token budget), with no `alloca`,
  VLA, `goto` out, varargs, or recursion.
- **How:** a statement expression in all but name.  Open a scope; declare
  one local per parameter, assigned from the arguments in order (that is
  where the argument conversions happen); declare a result local of the
  return type; replay the body's tokens with `begin_macro`.  Each
  `return e;` becomes `result = e; goto <end label>`, through a flag the
  `return` parsing checks.  Close the scope and push the result.  Line
  info and `__func__` must keep the callee's names.
- **Indirect calls:** cmp_idx/calc_func reach the merge sort through a
  pointer parameter.  That needs the sort itself specialised per
  comparator, which is out of scope; only direct calls are covered.
- **Expected:** about 3,240 calls an iteration.  Each costs call, two
  `ajw`, `ret` and the argument and result traffic, about 6
  instructions, or about 17 cycles a call on the RTL.  So about 19 K
  instructions (~2%), and about 3% of cycles with the redirects.
- **Risk:** the front end is shared with every program.  It needs a knob
  defaulting off until it has passed the full gate plus Doom, the
  kernel and the fbwm corpus.

**Other measured leftovers:**
- core_state_transition's merge block copies a pointer
  (`ldl 21; stl 4; ldl 4 … ldl 4`): −2 on 936 runs.
- matrix_mul_matrix_bitextract keeps a product in a slot it reads twice;
  `dup`/`rev` would save 1 per inner iteration.
- The `-mt800` halfword loads cost about 11 instructions each without
  `ls`/`lsx`; a `dup` form saves 1 where the stack has room.  For an
  equality test the two sign extensions can go: about 23 K at `-mt800` in
  core_list_find.

**Pushed (20 November 2026):** `main` went to `origin/transputer` as
`c335c128..0ac0cb70`, 104 commits, other sessions' included.

## F1: values kept off the workspace (20 November 2026)

MODERNISE's F1, "workspace register allocation", measured before building
anything.  `coremark/transputer/cm-mine.py` disassembles the
image, attaches T4_OPHIST counts (the i30 − i10 difference / 20 for
CoreMark, the 20,000-loop run / 20,000 for Dhrystone) and classifies every
executed `ldl x` by what precedes it, with x's liveness followed through
the function's branches.  Baseline is `f9f2b3ba`, word-aligned string
literals included: CoreMark `-mt9000` 841,525 instructions an iteration,
`-mt800` 1,276,988; Dhrystone 2.1 (`examples/dhrystone`, the port's byte
loops) 1436 per Dhrystone at `-mt9000`, 1751 at `-mt805`.

| shape, CoreMark `-mt9000` per iteration | count | removable |
|---|---:|---:|
| `stl x; ldl x`, x dead after | 517 pairs | 1,034 (calc_func) |
| `stl x; ldl x`, x live after | 16,293 | 0: `dup; stl x` is the same count |
| `stl x; <1-3>; ldl x`, x dead after | 6,985 | most: core_list_find's park |
| `ldl x; ldl x` | 680 | 0: already `dup` where cheaper (`tps_apply_dup_loads`) |
| `ldl x; <1-3>; ldl x`, no write between | 24,597 | about 0: `*p += v` and loop counters, where keeping the value costs a `dup` and a `rev` |

The scheduler's S1/S4 already delete the dead adjacent pairs: the
"1,986 an iteration" counted on the RTL's trace includes pairs a branch
lands between, where the reload is a loop head and the value is not on the
stack.  Three rules were worth building, plus one that changes no count:

| rule (knob, `=0` turns it off) | CM `-mt9000` | CM `-mt800` | Dhry `-mt9000` | Dhry `-mt805` |
|---|---:|---:|---:|---:|
| S13 park-unary (`TCC_TRANSPUTER_PARK_UNARY`) | −6,265 | 0 | −8 | −8 |
| S14 post-increment sink (`TCC_TRANSPUTER_POSTINC_SINK`) | 0 | 0 | −62 | −62 |
| sign extension drop (`TCC_TRANSPUTER_EXT_DROP`) | −195 | −677 | −31 | −102 |
| all three | 841,525 → 835,065 (−0.77%) | 1,276,988 → 1,276,255 | 1436 → 1335 (−7.0%) | 1751 → 1579 (−9.8%) |
| `dup; stl` (`TCC_TRANSPUTER_DUP_STL=1`, off) | 0 | −56 | 0 | 0 |

- **S13**, `Q; stl a; U; ldl a -> U; Q` (a dead after): Q pushes one
  value over v and U works on v alone, so U can run first.
  core_list_find and cmp_idx compare two halfwords:
  `adc 2; ldl 12; adc 2; stl 10; lsx; ldl 10; lsx; diff` becomes
  `adc 2; lsx; ldl 12; adc 2; lsx; diff`.  Neither run stores, calls,
  branches or reads below itself; `tps_permute` rewrites the records in
  place, so no branch target moves.  At `-mt800` the halfword load is a
  longer form this does not match.
- **S14**, `ldl d; stl t; ldl d; M; stl d; R; ldl t ->
  R; ldl d; ldl d; M; stl d` (t dead after): `*d++ = <R>` reads the old d
  where it is used and updates d there.  R must not name d or t, write
  them through an LDLP, or call while either is a stacked argument; d's
  address must not escape; the update needs two stack places over R's
  result.  Dhrystone's strcpy byte loop, 17 → 15 instructions a byte.
- **Extension drop** (`tpn_peephole`, with the range rules): `xbword`,
  `xsword`, or T800's `ldc 128; xword` / `ldc 32768; xword`, goes when
  the value is already in the signed range, when only `eqc 0` or `cj`
  reads it and it is in [-128, 255], or when only a byte (halfword) store
  reads it.  strcpy's `ldl c; xbword; eqc 0` on an `lbx`-loaded byte.
- **`dup; stl x`** for a live `stl x; ldl x`, with the stack depth at
  most 2 and not on the T414: the same instruction count, so off by
  default; on for an RTL A/B, where it avoids reading back a word just
  written.

Gate: CoreMark CRCs (crcfinal `0x3978` at 70 iterations), Dhrystone PASS
at both parts, c-testsuite and local suite (`test_f1_park.c`, all four
rules, both ways of each, at `-mt9000`, `-mt800`, `-mt425`), difftest
41001-41150, Doom bench hashes unchanged at `-mt800` and `-mt9000`
(span −5.2%, mapplane −5.1% instructions).

**Step 4, a real allocator for short-lived locals: not worth it on these
two.**  After S13/S14 what stays in the workspace is mostly (a) values
read again after a store, live past it (16 K an iteration, where keeping
them on the stack is `dup` instead of `ldl`, the same count), and (b)
read-modify-writes through a pointer, `ldl p; ldnl 0; <v>; op; ldl p;
stnl 0`, where holding p costs `dup` and `rev` (+1).  The three-deep stack
cannot hold a temporary across more than one other live value, and a
CoreMark iteration's remaining `ldl`/`stl` are 32.6% and 11.7% of its
instructions mostly because its loops carry three or more live words.
The −31% CoreMark needs is not in round trips.

## `-mcrc-idiom`: bit-serial CRC loops as `crcbyte` (20 November 2026)

crcu8 (core_util.c) was 15.6% of an iteration's instructions: 584 calls
of 225.  It feeds a byte into a reflected CRC-16 one bit at a time, which
is what `crcbyte` does a byte at a time, the other way round:

    crc' = bitrevword(crcbyte(A = 0, B = bitrevword(crc ^ data), C = gen))

gen is the W-bit polynomial reflected back to normal order at the top of
the word (P = #A001, W = 16: #80050000).  MODERNISE checked it against
the ISM's crcbyte for all 2^24 (data, crc) pairs; it holds for W from 8
to 32 (host check: 8, 16, 24 and 32 with six polynomials).

CoreMark's rules allow toolchain changes, not source changes, and all
files built with one set of flags, so this is a general pass behind a
flag that the build line shows (`EXTRA_CFLAGS=-mcrc-idiom`), not keyed
to any name.  **How it recognises a loop** (`tps_crc_lower`): a region
from a point with an empty stack through a loop's back edge is executed
symbolically.  Every value is 32 bits, each an affine function over
GF(2) of the input bits (the words the region reads before writing), so
xor, constant shifts and masks, sign extensions, the narrow reads
`ldlp x; lb`/`ls`, and `eqc 0/1` of a truth value stay exact; the loop
counter is a constant all the way (`adc`, `gt`, `gtu`, `ldiff` on
constants).  A branch on a one-bit condition runs both ways and the two
paths must meet with every value differing by a constant, which keeps
the merge affine.  At the exit exactly one written word may be live, and
its value must equal T(xor of the inputs, each masked to its low w
bits), T the 8-step reflected CRC of some P: P is read off the result
(the response to an input's bit 7) and every input bit's response is
compared with T's.  That is the whole function of the region, so the
replacement is exact for all inputs.  Memory, calls, a variable add or
compare, an 8-iteration count that is not 8, nested conditions: not one.

Lowered in `tests/test_crc_idiom.c` at `-mt9000`, `-mt800` and `-mt425`:
CoreMark's form, CRC-16/ARC (byte xored in first), reflected CCITT #8408
as a ternary, CRC-8/MAXIM, CRC-32, xor after the shift.  Not lowered
(and still right): 7 rounds, a left shift, an arithmetic shift, a test
of bit 1.  All checked against a reference over every data byte and 64
checksums.  `TP_NARROW_STATS=1` prints a `[CRC]` line for each loop
lowered; `TCC_TRANSPUTER_CRC_IDIOM=1`/`0` overrides the flag.  Not on
the T414 (no crcbyte, no bitrevword).

crcu8 becomes `ldl data; ldc 255; and; ldl crc; ldc 65535; and; xor;
bitrevword; ldc gen; rev; ldc 0; crcbyte; bitrevword` (the masks are the
code's own: the parameters' high bits must not reach the result).

## The hot-loop rows (20 November 2026)

MODERNISE's opt4b hot-loop census named four more rows.  Baseline for
them is `fa1b0975` (F1, the range-window fix, `-mcrc-idiom` off):
835,065 instructions an iteration at `-mt9000`, 1,276,255 at `-mt800`.

**Row 1, core_list_find: a loop-invariant load hoisted**
(`TCC_TRANSPUTER_HOIST_LOAD`).  After F1 a node of the idx loop is 15
instructions; 3 of them, `ldl 12; adc 2; lsx`, read `info->idx` again on
every trip.  tp_hoist now also moves runs that read memory (`ldnl`,
`lb`/`lbx`/`ls`/`lsx`, and the `adc` that forms the address) when the
loop stores nothing, calls nothing and the function names no volatile -
and only where the same word was already read on the straight way into
the loop (`tph_twin`): a hoisted read happens even when the loop runs no
times, and a read the program never made could trap under T9000 memory
protection.  core_list_find tests `info->idx >= 0` just before the loop,
so the idx loop qualifies; the data16 loop is entered by a `cj` from
outside and keeps its load.  −5,436 an iteration at `-mt9000` (835,065 →
829,629), 0 at `-mt800`.  The branch-sense half of the row (`diff; eqc
0; cj` → `diff; cj`) moves the `eqc 0` to the null test instead: no
instruction fewer, not done.  `test_hoist_load.c`: the hoisted search,
and a store, a call, a volatile and a loop with no earlier read, none of
which hoists.

## Step 3: instructions the compiler would use

Measured on the final `-mt9000` image: how many instructions per iteration
each candidate would remove if every matching straight-line sequence
became one instruction (sequences never cross a `j` or a call; a sequence
counts as often as its least executed member).  The rows overlap; the last
line covers the code with all of them at once, choosing the best
non-overlapping set (`cm-mix.py --idioms`).  Opcode numbers are
MODERNISE's to allocate (`docs/t2026_isa_ext.md`); the names are
placeholders.

| proposal | replaces | per iteration | % |
|---|---|---:|---:|
| `opl` A = A op local (add/sub/and/or/xor/mul/gt/gtu with a workspace operand) | `ldl b; op` | 55,076 | 5.4% |
| `incl` add a constant to a local | `ldl x; adc k; stl x`, `ldl x; ldc k; bsub; stl x` | 41,074 | 4.0% |
| `stlk` store to a local, keep A | `stl x; ldl x` | 34,405 | 3.3% |
| `cjnz` jump if A ≠ 0 | `eqc 0; cj` | 31,690 | 3.1% |
| `andc` and with a constant | `ldc m; and` | 30,342 | 3.0% |
| `ldlnl` load at local pointer + k | `ldl x; ldnl k` | 30,118 | 2.9% |
| `op3` c = a op b on workspace words | `ldl a; ldl b; op; stl c` | 28,351 | 2.8% |
| `cjgt`/`cjgtu` compare and jump on false | `gt|gtu; eqc 0; cj` | 24,072 | 2.3% |
| `cjeqc` jump unless A = k | `eqc k; cj` | 19,192 | 1.9% |
| `stlnl` store at local pointer + k | `ldl x; stnl k` | 17,601 | 1.7% |
| `ldxi` indexed load | `ssub; lsx`, `wsub; ldnl 0`, `bsub; lb` | 13,996 | 1.4% |
| `adcu` add a constant, unchecked | `ldc k; bsub` | 12,945 | 1.3% |
| `cjgt`/`cjgtu` compare and jump on true | `gt|gtu; cj` | 11,746 | 1.1% |
| **all together, no overlap** | | **294,238** | **28.6%** |

So the compiler-facing set would take the `-mt9000` image from 1,027,543
to about 733,000 instructions per iteration (incl 41 K, ldlnl 30 K, andc
30 K, op3 28 K, opl 28 K, stlk 26 K, cjgt(u) 35 K, cjnz 20 K, cjeqc 19 K,
stlnl 18 K, ldxi 14 K, adcu 3 K in that cover).  The plan's ~0.55 M for
CoreMark/MHz 3 needs a further ~0.18 M: what is left is mostly the
workspace round trips of TinyCC's always-STL model (`stl` is still 10% of
instructions) and the switch dispatch in core_state_transition (11
instructions a character through a jump table).

The compare-and-branch forms matter beyond their count: `adc` traps on
signed overflow, so unsigned counters step with `ldc 1; bsub` (2
instructions) and every loop test is `ldl; ldl; gt(u); eqc 0; cj` (5),
because `cj` jumps on zero.

## Files

- `transputer-fnopt.h`: `tp_narrow_locals`, `tpn_*`, `tpn_peephole`,
  `tp_bool_select`, `tp_hoist`, `tph_copies`, `tps_block_copies`, S6
  through an LDNLP; `tp_relax_function` runs them.
- `transputer-hist.h`, `transputer-fp.h`, `tccgen.c` (fenced), `tcc.h`:
  the dead post-increment value.
- Round 2, `transputer-fnopt.h`: `tpn_late` (and the new `tpn_peephole`
  rules), `tp_thread_jumps`, `tp_thread_flags`, `tp_inline_leaves`,
  `tp_tail_dup`, `tp_loop_invert`, `tpf_insert`/`tpx_insert`; knobs
  `TCC_TRANSPUTER_RANGE_PEEP`, `_THREAD`, `_FLAGS`, `_INLINE_LEAF`,
  `_TAIL_DUP`, `_LOOP_INVERT`; `tests/test_range_flags.c`.
- `transputer-knobs.h`: `TCC_TRANSPUTER_NARROW_LOCAL`,
  `TCC_TRANSPUTER_BOOL_SELECT`, `TCC_TRANSPUTER_HOIST`, `TP_NARROW_STATS`,
  `TP_HOIST_STATS`; `TP_SCHED_MASK` default 0x7f7f (bit 14).
- `transputer/coremark/transputer/cm-measure.sh`, `cm-mix.py`,
  `cm-ophist.patch.py`: the measurement.
