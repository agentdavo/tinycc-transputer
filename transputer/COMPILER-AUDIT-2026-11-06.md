# Full backend audit — 6 November 2026

Three read-only agents read every line of a frozen `transputer-gen.c`, then
cross-checked findings against live source and the existing sessions' lessons.
This report separates reproduced defects, internal-pass counterexamples,
unmeasured performance candidates and maintenance work. It is not a claim that
all proposed changes are safe or profitable.

## Coverage and evidence

| Agent | Inclusive snapshot lines | Main areas |
| --- | --- | --- |
| `audit_gen_front` | 1–4700 | Definitions, descriptors, emission, loads/stores, conversions, loop constants |
| `audit_gen_middle` | 4701–9400 | Intrinsics, memory inlining, scheduling, liveness, workspace layout, relaxation |
| `audit_gen_back` | 9401–14025 | Calls/returns, value pushes, pointer arithmetic, integer/64-bit operations, FPU optimizers |

Snapshot HEAD: `5c43b2bcb3a591e580e8a94c16186e66db82f8fb`.
Source SHA-256:
`485c5b4c1b2e7ca676a4174a8545ff5f200f34f9c5911c0e4eb4182d5f092058`.
Line references below refer to that snapshot; use function names when locating
current code because concurrent commits move lines. The snapshot, scratch
reproducers and captures are in the ignored `logs/compiler-audit-2026-11-06/`
directory. Agents did not edit shared compiler sources or tests.

## First: correctness

### 1. Packed memcpy length can destroy the source address — reproduced C defect

`tp_push_sv_is_pure` (9896–9914) accepts misaligned local word lvalues and
`VT_UNALIGNED` word lvalues. Their actual materialization in `tp_push_sv`
(10605–10610, 10672–10676) uses `tp_park_new` / `tp_bytes_load`, requiring two
free hardware-stack positions. The memcpy inliner (6180 area) assumes a pure
push needs only one. Source and destination already occupy C/B when a packed
length needs another temporary, so the source is lost.

The legal reproducer uses a packed `{ char pad; unsigned n; }`, sets `n=5`,
and calls `memcpy(dst, src, p.n)` on distinct static buffers. It reports
`PACKEDCALL_BAD` on strict T414 and T800/T9000-target emulator runs, with Error
clear. Replacing only the count with an aligned parameter reports
`PACKEDCALL_OK` on T800. See
[reproducer](../logs/compiler-audit-2026-11-06/back/packed_call.c).

The review session subsequently committed the conservative classification fix
and regressions as **`0fd07e87`**. The pre-fix compiler fails the source test;
the fixed compiler passes on T414, T425, T800 and T9000 against a host oracle,
including packed pointer arguments, volatile fields and direct/indirect calls.
A code-generation check preserves inline MOVE for aligned counts and uses the
safe call path for packed counts. Check all consumers of the predicate, not
only memcpy, when extending this model.
Longer term, describe maximum stack demand and scratch effects explicitly
instead of relying on one loosely defined purity bit.

### 2. Copy propagation loses an indirect reader — internal-pass counterexample

The real `tp_sched_function` accepts this decoded stream with a 12-word frame,
no outgoing arguments/inline assembly, and optional patterns disabled:

```text
AJW -12; LDL 14; STL 9; LDLP 9; LDNL 0; STL 7; AJW 12; RET
```

It deletes `LDL 14; STL 9` while leaving the indirect read `LDLP 9; LDNL 0`.
Scheduling annotations recognize that read (7108–7110, 7157–7168), but the
copy-propagation checks/rewrite (7463–7500) redirect only direct `LDL` readers
and do not veto this one. The read loses its reaching definition.

This is **not yet a demonstrated C-source miscompile**: frontend folding and
address-escape restrictions may prevent this exact emitter shape. The
raytracer session committed a conservative guard and host regression as
**`5a5ef711`**, covering direct, displaced and wrapping indirect reads and a
non-aliasing control. The coordinator reran the committed host test and it
passed. The owner also reports scheduler/address/frame checks on T414, T425,
T800 and T9000, all four raytracer images on one/four nodes, and model 3 on
16/64 nodes. `TransLocal` remained at 339 modeled cycles per call; this is a
correctness fix rather than a measured performance improvement.
[Harness](../logs/compiler-audit-2026-11-06/middle-copyprop-probe.c) ·
[preconditions/invocation](../logs/compiler-audit-2026-11-06/middle-copyprop-probe.txt) ·
[exact output](../logs/compiler-audit-2026-11-06/middle-copyprop-probe-output.txt).

### 3. Fuzz seed 329: scalar64 argument spill alignment — fixed

The whole-struct experiment exposed a pre-existing failure even when the
reduced caller made no aggregate calls. The lead and two read-only reviewers
traced it to `tp_expand_scalar_call_words`: reserving eight bytes from a
non-word-aligned `loc` caused word-addressed stores to round down, then
byte-accurate temporary reloads to read two bytes into the stored halves.
The 64-bit shift count was correct; its operand was already corrupted before
`__ashrdi3`. Unrelated function layout changed the wrong result.

**`fc344c0b`** aligns the reservation before deriving either half's address.
Original fuzz seeds 329 and 367, both failing before, now agree with host
UBSan/O2 oracles. The reduced source regression passes six targets at link
pads 0, 1027 and 4096; three full C suites pass 244 tests each. A separate
reviewer rebuilt exact revision `fc344c0b` and confirmed the original seed
against its preserved oracle on strict T414/T425, T800 and T9000. The
verification hashes and results are committed in `05b0cc9f`.
See [c-testsuite regression](c-testsuite/tests/single-exec/09118.c) and
[scalar argument tests](tests/test_scalar_arg_alignment.c).

The separate aggregate-copy optimization landed as `c96e1bcd`, including
volatile-source/member fallback. Its guarded trial passes 400 fuzz seeds at
two link pads and the three workload checks. Exact integration-commit versus
parent acceptance was repeated after rebasing; `b0e8dc23` records matching
source hashes and pinned inputs in the
[tracked report](tests/bench/struct-call-acceptance-2026-11-06.md).

## Next performance experiments

The table records the original audit candidates; subsequent outcomes are
listed below and in the [coordination record](COMPILER-COORDINATION.md).
Measure instruction/encoding changes first, then fixed-work guest cycles,
hashes/CRCs and frame cost against a matching baseline.

| Priority | Candidate and snapshot anchors | Intended benefit | Required limits and checks |
| --- | --- | --- | --- |
| 1 | `gen_cvt_csti` 4378–4402: reuse `tp_push_reg_or_cancel_stl` 527–557 | Avoid a fresh `gv` spill/reload before byte/halfword narrowing; especially CoreMark `crcu8` | Shared vstack aliases, signed/unsigned boundaries, pending call results, volatile/packed inputs, no-code contexts; all targets |
| 2 | `tp_expand_struct_call_words` 9675–9765 | One whole-object MOVE instead of zero/store/copy per word; fewer setups for kernel/physics aggregates | Keep empty-struct ABI, partial-tail padding and rvalue fallback; sizes 0–17, large/packed aggregates, nested calls and hidden return pointer |
| 3 | `gen_cvt_itof` 4174–4197 | Feed FPU conversion directly from existing aligned 32-bit source storage, avoiding reload and scratch store | Preserve float-result eviction; start with proven workspace/aligned local words; keep widening/packed/constant fallbacks and single-read semantics |
| 4 | `gen_opl_shift` 12382–12469 | Directly stage independent high/low/count operands for native LSHL/LSHR | Reserve result slots first; preserve count/overshift convention and complex fallback; live pairs, scratch aliases, counts 31/32/63/64 |
| 5 | `tp_slot_remap` 8427–8455 and `tp_gaddr_cache` 8719–8766 | Share loop weights and optimize weighted encoding cost, not static access density alone | Preserve aggregate units, implicit slots, actual stacked arguments, one-past references and frame-prefix thresholds |
| 6 | `tp_pool_note_ldc` 2131–2164 / `tp_loop_pool_close` 2211–2242 | Validate surviving uses before initialization; avoid dead/unprofitable entries and first-six admission bias | Dominance, zero-trip loops, goto/Duff guards, VLA/loc rewind and patch ordering; test six rare constants followed by a hot seventh |
| 7 | `gen_ptradd_wsubdb` 11001 area | Constant eight-byte index can use `ldnlp (2*index)` after pointer load | Retain frontend constant folding and T414 fallback; positive/negative indices, 32-bit scaling semantics, register pressure |

The CoreMark session has been assigned the narrowing-cast experiment using
its new [fixed-seed baseline](tests/bench/coremark-baseline-2026-11-06.md).
The completed trial found 16 cast-time cancellations per CoreMark build, but
the normal scheduler already removes those pairs. Final executables and
estimated cycles were identical on four targets, with matching CRCs; the
change was reverted. Six-target cast/alias/volatile/packed/no-code probes
passed. The rejected trial is recorded in `a361932b` and
[its report](tests/bench/coremark-cast-trial-2026-11-06.md).
Backend shift staging is distinct from the already-landed runtime helper
improvement `8e88385c`.

Subsequent check-in outcomes:

* Direct int-to-FPU storage conversion is integrated as `16e7e460`, with
  six-target probes and a matched Doom/CoreMark/raytracer comparison. Doom and
  CoreMark were unchanged; the reference-equal raytracer saved 781,344 modeled
  T800 cycles (about 0.021%). See the
  [full report](tests/bench/itof-direct-2026-11-06.md).
* The backend shift-staging trial also produced no application-level gain on
  the three requested workloads and remains unmerged. `24e467f2` retains its
  boundary regression and review evidence.
* Whole-struct MOVE exposed the scalar64 temporary-alignment defect described
  above. Its repair `fc344c0b` is separate from the aggregate optimization;
  passing workload images alone did not clear that correctness gate.
* Symbol-address emission is consolidated in `3628b7e0`; shared instruction
  effects have a separate design in `51c35b4c`; four-argument TVX accumulator
  staging is consolidated in `265309e9`; word-result builtin dispatch is
  table-driven in `3783a69a`. These are equivalence/maintenance
  results, not new guest-performance claims.
* The dead pool-initializer trial is rejected in `c6de60f9`: the production
  scheduler already removes those initializations, leaving all three workload
  ELFs identical. Surviving-reference and admission profitability remain open.
* The rebased whole-struct trial matches workload outputs and repaired seeds,
  and `c96e1bcd` preserves source volatility across argument casting before
  choosing whole-object or original per-word staging. The coordinator also
  passed the committed code-generation guard in an independent source build.
* `bef52a24` shares workspace overlap arithmetic in the integer/FPU history
  predicates while preserving caller barriers. `cabe1fbf` hardens the Doom
  benchmark against unresolved paths and invalid results; changing that harness
  shifts some layout-dependent counts, so use matched harnesses for comparisons.

## Common code worth consolidating

1. **Instruction and operand effects.** Stack requirements, local footprints,
   alias/escape information and FPU effects recur in `tps_effect`,
   `tps_word_escapes`, `tps_dead_after`, scheduling annotations, slot remapping,
   `tpg_ldlp_extent`, `tp_fpu_effect`, `tp_fpu_pure` and related predicates.
   Share small explicit descriptions, retaining separate memory/device,
   descheduling, trap and hidden-state properties. Unknown instructions must
   remain barriers. The two correctness findings show why metadata drift
   matters; a broad new "pure opcode" flag would repeat the problem.
2. **Relocatable symbol addresses.** `tp_push_sym_addr` 10740–10751 already
   emits the relocation, seven PFIX bytes, LDC and addend. The same sequence
   occurs in `tp_push_sv`, `load`, `tp_emit_float_addr` and `gen_store_addr`.
   Share raw emission while keeping cache policy, tracing and stack accounting
   explicit. Preserve VT_SYM provenance, addends, history barriers and no-code
   behavior. Require byte/relocation-equivalent objects on six targets.
3. **Result-producing intrinsics.** Remaining `disc`/`dist`/`diss`/`resetch`
   wrappers repeat staging/finalization. Four-argument accumulating operations
   such as `bf16dot`, `mxfp4gemv`, `mxfp4gemvbf` and `tp_gen_q8dot` repeat
   accumulator/pointer/count handling. Parameterize only regular shapes;
   unusual stack effects and software fallbacks remain custom. The no-argument
   result group already landed during this audit as `60f7d61e`.
4. **Scaled pointer lowering and rollback.** The four pointer-scale fast paths
   repeat operand/result setup. FPU rewrites also repeat history/local-patch/
   cache rollback and replay. Specify the differing state contracts before
   merging helpers; do not erase intentional differences.
5. **Compiler throughput.** Copy propagation repeatedly scans a function and
   `tps_dead_after` allocates traversal arrays per query. Reusable storage and
   reader indexes may help large kernel functions, but must update after each
   rewrite to retain existing copy-chain folding. Measure compiler time/memory
   separately from generated-program performance.

These are initially maintenance/equivalence projects, not claimed guest-speed
gains. Do not remove `gen_opf` fallback emission as apparently unreachable:
bounds-related flags can exclude operands from the earlier fast path.

## What the existing sessions learned most

| Session | Evidence and lesson carried into this audit |
| --- | --- |
| Compare raytracer binaries | `e0cf42e0` removed three REV executions per `TransLocal` call and saved 5,175,852 modeled T800 cycles. Interior targets, relocations, ABC depth and inline assembly constrain late scheduling. An 8→12/16 cache trial saved only about 0.012% while adding workspace and was rejected. |
| Refactor transputer code layout | Explicit availability metadata `6d83e718` matched 2,560 object/diagnostic cases plus 48 malformed calls. Assembler constants `a4fe457b` matched all 326 standalone mnemonics and 1,956 integrated mnemonic/target cases. Shared tables must preserve operand permutations, negative secondaries and diagnostic timing. |
| Build per-part uClibc | `964fbf96` pins actual implicit stack arguments rather than every temporary below a high-water mark. Integrated Doom span cost is 4,413,304 estimated T800 cycles with matching hashes and a passing 243-test C suite. Explicitly accessed temporaries can move; implicit call slots and slot zero require protection. |
| Optimize 64-bit division (Linux work) | The glyph SHR/SHL fold became visible after scheduling; `f75000be`, merged at `32afb1db`, preserves interior-target/relocation barriers. Nine-boot A/B reported 581,818 fewer estimated cycles. Suggested next pattern: duplicate pointer loads around `*p++`. |
| Explain per-architecture libtcc1 builds | T414 Linux userspace overlapped the fixed syscall gate; final ELF lacked relocation provenance. `cce8054e` honors explicit placement and rejects overlap rather than exempting a coincident address. All six libc builds then passed on T9000 Linux. CoreMark CRCs passed on four target models; samples put `crcu8` first. |
| Review and implement afcc4dfe | Direct-word/native runtime helpers removed expensive struct-return/call overhead; host differential checks and target tests are essential for boundaries. MOVE2D correctness is tested in `1096b09`, but transfer timing is still missing from the estimator; its setup cost is not the full operation cost. |

## Deferred or explicitly unproven

* `1dbe953a` quantifies INT64→REAL32 nearest-even differences with 350 exact
  integer-derived vectors. All emitted results remain one of the two adjacent
  binary32 values, consistent with the documented C latitude; this does not
  establish an unconditional C violation. TCWG's `fpunoround` recipe is for
  round toward minus infinity, not a drop-in nearest-even replacement. Its
  literal sequence also differs from the directed oracle on T4, requiring
  separate instruction/representation validation before changing lowering.
  See [investigation](tests/bench/int64-real32-investigation-2026-11-06.md).
* `tp_gaddr_cache` changes frame bookkeeping before later relaxation bailouts.
  The audit did not establish emitted-frame corruption; transactional state
  cleanup remains a lower-priority question.
* Broader memset/fill lowering needs a costed design, preserving overlap,
  volatility, device access and return semantics. MOVE2DZERO is not memset.
* Do not re-propose existing mint selection, narrow stores, indexed FPU loads,
  explicit builtin CPU categories, indexed-copy scheduling or the call-staging
  remapper as missing features.

## Delivery order

The packed-load and scheduler guards are landed. Use those commits as the
baseline, then take one contained performance experiment per owner, following
the [current ownership and three-workload gate](COMPILER-COORDINATION.md).
Refactors should preserve objects/diagnostics before
changing instruction selection. Record a separate validated commit for each
group; rejected trials should not be merged merely to keep commits frequent.
See [coordination](COMPILER-COORDINATION.md) for ownership and
[protection review](LINUX-PROTECTION-REVIEW-2026-11-06.md) for the separate
trap-syscall/isolation blockers.

## Legacy math and renderer reference audit — 6 November 2026

The chief read the Whetstone and Savage sources/build descriptions, ran their
supplied boot images, captured executed instruction profiles, and inspected
POV-Ray's Transputer platform layer and configured BTL. This is reference
evidence for future compiler work; no backend optimization was changed here.
The six worker chats remain archived.

### Whetstone: use the individual modules and preserve their actual semantics

The [wrapper](t4/examples/whetston8/whetston.occ) calls the
[REAL32 kernel](t4/examples/whetston8/whetr.occ) or
[REAL64 kernel](t4/examples/whetston8/whetd.occ) with 10 then 20 repetitions
and subtracts cumulative timings. Modules cover scalar/array arithmetic,
array arguments, branches, integer-to-FP conversion, trig, calls, array
references and log/exp/sqrt. However its count vector is
`[0,12,14,345,0,210,32,899,616,0,93]`: modules 1 and 10 have zero iterations,
and module 5 is omitted. This exact image cannot measure those absent loops.

The zero-interrupt option halts: the high-priority branch contains only
`IF delay <> 0`, with no true fallback when delay is zero. On T4 `2853b29`,
the single-precision image executes `SETERR` at `0x800005ac` for argument 0.
Argument 10 completes both precisions. Its high-priority timer process and
two-run subtraction make the displayed score sensitive to scheduling and
timer mode; ordinary wall-clock samples even produced a negative individual
module difference. Do not use the printed KWhetstones or modeled-clock
substitution as a calibrated CPU comparison.

Both kernels' second trig update computes `COS(y)` into `r3` but uses `r2`
in the final numerator. That differs from the adjacent formula comment.
Keep the exact supplied source as a legacy case; a corrected/textbook
variant needs a separate identity and expected results. Module 9 also has
sequentially dependent stores: the final `e1[l] := e1[j]` reads the value
written by the first store. It is not a simultaneous three-element rotation.

Executed BTL profiles confirm:

* REAL32 procedure `p3` at `0x80000a8b` runs 26,970 times (899 × 30). Its
  arithmetic uses `fpldnlsn`, `fpldnladdsn`, `fpldnlmulsn`, `fpdiv`, explicit
  stores and `fpchkerr`; the checked-error operations cannot be discarded as
  ordinary staging noise.
* The array-argument procedure's inner loop runs 2,520 times; the indexed
  array-reference procedure runs 18,480 times. REAL64 uses `wsubdb` for
  double-word indexing; the REAL32 path uses `wsub`.
* The sqrt stage executes 2,790 first/last operations, with 5,580 steps for
  REAL32 and 13,950 for REAL64: two versus five steps, as prescribed for T800.
  TinyCC already emits these sequences and fused memory arithmetic; they
  are regression requirements, not missing features.

### Savage: repeated numerical composition, not just a throughput loop

[Savage C](t4/examples/savage/savage.c) and
[REAL64 Occam](t4/examples/savage/savage.occ) perform 2,499 iterations of:

```c
a = tan(atan(exp(log(sqrt(a*a))))) + 1.0;
```

For positive exact arithmetic the composition returns `a`, so starting at 1
gives an ideal final value of 2500. The program prints the accumulated
difference; it does not enforce a passing tolerance. It stresses math-library
accuracy, rounding and cancellation as well as FP/call/workspace costs.
Replacing the composition by `a + 1` would destroy this test's purpose and
strict numerical behavior.

| Supplied image / emulator | Observed result | Executed instructions |
| --- | --- | ---: |
| `savage.b8h` / T800 | `2500.0000000011773400`, difference `-1.1773408914450556e-09` | 2,373,606 |
| `savage.b4h` / strict T414 | Same printed double result | 35,761,659 |
| `savageb.bbh` / strict T414 | `2.4772444E+03`, difference `2.2755615E+01` | 7,151,270 |

The `b` variant uses REAL32 and `tbmaths.lib`, so its result is not an oracle
for the double variants. `savageb_test.occ` isolates log/exp round trips, while
`savageb_test2.occ` probes `UNPACKSN`/`ROUNDSN`. No supplied BTL was identified
for those two diagnostic sources. Both C and Occam makefiles can produce the
same `.b8h` name; its filename alone does not prove language/compiler origin.
Counts include startup, libraries and output, not just the recurrence, and
are instruction counts rather than hardware timing comparisons.

### POV-Ray: a working full-render baseline and a larger compiler workload

The supplied [build configuration](t4/examples/povray/povsrc/build/povray.cfs)
has a root/cache processor plus three pipes of five workers: 16 T800 nodes.
The [platform template](t4/examples/povray/povsrc/machine/transp/povray.cfs)
instead has three workers per pipe. The collector map agrees with the
16-node build. Do not rebuild the template and assume it reproduces this BTL.

The platform sources include startup/get_param handling, channel/alternative
operations, high-priority I/O multiplexers, file caching and pixel transport.
The map budgets each rendering process 25,876 bytes of workspace and 185,380
bytes of code, including linked runtime/library code. These are process
budgets, not per-function compiler frame sizes. The INMOS build uses `icc -t8`
and toolset runtime interfaces; comparing new TinyCC-generated code requires
rebuilding kernels/application code with verified compatible runtime support,
not merely relinking an opaque BTL.

`povscn/level1`, `level2` and `level3` contain 34, 27 and 16 demo `.pov`
scenes. They are scene collections, not quality settings. `include` holds
shared definitions and `math` contains mathematical scenes. Level 3's
`chess.pov` documents 430 primitives plus 41 bounding shapes. The matching
scripts select scene files and render at 640×480.

The chief rendered `level1/box.pov` with the supplied BTL, isolated T4
`2853b29`, the supplied collector map and `-st -s8 -sn -16 -sm 22`.
The complete 921,615-byte PPM is **byte-identical** to
`pictures.zip:ppm/level1/box.ppm`, SHA-256
`fcaeaccaea31d263bede7a2f8774e2e7af8dcfc6f640a893a3fd1d3b8d1d6db0`.
The BTL SHA-256 is
`261e056fa3bc03bb9f41454938225703e45f8645d8fff8bc402350eadbf4d831`.
This establishes one reference-matching baseline, not all-scene coverage or
a TinyCC performance improvement.

A 16×16 experiment timed out because `muxdemux.c` fixes `MAXX=640`,
`MAXY=480`, writes full-width rows, and waits for 480 pixel messages. Use
640×480 for the existing image; a small-resolution test requires rebuilding
the multiplexer and workers consistently. Use short relative include paths
and a single-line `povray.def`, as in the supplied scripts. A trial with
long absolute paths/multiline defaults failed and is not an accepted harness.
Do not run the old `pov` script's global `pkill -9 -x t4`; the threaded
launcher avoids terminating unrelated emulator work.

### Concrete compiler priorities from these references

1. **Resident FP results across unary operations.** A current TinyCC probe
   `__fpsqrt(x*x + 1)` still spills and reloads immediately before sqrt for
   both float and double. The float case also copies the spilled word to
   another workspace slot. Extending resident-operand handling to unary
   builtins is a concrete next experiment, preserving aliasing, pending call
   results, FP-stack depth, precision, rounding and exceptions.
2. **Aligned double parameter storage.** A C Horner-step probe already uses
   fused `fpldnlmuldb`/`fpldnladddb`, but first homes all three double arguments
   through integer load/store pairs. Determine which copies are required by
   the ABI and which can feed the FPU from existing aligned storage. The
   seed-329 alignment and nested-call regressions remain mandatory.
3. **Double vector/matrix and polynomial loops.** POV-Ray's `vector.h`,
   `matrices.c`, `spheres.c` and `vect.c` provide dot products, normalization,
   sphere intersection, matrix multiplication and Horner evaluation. Reuse
   proven addresses and reduce workspace traffic while preserving operation
   order. Matrix multiplication uses a temporary to support aliased inputs
   and output; do not replace it with destructive direct stores.
4. **Math-library and numerical contracts.** POV-Ray defaults `DBL` to
   `double`; its quartic solver explicitly tunes tolerances for IEEE64.
   Approximate reciprocal/sqrt, lower precision, reassociation or a
   single-round multiply-add are not justified by these references. Savage's cumulative
   error and reference POV pixels are useful numerical checks alongside
   instruction/encoding measurements.
5. **Control flow, scheduling and ABI costs.** Counted-loop lowering and
   small procedure calls are visible in Whetstone; POV adds indirect calls,
   recursive work, process parameters and channel traffic. Keep first-gen
   ISA availability, protected FP/block state, volatile access and trap
   behavior explicit. Do not copy Occam's bounds/error-check overhead into
   ordinary C or compare differing safety contracts as pure optimizer gains.

Raw runs, source-to-instruction probes, executed-address histograms, profiler
adaptations and fingerprints are retained in
`logs/compiler-coordination/whetstone-savage/`. Profiling disables emulator
instruction fusion, records only executed addresses, and uses the current
decoder's inclusive instruction length. Whetstone's timer process adds small
run-to-run event-count variation; no aggregate hardware-speed claim is made.
