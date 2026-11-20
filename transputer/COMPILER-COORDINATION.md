# Compiler coordination — 6 November 2026

The [CoreMark instruction-count checkpoint](COREMARK-INSTR.md) (20 November)
cuts CoreMark's executed instructions per iteration from 1,301,505 to
1,027,543 at `-mt9000` (-21.0%) and from 1,836,982 to 1,609,630 at `-mt800`,
with five new passes over the finished function (narrow locals as words,
value ranges, branch-free boolean selects, block-local copies, loop-invariant
hoisting) and the dead post-increment value.  CRCs validated; c-testsuite
244/244 at three targets, local suite unchanged, 300 difftest seeds.  The
report also measures candidate compiler-facing instructions for the tp
repo's T2026 extension (about 29% fewer instructions together).  Its
round 2 takes `-mt9000` on to 895,789 (-31.2% on the baseline) with six
more post-pass rewrites: late range rules, jump and flag threading, tiny
leaf inlining, tail copies and loop inversion.  Round 3: 879,969 at
`-mt9000` (-32.4%), and `-mt800` 1,609,630 -> 1,281,628 once narrow locals
learned T800's two-`sb` halfword store.  It uses no new opcodes, and
passes the same gate, with 150 more difftest seeds and the Doom hashes.

The [workspace narrowing checkpoint](tests/bf16-softfloat/README.md#workspace-narrowing-optimization--20-november-2026)
shares binary16/bfloat16 TVX narrowing and directly loads aligned workspace
bits. Each exhaustive-edge narrowing workload saves four instructions per
conversion (506,376 to 453,128). All 76 image runs pass, including both format
suites, both super BTLs, language and volatile/packed/call context tests.
Software and widening timings are unchanged; super images are byte-identical.
The README explicitly records payload-preserving binary16 NaN rules for
MODERNISE/RTL acceptance: use reference JSON `target`, not canonical
`softfloat`. Instruction counts are not silicon-cycle measurements.

The [binary16/software-bfloat16 checkpoint](tests/bf16-softfloat/README.md#binary16-and-integrated-software-bfloat16--19-november-2026)
removes the TVX binary16 widening scratch/FPU roundtrip: four instructions
saved per conversion, 11.8% of the measured exhaustive widening workload.
Both 16-bit formats pass all widening patterns and 13,312 narrowing vectors
on software and TVX paths, 56 image runs total. Updated super BTLs also pass
699/1023 native probes plus all 78,848 software bfloat16 checks per image,
with final FPU-ALL PASS required. Packed input/reference tables preserve the
256 KiB guard. Four 30-check binary16 language runs pass; T4 is unchanged.

The [TVX widening optimization](tests/bf16-softfloat/README.md#tvx-widening-optimization--19-november-2026)
removes the scratch/FPU roundtrip from __bf16 widening. The 65,536-pattern
TVX workload drops by four instructions per conversion, 2,230,528 to
1,968,384 total; software and narrowing counts are unchanged. All 315,392
SoftFloat-derived/policy-aware checks and four 31-check language runs pass.
The emitted-code regression rejects the former roundtrip. This is a TinyCC
optimization; unchanged T4 already implements the conversion correctly.

The [bfloat16 checkpoint](tests/bf16-softfloat/README.md) validates all 65,536
widening patterns and 13,312 narrowing vectors against the supplied SoftFloat
fork, with explicit NaN-policy differences. TinyCC software and optional TVX
paths are tested separately and their emitted opcodes checked. All 28
direct/link image runs pass: 315,392 conversion checks with UART instruction
timing. No implementation fixes were required. TVX narrowing uses fewer
instructions; the current TVX widening sequence uses more than the software
shift and is a future optimization candidate. This is separate from original
T9000 hardware-FP coverage and does not claim SoftFloat exception-flag support.

The [cross-opcode edge checkpoint](tests/fpu-super/README.md#cross-opcode-edge-coverage--19-november-2026)
adds 96 T9000 probes for rounding consumption, passive NaN transport,
unary NaN flags, range boundaries and trap state. Full images pass 699/1023
cases; all 12 full/seed runs total 4,340 timed passes. Packed expected-value
tables reduce image sizes and a deliberate slot-21 mutation is detected.
The 56-opcode audit and fresh 32 MiB Linux signal/CoreMark/desktop boot pass.
T4 and compiler implementations are unchanged in this checkpoint. Standard
T9000 bfloat16 software conversions and optional TVX remain separate future
test work, not hardware-FP coverage claimed here.

The [FP opcode/rounding checkpoint](tests/fpu-super/README.md#fp-opcode-audit-and-compound-rounding--19-november-2026)
audits all 50 architectural and six compatibility/diagnostic FP opcodes,
including independent assembler-byte checks. TinyCC now accepts fpstall
and fpldall mnemonics. Twenty-six new fpadddbsn rounding/trap-state cases
pass on unchanged T4 `1d88eaf`. Full images pass 699/927 cases, all 12
full/seed runs total 4,148 timed passes, and the fresh 32 MiB Linux
signal/CoreMark/desktop boot passes with restored pixels checked.

The [compound/remainder checkpoint](tests/fpu-super/README.md#compound-and-remainder-edges--19-november-2026)
adds 35 T9000 probes. Nested T4 `1d88eaf` fixes remainder NaN cause codes,
range quotient limits and an invalid host operation with infinite divisors.
The new fpadddbsn cases already pass; no compiler backend change is needed.
Full images pass 699/901 cases, and all 12 full/seed runs total 4,096 passes.
The fresh 32 MiB Linux timer/signal/CoreMark/desktop boot and restored pixels
pass. Generated `trap-cases.json` identifies handler-dependent probes for
external RTL staging without relaxing T4's all-pass acceptance gate.

The [NaN propagation and conversion-trap checkpoint](tests/fpu-super/README.md#nan-propagation-and-conversion-traps--19-november-2026)
adds 30 T9000 probes and fixes architectural NaN operand selection in nested
T4 `6754ff5`. Full images pass 699/699 T805 and 866/866 T9000; all 12
full/seed runs total 4,026 passing timed probes. The unchanged Linux kernel
`4726b24b92be` passes a fresh 32 MiB libc/timer/signal/two-context CoreMark/
desktop boot, including pixel restoration. Both images retain the 256 KiB
layout. T9000 is the architectural target and T805 a compatibility gate;
no external RTL default-mode decision is assumed or changed.

The [NaN/conversion and multi-seed checkpoint](tests/fpu-super/README.md#multiple-seeds-and-exceptional-conversions--19-november-2026)
validates 699-case T805 and 836-case T9000 super images plus two extra seeds.
Nested T4 `58a9971` preserves NaN signs for T9000 fpabs while protecting T805
sign clearing. All 12 timed image runs and the fixed-emulator 32 MiB Linux
signal/CoreMark/desktop boot pass. TinyCC hardware instruction selection
already matches both targets; no compiler encoding change was needed.

The [expanded SoftFloat checkpoint](tests/fpu-super/README.md#expanded-softfloat-super-btls--19-november-2026)
adds 408 independent arithmetic/conversion vectors, including 112 seeded
random cases. Combined T805/T9000 super BTLs pass 627/764 cases in both boot
formats, and the fresh 32 MiB Linux signal/CoreMark/desktop boot passes.

The [19 November architectural FP-state checkpoint](tests/fpu-super/README.md#architectural-fp-state-and-linux-abi--19-november-2026)
updates T4's seven-word FP image and Linux's protected trap/signal ABI together.
The 32 MiB boot validates distinct caller/handler FP flags, nested two-thread
FP/MOVE2D signals, two-context CoreMark and desktop restoration. Kernels that
read trap causes from PDS.Cntl must be rebuilt: that word now preserves STATUS.
The suite also supports independent finite SoftFloat reference vectors.


The [19 November CONFIG_SMP checkpoint](tests/bench/smp-kernel-2026-11-19.md)
records a clean, boot-tested SMP-enabled kernel (`025cabea69d7`) and TinyCC
per-CPU/lock-text linker support (`5c5c983d`). Four-node lock tests and the
protected-thread/CoreMark/desktop boot pass. Linux still reports one online
CPU; shared kernel RAM and IPIs remain prerequisites for secondary scheduling.

The [19 November four-T9000 Linux bootstrap checkpoint](tests/bench/shared-linux-boot-2026-11-19.md)
records Linux-controlled release of three shared-workspace secondary images,
atomic/per-CPU admission checks and a subsequent protected-thread/CoreMark/
desktop boot. Linux commit `4e53d2d097c2` keeps this opt-in and parks the
secondaries; CONFIG_SMP and secondary Linux scheduling remain unimplemented.

The [19 November shared-memory board checkpoint](tests/bench/shared-board-2026-11-19.md)
records four- and eight-T9000 transparent RAM/atomic validation in nested T4
commit `bfc321e`, plus all 21 existing T9000 regressions passing after runner
repair `acf398a`. This is the functional board foundation; SMP Linux boot,
serial-link timing and the distributed POV-Ray service remain pending.

The [17 November Linux fill checkpoint](tests/bench/linux7-fills-2026-11-17.md)
records the strict-link desktop boot with wider aligned fills and
sixty-four-word framebuffer row clears. At the user's request, Linux history
from `842b0d5cfa08` through the validated changes was squashed into
`8708fa3c6263`, preserving the complete tracked source tree. The original
commits are retained at `codex/linux7-before-squash-20261117` in the nested
Linux repository. Earlier Linux commit identifiers below describe that
preserved history.

This is the saved handoff for the six TinyCC Transputer sessions on
**6 November 2026**. At the user's request all six stopped taking new work,
finished their bounded checks and reported ready for archival. The chief has
reviewed the handoffs and preserved the unfinished patch. Archive completion
is recorded below. `STATUS.md` contains historical baselines, not the current
compiler or `linux-7` state.

**Post-archive follow-up:** the chief completed the slot-3 bounds repair in
`685532a4` on 6 November 2026. All twelve target/profile runs report zero
overflow and failures; both host profiles retain the canonical frame hash.
See [bounds acceptance](fbwm/SHM-SLOT-OVERFLOW-2026-11-06.md). FBWM and all
guest clients must be rebuilt together; the commit is local and no complete
rootfs deployment is claimed. The six worker chats remain archived.

The chief also audited the supplied Whetstone/Savage sources and executed
BTLs, and completed a 16-node POV-Ray `box.pov` render byte-identical to the
included reference. The [legacy math/renderer audit](COMPILER-AUDIT-2026-11-06.md#legacy-math-and-renderer-reference-audit--6-november-2026)
records benchmark pitfalls and new candidate work: resident FP unary operands,
aligned double parameter staging and strict-order vector/polynomial loops.
These are measured reference observations and unmerged optimization avenues;
no worker sessions were reopened.

The chief's [deep POV-Ray analysis](POVRAY-BTL-ANALYSIS-2026-11-06.md)
records the actual 16-node wiring and process/memory budgets, a reference-exact
render with separate per-node instruction profiles, and measured FP/workspace
and root byte-output targets. The chief also completed the
[resident-result-before-sqrt acceptance](tests/bench/fpsqrt-resident-2026-11-06.md)
in `b70eb63c`, with the reusable POV harness in `0d2aa62b`:
all target regressions, hardened Doom, CoreMark, all four ray images at one/
four nodes and complete rebuilt POV `box`/`basicvue` references pass. The
full-source [POV gate](tests/bench/povray/README.md) is a generic single-node
fixture with existing T9000 libc, distinct from the unchanged ICC BTL.
The subsequent [buffered RGB row acceptance](tests/bench/povray-rgb-rows-2026-11-06.md)
closes target glass compatibility using the scoped historical generator
(`b0b79743`). Pixel and row builds match all three full references. T4
`b3ab1e0` updates collector sources/shared output; `2d6cf12` adds optional
MOVE2D packing. The full generic box saves 1.946% modeled cycles. The actual
sixteen-node ICC image still requires recollection/toolset integration;
the existing BTL is unchanged. No archived worker chats were reopened.

The next [double-parameter storage acceptance](tests/bench/double-param-storage-2026-11-06.md)
reuses the existing ABI area for nonvolatile eight-byte doubles instead of
copying them into new callee locals. It passes the four workload gates,
including complete target glass, 48 target/layout regressions and sixteen
mixed old/new ABI runs. The POV box saves 4,119,717 decoded instructions and
0.17855% modeled T800 cycles; its ELF shrinks by 1,208 bytes. Volatile and
variadic controls remain byte-identical. Caller ABI and runtime archives
remain unchanged.

The subsequent [direct double-return acceptance](tests/bench/double-return-pairs-2026-11-06.md)
loads existing result pairs directly into ABI B/A, removing the intermediate
IRET/IRE2 copy. It passes all four workload gates including full glass,
80 target/layout regressions, 32 mixed-version ABI runs and eight VLA-return
runs. The full POV box removes 172,148 workspace instructions and 258,347
modeled T800 cycles (0.00462%); its ELF shrinks 400 bytes. FPU opcode totals
are unchanged. The focused Horner frame shrinks ten to eight words. Memory,
volatile and cleanup-preserved return values retain their existing path.

The next [aligned-memory double-return acceptance](tests/bench/double-memory-returns-2026-11-06.md)
extends the shared return helper to proven nonvolatile locals/parameters and
aligned nonlocal memory. The local proof retains original-symbol and address-
escape checks; nonlocal reads retain low/high order and T414's existing path.
All four workload gates pass, including full glass, plus 80 target/layout,
32 mixed-version ABI and eight VLA runs. The expanded return fixture has
29 cases. Full POV box saves 527,204 decoded instructions and 863,402 modeled
T800 cycles (0.01543%); the ELF shrinks 184 bytes. Identity-return frames
shrink ten to one words. Opaque locals, packed accesses and pending checks
retain their existing materialization. The archived worker chats stay closed.

The [direct double-literal acceptance](tests/bench/double-constant-returns-2026-11-06.md)
loads already-converted literal bits into ABI B/A without literal memory or
workspace staging. Standalone frames shrink ten to one words; focused
zero/one/dense-literal call kernels save 28.04%/15.77%/3.50% modeled cycles.
The full POV ELF shrinks 256 bytes, while box/basicvue decoded runtime totals
are unchanged. All workload gates pass, including full glass, plus 80
target/layout, 32 mixed-version ABI and eight VLA runs. The return fixture
now has 40 cases, including exact literal bits and widened early-return
epilogues. Memory, flagged and non-double return controls stay unchanged.

The [direct double-local-store acceptance](tests/bench/double-local-stores-2026-11-06.md)
extends statement-end cleanup to immediate copies of FPU double results into
proven nonvolatile, unescaped locals/parameters. It removes temporary pair
and optional high-word copies while preserving FPU operation totals. Full
POV box saves 45,305,362 decoded instructions and 71,593,148 modeled T800
cycles (1.27956%); its ELF shrinks 5,304 bytes. Focused recurrence and
accumulation loops save 19.73% and 12.24%. All workload gates pass including
full glass, plus 96 target/layout, 32 mixed-version ABI and eight VLA runs.
A new seventeen-case fixture covers loops, aliases, live results, volatile/
packed destinations, control flow, signed zero and wide frames. Soft-float
store probes and the negative store controls remain byte-identical.

The [consumed double-pair acceptance](tests/bench/double-pair-reuse-2026-11-06.md)
reuses contiguous operand pairs whose two words are dead outside the current
operation. The audit also repairs FP producer motion past a stored-result
copy and prevents workspace remapping from splitting a double across the
pinned outgoing-argument prefix. Both have dedicated regressions; the float
copy bug is reproduced on baseline T800/T9000. The final build passes full
glass and all workload gates, 128 paired target/layout runs, 32 mixed ABI,
eight VLA and 384 generated-expression bit comparisons. Full box removes
163,694,057 decoded instructions and 372,076,234 modeled T800 cycles
(6.73621%); its ELF shrinks 15,632 bytes. Compound-product and dot-product
loops save 35.26% and 44.15%, preserving logical arithmetic totals.

The [double workspace-unit acceptance](tests/bench/double-slot-units-2026-11-06.md)
merges known two-word FPU accesses before slot remapping. Units touching the
outgoing-argument prefix remain entirely pinned, preserving the turbulence
repair while allowing other units to use shorter workspace addresses. Full
box saves 60,119,037 prefix bytes and modeled T800 cycles (1.16703%); the ELF
shrinks 6,072 bytes. Decoded instruction/FPU totals are unchanged. All four
workload gates pass, including full glass with the recovered RNG, plus 144
paired target/layout runs, 32 mixed ABI, eight VLA and 384 generated-expression
bit comparisons. Static checks confirm double adjacency and pinned arguments;
soft-float fixture objects remain identical. The six worker chats stay closed.

**10 November 2026.** A re-profile of the full box render found `main` at
`6e2f6d81` instruction-identical to that candidate. The
[double memory-store acceptance](tests/bench/double-memory-stores-2026-11-10.md)
then extends the statement-end rule to destinations in memory: an FPU double
result goes straight to the parked address instead of through a temporary
pair and two `stnl`s. Full box saves 134,896,513 decoded instructions and
242,441,560 modeled T800 cycles (4.76185%); the ELF shrinks 8,352 bytes and
FPU opcode totals are unchanged. All four workload gates pass including full
glass, plus 176 paired target/layout runs, 32 mixed ABI, eight VLA and 384
generated-expression bit comparisons; soft-float objects are byte-identical.
Open from that work: memory-to-memory double copies still move two words.
The packed-operand miscompile it turned up is fixed: a `long long` or
`double` packed member reached through a pointer and passed to a call (so
every soft-float operation on one) was staged with `ldnl 0`/`ldnl 1`, and
two FPU shortcuts loaded one with `fpldnlsn`/`fpldnldb` at its odd address.
All three now copy the bytes; `test_packed_wide_arg.c` covers them, and of
the local and c-testsuite objects only that fixture's change.

**11 November 2026.** The wide-constant lane turned into something else.
Lowering the address cache to two uses was measured and dropped (0.115% on
the box render, three routines slower). The hot shading routines cached
nothing because any function with a call through a pointer was refused by
the cache, the scheduler and the slot remap: the call's tail addressed
slots after its `ajw -4`. The
[indirect-call acceptance](tests/bench/indirect-call-postpass-2026-11-11.md)
stores the return address at `Wptr[-4]` before the AJW and pins the tail
(`ldc 5; ldpi; stl -4; ajw -4; gcall`), so those functions go through all
three. Full box saves 59,073,121 modeled T800 cycles (1.219%) against
`00e15657`; Doom's segloop and masked phases gain 2.7% and 4.1%. All
workload gates, c-testsuite at four parts, the local suite at three, 150
difftest seeds and a linux-7 boot to the desktop pass. The full suites were
also run on `ff6a90ba` and are clean.

The scheduler's "ldlp-escape" refusal of a double load or store through a
register pair, which declined 126 POV functions, went the same day
([acceptance](tests/bench/sched-double-access-2026-11-11.md)): the access
is recorded as a read of both words, and S4/S5 stop at it. Full box saves a
further 57,739,465 modeled cycles (1.206%) against `27efe07e`, no routine
slower; soft-float objects are byte-identical; the same gates, suites, 150
more difftest seeds and a kernel boot pass. The box render is 7.1% below
`6e2f6d81`. A second step keeps the local range when a local feeds an FP
access of known extent: 17,911,541 more cycles (0.379%) against `692f20a6`,
same gates. Copy propagation in those functions was measured and dropped
(more cycles).

**16 November 2026.** The scheduler now keeps only the words of an escaped
object live throughout the function, retaining the analysis of other local
words. The [escape-tracking acceptance](tests/bench/sched-object-escape-2026-11-16.md)
records 10,607,628 fewer modeled T800 cycles (0.225%) for POV box against
`c335c128`, with 2,809,108 fewer `LDL` and `STL` executions each. This
includes 1,623 double word spill/reload pairs in `sin` argument staging.
FPU double transfer totals match. Doom, CoreMark, the raytracer and full
POV box, basicvue and glass pass with both compilers.

The [first-use address cache acceptance](tests/bench/gaddr-first-use-2026-11-16.md)
keeps two uses of a symbol in the same basic block on a private frame word.
`All_Object_Intersections` cuts four unconditional counter-address loads,
saving 40 modeled T800 cycles per call in box and basicvue. Its measured
cycles fall 18.45% and 17.25%, respectively. Whole-render cycles fall 1.07%
and 1.22%, including the costs of reinitializing addresses in hot loops.
Doom, CoreMark, the raytracer and all three complete POV references pass.

The [double-copy acceptance](tests/bench/double-copy-words-2026-11-16.md)
uses the same parked pointer for both words of an aligned memory copy.
The high word uses `STNL 1` directly. Both source words are captured first
and remain available as the assignment result. Box saves 8,723,915 modeled
T800 cycles (0.188%), of which 5,040,966 come from the inverse vector
transforms. Basicvue saves 6,324,856 (0.080%). Doom, CoreMark, the raytracer
and all three complete POV references pass, along with bit-pattern and
packed-copy controls on four CPUs.

The [stack-resident double copy acceptance](tests/bench/double-copy-stack-2026-11-16.md)
keeps both words of a discarded aligned local copy on the evaluation stack.
The inverse transforms save one workspace store and one reload per copy,
for 7,561,449 modeled cycles in box. Full box saves 8,213,268 cycles (0.177%).
Basicvue retains its measured cycle total. Doom, CoreMark, the raytracer
and all three complete POV references pass, including glass.

The [24-byte result copy acceptance](tests/bench/double-copy-move-2026-11-16.md)
combines three terminal double copies when the source is in the callee's
frame and the destination is an unchanged incoming pointer. The two
inverse transforms and `MTransNormal` account for all 865,064 replacements
in box. The full render saves 18,166,344 modeled T800 cycles (0.392%) and
19,031,408 decoded instructions. Doom, CoreMark, the raytracer and the
complete box, basicvue and glass references pass. The four-CPU fixture
checks both the block move and six controls, and seed 329 plus 100 new
fuzz seeds pass at both code pads.

The [non-terminal result copy acceptance](tests/bench/double-copy-nonterminal-2026-11-17.md)
extends the same 24-byte `MOVE` to proven fallthrough sequences. All five
matrix vector routines produce byte-identical code. The new matches are two
`painted2` texture branches that copy RGB before storing alpha. The focused
render saves 83,636 modeled T800 cycles (4.07% inside `painted2`), and the
stock box and basicvue profiles match the baseline. Doom, CoreMark,
the raytracer and the full POV-Ray glass reference pass.

The [Doom status-bar MOVE2D acceptance](tests/bench/doom-copyrect-move2d-2026-11-17.md)
adds an opt-in, disjoint-span fast path to `V_CopyRect`. With the rebuilt T4
transfer-cost model it saves 11.1% and 19.4% on the Freedoom number and face
rectangles, respectively. The full T9000 Doom application links. The Doom,
CoreMark and raytracer gates pass, and all three POV-Ray images match their
references, including glass.

The [POV-Ray collector MOVE2D acceptance](tests/bench/pov-collector-move2d-2026-11-17.md)
captures worker traffic from the packaged 16-node BTL and drives the
original `muxdemux.c` with both TinyCC packer variants. The collector
interval saves 16,284,480 modeled cycles and 11,061,120 instructions per
scene. Both packers reproduce box, basicvue and glass byte for byte.
`Create_Ray` conversion spills and `Intersect_Boxx` FP scratch pairs are
the next measured compiler candidates.

The [signed conversion FP-stack acceptance](tests/bench/signed-i32-fp-stack-2026-11-17.md)
reuses FP history for signed 32-bit to double conversions. `Create_Ray`
removes 1,536,000 spill and reload pairs per profiled scene and saves
14,131,200 modeled cycles (3.19%). The box and basicvue renders save
0.338% and 0.241% across the full image, respectively. The focused fixture
covers signed limits, operand order, comparisons and pending calls. Doom,
CoreMark, all raytracer images and the full POV-Ray glass reference pass.

**Source layout, 10 November 2026.** The back end was restructured with no
change in generated code (objects for all six parts, linked images and the
Doom/fbwm corpus byte-identical at every step). `transputer-gen.c` now
includes parts of itself, in this order: `transputer-knobs.h` (every
environment switch, read through `tp_knob()`: add a new one to its table),
`transputer-hist.h` (the emission window; the `tp_last_*`, `tp_prev_*` and
`tp_last_stl_*` trackers and `tp_stl_track_clear()` are gone, ask
`tp_hist_last_instr()` / `tp_hist_last_stl()`), `transputer-fnopt.h`
(relaxation, the scheduler as `tps_*` functions, slot remap, address cache),
`transputer-call.h`, `transputer-int.h` (`gen_opi` as `tp_opi_*` stages) and
`transputer-fp.h` (`gen_opf` as `tp_opf_*` paths, FPU residency, the
statement-end rules). `transputer-link.c` includes `transputer-linux.h`
(kernel-image knowledge) and `transputer-linkopt.h` (peephole, ICF, call
relaxation); `tccgen.c` includes `transputer-front.h` (inline expansion, the rotated
`while`/`for` loops, the builtins `unary()` hands over). A new file must be
added to `transputer_FILES` in the Makefile as a `.h`.

## Closure checkpoint

| Session | Saved result and validation | Remaining work / retained artifacts |
| --- | --- | --- |
| Optimize 64-bit division (Linux lane) | Linux `e3181b5f0775`: per-continuation FP/block switching; three nested clients and supervisor 200-round-trip controls pass. Exact fixture rebuilds match. Trap-first/default boots reach `DESKTOP_LOOP`; six syscall arguments traced across four calls. | Native flags/RTL, timeslice/signal/task-switch policy and disjoint regions remain open. T4 high-priority shadow omits rounding mode: add an interrupt/rounding regression first. Replay commands: `logs/p-isa-audit/checkpoint-2026-11-06.txt`; committed fixture: `linux-7/arch/transputer/tools/pstate-test/`. |
| Review and implement afcc4dfe (linker lane) | `f792e755`, acceptance `a402b034`: exact parent/commit pass hardened Doom, fixed-seed CoreMark and reference ray; ELF/diagnostic/relocation pairs match. Linux-link medians improve 0.8–1.7%, costing about 444 KiB of extra index memory for the measured object. | No pending code or process. Captures: `logs/link-acceptance-f792/`; [tracked report](tests/bench/linker-reloc-index-2026-11-06.md). These are short-link measurements, not whole-build gains. |
| Compare raytracer binaries | `681cbe51`, acceptance `f2997503`: exact parent/commit save 8,980,996 modeled T800 cycles (0.2965%) on model 3. All four images match at one/four nodes; hardened Doom and CoreMark unchanged; volatile/cast/alias/stack/target/relocation tests and seeds 329/367 pass. | TransLocal loop-to-MOVE proof remains open. Retained `.tmp-rayaccept/` snapshots/logs/histograms; [tracked report](tests/bench/indexed-fp-address-reuse-2026-11-06.md). No live process. |
| Refactor transputer code layout | `84d7e632`: shared direct workspace opcode classification. 294 comparisons across six targets/options: 252 identical objects, 42 matching diagnostic failures. Independent Linux wrapper source review found no blocker. | `_wsop_private_7204/` retained. S10/`tps_dead_after` is reachable; profiling did not justify changing allocations. No pending edits/process/worktree owned. |
| Explain per-architecture libtcc1 builds (FPU lane) | Direct conversion `16e7e460`; investigations `1dbe953a` / `15c7037b`; no speculative FPUNOROUND fix. Read-only Linux image-size/order review completed. | T4 `fpstall` rounding postcondition, native exception flags and FPUNOROUND transient representation need separate tests. Unary FP resident-operand/sqrt reuse is only a candidate. Detached worktree and `_fp_noround_audit/`/earlier untracked probes retained. |
| Build per-part uClibc (aggregate/compositor lane) | Seed repair `fc344c0b`; aggregate `c96e1bcd`, acceptance `b0e8dc23`; compositor `bc3c780c`, bank canary `f873c42b`. Chief follow-up `685532a4` fixes slot 3 at 400×284: twelve target/profile runs and both host profiles pass. | Rebuild FBWM and guests together; older binaries retain unsafe capacity. The [original patch](fbwm/fbwm-slot-bounds.pending.patch) and detached worktree/results remain as superseded historical evidence, not a pending repair. |

All sessions report no remaining live build/emulator process. No new task
should be inferred from the candidate list. Resume only a selected lane with
the relevant snapshots and current source checked first.

### Repository and publication state

* Outer repository `main`: the listed compiler/linker/acceptance commits are
  local. The sessions did not push this concurrent history.
* Linux `master`: `bc2969c2ea63` (frame ABI) and `e3181b5f0775` (FP/block state)
  are local. The owner confirmed `cbf812242ba1` as the published
  `origin/transputer` tip at checkpoint.
* T4 `master`: `8177e21`, `fe000b2` and `2853b29` are local timing/FP-access
  commits. The shared executable is older; use a recorded isolated build when
  reproducing measurements.
* Compositor branch `codex/fbwm-move2d` was pushed with `ed29c40e` / `31379af7`,
  equivalent to the local main canary/compositor files. Its detached worktree
  contains the original uncommitted bounds proposal; it is superseded by
  local-main repair `685532a4`. The existing published compositor commits
  alone are not evidence of a build containing that follow-up repair.
* Shared `.gitignore` scratch-directory additions, untracked `transputer/examples/`,
  Python caches, Linux `framebuffer.bmp`, T4 `CODEGEN.md` edits and
  `t4.pre-2e44501` remain untouched. The closure commit does not sweep them in.
* Full six-session handoffs are retained locally in
  `logs/compiler-coordination/session-handoffs-2026-11-06.json`. Logs and private
  snapshots are retained evidence, not tracked production inputs.

### Chat archival record

All six handoffs were reviewed and all six chats were **archived successfully
on 6 November 2026**. The archive tool confirmed `archived: true` for every
ID below. Worktrees and evidence were preserved; no worker was interrupted
mid-run. Archival is reversible. The chief chat remains open.

| Session title | Chat ID |
| --- | --- |
| Compare raytracer binaries | `01a1abe3-87fc-7913-9242-a60ac3dbfc8c` |
| Refactor transputer code layout | `01a1abec-9ea0-71c0-aad9-bdf264ca9b32` |
| Explain per-architecture libtcc1 builds | `01a1b0c7-5f2d-7ef1-a9ed-1c1c1be6d241` |
| Review and implement afcc4dfe | `01a1abe1-bc88-7353-a81f-778bdf8088f3` |
| Build per-part uClibc | `01a1b0bf-996f-70e3-9c3b-d2620f9677f3` |
| Optimize 64-bit division | `01a1b0bd-7353-73c1-b70b-325d45a2d08e` |

## Work allocation before checkpoint

| Session title | Owned work | Next evidence to collect |
| --- | --- | --- |
| Explain per-architecture libtcc1 builds | Direct int-to-FPU `16e7e460`; pool trials rejected; rounding investigation `1dbe953a`; FPUNOROUND semantic audit | Verify manual encoding/operand order and internal precision against primary sources; preserve conversion contracts and software-FP fallback |
| Review and implement afcc4dfe | Seed verification `05b0cc9f`; MOVE2D timing `8177e21` / `04cb234d`; Doom harness `cabe1fbf`; linker profiling | Find a measured repeated scan/allocation beyond `a1d87275`; require byte/diagnostic equivalence and real-link throughput evidence |
| Compare raytracer binaries | Scheduler correctness baseline; slot-weight and duplicate-load trials rejected in `55a9da8b` / `e75ac2a2`; hot-sequence profiling | Quantify a real hot-loop instruction/prefix saving before implementation; retain bounds/provenance/volatile/target/relocation guards |
| Refactor transputer code layout | Symbol/builtin consolidation; shared history overlap `bef52a24`; `tps_dead_after` allocation/traversal profiling | Measure large-function compiler cost before changing storage; preserve rewrite decisions, updated reader facts and copy-chain folding |
| Build per-part uClibc | Scalar64 repair `fc344c0b`; guarded whole-struct MOVE `c96e1bcd`, exact acceptance `b0e8dc23`; compositor `bc3c780c`; later chief bounds fix `685532a4` | Bounds validation complete; rebuild participating clients together before deployment |
| Optimize 64-bit division | Linux continuation/frame ABI repairs; T4 FP access repairs `fe000b2` / `2853b29`; per-continuation state preservation | Prove nested FP/block-move clobber first, repair entry/resume wrappers; keep physical alias migration separate and trap-first opt-in |
| Improve TinyCC Transputer compiler | Coordination, regression gate, audit/ownership records | Verify scheduler baseline first, keep experiment ownership separate, review evidence and commit each validated group |

The [full-file audit](COMPILER-AUDIT-2026-11-06.md) is complete. The review
session fixed its reproduced packed-memcpy purity defect in `0fd07e87`;
the scheduler indirect-reader guard and regression landed as `5a5ef711`.
The coordinator reran `host/run_sched_indirect.sh` successfully; the owner
reports four-target scheduler/frame checks and raytracer image matches on
one/four nodes, plus model 3 on 16/64 nodes. The cast trial was rejected and
recorded in `a361932b`: the normal scheduler already removes its proposed
spill/reload pairs. Keep the newly assigned functions separate.

## Current experiment gate

The user explicitly requested scheduler correctness first, followed by the four
avenues above. All five compiler owners acknowledged these assignments.

* Use a baseline containing `5a5ef711` and `0fd07e87`. Capture an isolated
  compiler snapshot and record its revision/hash; keep runtime archives,
  emulator, target flags and inputs identical across each comparison.
* Test whole-struct MOVE and shift staging against **all three workloads**:
  Doom phase hashes/estimated cycles, fixed-seed CoreMark CRCs/estimated cycles,
  and raytracer image equality/fixed-work modeled cycles. A synthetic win alone
  is insufficient. Apply the same checks to other performance candidates.
* Run the change-specific ABI/alias/volatile/target boundary regressions.
  For refactors, compare objects, relocations and diagnostics before changing
  emission decisions. Keep hardware timing estimates distinct from host speed.
* Whole-struct MOVE cleared post-repair validation in `c96e1bcd` / `b0e8dc23`.
  `fc344c0b` fixes an unaligned
  scalar64 spill reservation: word stores rounded down while reloads read two
  bytes into those words. The shift count was correct. Both original seeds
  329 and 367 now pass the agreeing host oracles; both failed before the fix.
  Six-target regression runs at link pads 0/1027/4096 and three 244-test
  conformance suites passed. Independent original-seed checks also pass on
  strict T414/T425, T800 and T9000; exact hashes are recorded in `05b0cc9f`.
* Commit each validated, useful change separately, with evidence. Keep
  unprofitable or unresolved experiments out of main and record their result.
  A source-only `TransLocal` MOVE experiment estimates potential; it is not a
  compiler optimization until the transformation's preconditions are proven.

Several sessions share one checkout and Git index. Use private source snapshots
for experiments, keep intermediate shared edits buildable, and commit only
owned changes. Inspect the staged diff immediately before committing. Separate
maintenance refactors, semantic fixes and performance experiments into reviewable
commits. A session's title can lag behind its current workload.

## Latest check-in results

* **Direct int-to-FPU:** `16e7e460` integrates isolated `41ef080e`. Six-target
  conversion probes passed. Matched Doom hashes/cycles and CoreMark CRCs/cycles
  were unchanged. Raytracer model 3 at 256x256, one T800, matched the reference
  image and fell from 3,655,261,764 to 3,654,480,420 modeled cycles: 781,344
  fewer (about 0.021%). The merged snapshot's workload objects/ELFs matched
  the measured candidate. Later experiments require their own matched baseline.
  See [measurement report](tests/bench/itof-direct-2026-11-06.md).
* **Shift staging:** rejected as unprofitable; `24e467f2` retains meaningful
  six-target boundary/operand regressions and the evidence. Doom phases,
  CoreMark's 5,008,259 estimated cycles and both raytracer runs at
  3,655,261,764 estimated cycles were unchanged, with matching hashes/CRCs/images.
* **Shared symbol emission:** `3628b7e0` replaces duplicated relocation/placeholder
  emission. The owner reports 168 source/target/debug object/diagnostic matches,
  matching relocation traces, and identical Doom, CoreMark and raytracer-style
  regression objects. This is maintenance, not a measured guest-speed gain.
* **Shared accumulating TVX staging:** `265309e9` covers nine four-argument dot
  operations. All 288 target/call-context comparisons matched, and hardware
  TVX, software TVX, Qwen-dot and T425 software fixtures passed.
* **Word-result builtin table:** `3783a69a` consolidates `disc`, `diss`, `dist`,
  `resetch`, `crcbyte`, `crcword`, `gajw` and `wcnt`. All 256 target/configuration
  object/exit-status/diagnostic comparisons matched; concurrency, process and
  return-register runtime tests passed on default, T414 and T425 models.
* **Accumulating builtin table:** `fb93a8d8` routes nine four-argument TVX
  accumulating builtins through the shared emitter. All 360 target/context
  cases matched: 288 complete object comparisons and 72 malformed-call
  diagnostic/status comparisons. Instruction effects and scheduling are unchanged.
* **Whole-struct MOVE:** `c96e1bcd` stages ordinary aggregates with one MOVE;
  volatile objects/members retain the original per-word copies. A guarded
  trial passes 400 seeds at pads 0/1027, three 244-test suites and focused
  six-target probes; its aggregate benchmark falls 1,854,033 to 1,064,031
  modeled cycles, with hash `1747770740`. Doom phase hashes/cycles, five-iteration
  CoreMark CRCs and 25,368,644 whole-run cycles match, as do reference-equal
  405,061-ray images. The coordinator independently built a snapshot containing
  the integrated guard and passed `host/test_struct_call_volatile.py`.
  Exact `c96e1bcd` versus parent acceptance has now repeated the matrix with
  pinned corrected T4 `fe000b2`; source hashes match the integrated commit.
  [Tracked acceptance](tests/bench/struct-call-acceptance-2026-11-06.md),
  committed as `b0e8dc23`, supersedes the earlier pre-rebase private matrix.
  `fc344c0b` remains the separate seed 329 alignment repair.
* **Pool initializer trial:** source-level dead initialization is real, but
  the normal scheduler also removes it. Current before/after objects and all
  three workload ELFs are identical; the isolated improvement with scheduling
  disabled is not a default-compiler performance win. Do not merge it merely
  because an earlier pass does less work. Report committed as `c6de60f9`.
  A follow-up candidate-ranking trial fixed synthetic first-six admission bias,
  but all three workload ELFs stayed byte-identical. Six-target loop/goto/Duff/
  VLA probes passed. Rejected-trial report: `5b7e89f1`,
  [pool ranking](tests/bench/pool-ranking-trial-2026-11-06.md).
* **Loop/block-copy proof:** `6c9b24a9` adds safety coverage. A source-only
  overlapping forward loop changed behavior when replaced with MOVE, so a
  generic lowering still needs a non-overlap proof. Volatility, final index,
  variable counts, break and continue are also covered; no compiler loop
  optimization is claimed from the source experiment.
* **Loop-weighted slot placement:** rejected; report committed in `55a9da8b`.
  The private trial saves 127,908 raytracer modeled cycles but adds 460
  CoreMark cycles and two Doom fill cycles. All four scenes match at one/four
  nodes for both variants, and seed 329 passes. The small gain and quadratic
  scan did not justify integration. See [report](tests/bench/slot-remap-loop-trial-2026-11-06.md).
* **Duplicate-load probe:** rejected in `e75ac2a2`. One compiler-owned-slot
  LDL-to-DUP site in the raytracer saved no modeled cycles and added 81 executed
  prefix bytes. Local-variable candidates cannot ignore lost volatile qualifiers.
* **Doom harness:** `cabe1fbf` replaces unexercised function bodies with explicit
  failure stubs, rejects linker diagnostics and validates complete reference
  output. All eleven phase hashes match before/after on four targets; a few
  counts shift 320 cycles with layout. This is harness maintenance, not compiler
  speedup. The coordinator independently passed its four host tests. See
  [audit](tests/bench/doom-harness-audit-2026-11-06.md).
* **History overlap helper:** `bef52a24` shares direct workspace-range checks
  while retaining each caller's barriers and conservatively handling integer
  limits. This is maintenance rather than a guest-performance claim.

## Landed work and measurement limits

* `b60ee22b`: runtime archive selection on executable links. Six targets linked
  with the expected archives and resolved a division helper. These checks used
  minimal startup objects and empty libc; full libc application validation is
  separate work.
* `7f6f774d`: eight cached frame addresses/constants. The raytracer session
  measured 1,548,737 fewer modeled T800 cycles on model 3 (about 0.051%). This
  can add four workspace words; it is not evidence of a universal improvement.
* `51a88b30`: direct binary32 literal loading. This completes the backend half
  and regression that previously remained uncommitted after a shared-core edit.
* Runtime division improvements include `c6df9457`, `c76a5ee5` and `2bc5cf36`.
  Keep their dedicated division measurements separate from whole-program gains.
* `c2817144`: shared standalone/inline assembler mnemonic lookup, initial-letter
  filtering, and a namespaced comparison helper that fixes the Windows libc
  `stricmp` collision. All 307 accepted inline mnemonics produced identical
  objects on six targets in three case variants; all 326 standalone mnemonics
  and 19 FPENTRY rejection diagnostics matched. Five native lookup-only runs
  had medians 0.873 s before and 0.567 s after for 3.26 million lookups. This
  measures lookup cost, not guest execution or complete compilation time.
* `a1d87275`: avoid relaxation maps and incoming-reference scans for executable
  sections without marked function candidates. Eighteen synthetic links and
  four existing link-relaxation regression ELFs were byte-identical. T4 runs
  on strict T414/T425 and T800/T9000-target binaries all printed exactly
  `SLINKRELAXOK\nH`, with Error clear. A synthetic link containing 48 unmarked
  1 MiB executable sections had five-run native medians 0.196 s before and
  0.105 s after. This is a linker stress case, not a measured kernel-link gain.

The coordinator's local comparison scripts and captures are in the ignored
`logs/compiler-coordination/` directory. Do not commit generated binaries.

## ISA contracts for optimization

Primary local references are [TCWG](searchable-tis-tcwg.txt),
[T9000 ISM](t9000ism.txt), and [T9000 HRM](t9000hrm.txt). Treat the existing
[ISA opportunities survey](ISA-OPPORTUNITIES.md) as a historical candidate list:
many listed transformations have already landed and its timing assumptions
must be checked against the current emulator.

The user also supplied Michael Brüstle's *Transputer versions*, MIKE's Technical
Note 2 (May 2008). It is a secondary cross-check covering first-generation
Txxx parts, not T9000/TVX. In particular, instruction availability does not
follow numerical CPU order: T425 has later integer/debug operations without
an FPU, while the original T800 has an FPU but lacks some T805 additions.
Verify new gating changes against INMOS primary documentation and strict
target regressions. The note's FPUTESTERROR entry for FPU-less parts needs that
check before broadening any FPU gate. T800 estimated timing is not evidence of
actual T414 instruction timing.

### Memory movement and graphics

TCWG 8.1 and ISM 7.15.1 specify:

* `__move2dinit(rows, dst_stride_bytes, src_stride_bytes)` loads A/B/C into
  block-move state. The transfer takes `(width_bytes, dst, src)` in A/B/C.
  Setup is required before each transfer. The T9000 undefines the integer
  stack and block registers after a transfer.
* There must be no descheduling between setup and transfer. Stage operands
  before an adjacent pair; do not leave a call or loop backedge between them.
  This alone is not proof of trap/interrupt state preservation.
* Zero width or row count does nothing. Source/destination overlap is undefined
  in the manuals. Retain an overlap-safe implementation where required.
* `move2dnonzero` and `move2dzero` select individual source bytes. They are not
  general 32-bit pixel transparency operations. `move2dzero` is not a fill.
* T414 needs a fallback. Existing `fbwm.c` already uses MOVE2D. Remaining
  disjoint rectangular copies in `fbwm_shm.c` are candidates; palette lookup,
  texture sampling and format conversion require their own computation.

The missing MOVE2D transfer cost is repaired in nested T4 `8177e21`, with root
evidence `04cb234d`. The model charges `(2p + 23) * rows`, where `p` is nominal
rounded-up words per row; differing alignments/partial rows remain approximate.
The host arithmetic test and T425/T800/T9000 semantic checks pass, with unchanged
destination hash. The aligned benchmark estimate rises from 1,151 to 1,910,567
cycles for 12 copies because omitted work is now counted. The coordinator also
passed the host arithmetic test independently with native GCC. The shared T4
executable was not rebuilt by that owner: pin the actual tested binary and use
this revision or later before interpreting a new MOVE2D comparison. Older
estimates cannot establish speedup. Keep canary correctness, target timing
models and host emulator throughput separate.

### Less common instructions

* `mint`: `Tp_LDC` already selects it for `0x80000000`. It is also useful for
  unsigned comparison biasing and sentinel/address formation. Its OPR encoding
  is two bytes, not one.
* `lddevid`: already supported with target checks. It is not available on every
  supported CPU and cannot be an unconditional universal feature probe.
* `crcword`/`crcbyte`: already exposed. A CRC-loop substitution must match the
  polynomial, bit direction, seed and finalization exactly. CoreMark validation
  remains mandatory.
* `fpunoround`: TCWG 7.9.3 describes REAL64-to-REAL32 conversion without mantissa
  rounding for normal numbers and zeros. Its prescribed INT64-to-REAL32 recipe
  is directed toward minus infinity, not nearest rounding. `1dbe953a` records
  exact-oracle differences and an unresolved T4 discrepancy in that recipe;
  do not substitute it into the normal conversion path without a separate
  semantic proof. It is not general no-round arithmetic.
* `testhardchan`: INMOS's 29 November 1988 Instruction Set Appendix describes
  shifting BReg into a link's internal register stack and returning DBuffReg.
  It requires inactive links and is not a nondestructive DMA-status probe.
  The current T4 implementation documents the same restriction.

## Existing Linux L/P/G integration

This work is already substantially implemented in the active nested
`linux-7` repository:

* `arch/transputer/kernel/thds_boot.c` installs a THDS via `selth` and verifies
  it with `ldth`. Both `.config` and `tpos_defconfig` enable
  `CONFIG_TRANSPUTER_L_THDS_BOOT`. The early L-trap handler in `thds_selth.S`
  is still a minimal UART marker and `tret` stub.
* `kernel/t9000_prot.c` and `t9000_goprot.S` are in `obj-y`. Exec populates
  per-task soft maps; userspace entry can build PDS/RODS, execute `goprot`,
  dispatch syscall traps and re-enter. Task switching rebinds region state.
* `kernel/setup.c` includes `t9k_enforce t9k_hard_p` in its default command
  line. The saved `logs/06-boot.log` from 3 November records hard-P mode,
  protection selftest PASS, P-entry for `/init`, and PID1 executing.
  This is saved evidence, not a fresh protection test by the coordinator.
* `tests/syscall_stub.c` currently defaults `TP_USE_T9K_SYSCALL` to **0**.
  The C-call gate remains the default; trap-first syscall rollout and tighter
  isolation are incomplete. [T9000-PROTECTION.md](T9000-PROTECTION.md) now
  distinguishes kernel hard-P entry from the userspace syscall choice and lists
  the remaining rollout tests; inspect source/build flags for each image.
* The [Linux isolation review](LINUX-PROTECTION-REVIEW-2026-11-06.md) records
  the original nested-spawn PID 1 panic. Linux `cbf812242ba1` repairs per-context
  continuation preservation. Linux `bc2969c2ea63` and root `a747d3b2` replace
  shared syscall staging/results with six caller-owned words addressed by B,
  number/result in A, and capability tag `0x54504601` at `0x8000006C`. Nested
  trap-path boots and mixed-version fallback checks reached `DESKTOP_LOOP`.
  These fixes do not complete strict isolation or justify the default switch.
* Current code includes broad quarter mappings, a Q3 stack mirror, and
  non-SPINE T4-specific cause/leave-P mechanisms. The Linux owner confirmed
  Q2/Q3 map the same physical GiB: this violates HRM §5.2.2, making the
  descriptor combination architecturally undefined. Desktop success on T4
  does not establish a conforming hardware protection boundary.

### HRM chapter 5 constraints supplied by the user

Apply these to Linux, emulator tests and compiler effects descriptions:

* Four logical quarters have independently sized power-of-two regions,
  256 bytes through 1 GiB, at the top or bottom of each quarter. Physical bases
  are aligned to region size; translated physical regions must not overlap.
* All P-process reads, writes and instruction fetches are checked, including
  device-only restrictions. P-processes cannot execute privileged operations
  (ISM §9.2); the pasted HRM/OCR's description of the permitted subset as
  "privileged" contradicts its next sentence and the ISM.
* CALL/AJW/GAJW must leave Wptr writable. Workspace-extension traps support
  restart after region growth; ordinary illegal accesses are not normally
  restartable. The Linux owner's disjoint-region T4 probes pass all three
  workspace-growth cases and scalar access/device/privilege checks.
* Hardware P-state saving does not save all state. The supervisor must handle
  floating-point and block-move state when those resources can be clobbered
  (ISM ch.13). The Linux owner reproduced remapped `FPLDNLSN` and RX `FPSTNLSN`
  failures separately from state preservation. Nested T4 `fe000b2` fixes FP
  loads through translated word accessors and preflights wide reads; `2853b29`
  repairs stores, checks permissions before consuming the FP operand, and
  preflights wide writes. Denied second-word probes preserve memory/FP state;
  removing the preflight fails the canary. Host/guest protection and workspace
  tests pass, and the unchanged Linux frame-ABI image reaches `DESKTOP_LOOP`.
  Linux `e3181b5f0775` then adds actual per-continuation and supervisor
  FP/block switching; the repeated nested, omitted-save and supervisor tests
  pass on T4. Native flags, interrupt rounding and RTL remain separate gates.
* Both local text manuals render the null descriptor as `#8000000`, whereas
  implementation code uses `0x80000000`. Verify the printed facsimile before
  changing that literal; OCR alone does not establish a correction.

ISM overview and chapter 9 define P execution as a protected mode of an
L-process, supervised by that L-process. A trap returns to the supervisor;
software chooses recovery, resumption or termination. In the port's boot
terminology, G is the global-error/null-THDS starting condition. Do not infer a
separate cluster-wide privileged scheduler from that label.

ISM 10.1.3 explicitly specifies that a trap with a null trap-handler halts the
processor. HRM 2.1.5 and 8.3 separately describe the control unit/control links
and an external controlling processor. Those facilities do not establish an
independently executing G-process, automatic kernel recovery, or debugging
without timing effects.

Compiler changes must preserve legal instruction availability, trap behavior,
rounding state, memory-access semantics and the workspace ABI on these paths.
Kernel integration does not relax the project's ISM/HRM → RTL → compiler →
Linux authority order.

## Indirect and packed narrowing — 20 November 2026

TVX binary16 and bfloat16 narrowing now load binary32 bits through the
existing integer load paths. Packed fields use byte assembly; saved indirect
addresses retain the canonical spill convention. Volatile reads are not
reused. Constants retain the established staging fallback.

Each format passes 528,384 conversion checks in 28 software/TVX image runs,
through direct and link boot. All 65,536 widening patterns are tested; each
of 13,312 narrowing vectors is checked through workspace, volatile indirect,
and packed fields at offsets 1, 2 and 3. Exact NaN sign/payload policies are
checked separately from the supplied SoftFloat fork's canonical NaNs.
Narrowing tests cover nearest-even boundaries and seed-329 random inputs;
this is not an exhaustive test of all 2^32 narrowing inputs or every TVX op.

For either format, the measured TVX indirect workload falls from 493,064 to
453,128 instructions (three saved per conversion). Packed offset 1/2/3
workloads fall from 879,112/919,048/905,736 to 799,240/839,176/825,864
(six saved per conversion). Software, widening and workspace timings are
unchanged. UART output and input preparation remain outside measurement;
loop/call/result staging are included. These are T4 instruction counts,
not hardware cycles. No emulator semantic changes were required.

The super builder now also emits a distinct T9000+TVX image, containing
1,023 native FP probes and 132,096 bfloat16 checks with 516 batch timings.
T805 and standard T9000 retain 699/1,023 native probes and 78,848 software
bfloat16 checks. All six super boot runs and sixteen additional language/
context runs pass: 78 current-compiler image runs overall.

Artifacts: `logs/compiler-coordination/tvx-narrow-memory-2026-11-20/`.
`comparison.json` records the old/new timings; `super/` contains all three
image pairs and UART validation results. Use `fpu-super-t9000tvx-link.btl`
for ROM link boot with TVX, or its `-direct.btl` counterpart for T4's direct
loader. The TVX payload is 212,216 bytes and fits the 256 KiB test RAM.

## TVX bfloat16 arithmetic — 20 November 2026

Added 1,116 independently checked arithmetic probes to the T9000+TVX super
image: 528 packed pairs each for BF16ADD2 (#1CE) and BF16MUL2 (#1CD), plus
30 BF16DOT (#1C9) vectors tested at aligned and unaligned addresses.
Coverage includes both lanes, signed zeros, subnormals, half-ULP ties,
overflow, infinities, signalling/quiet NaNs, cancellation, sequential
(non-fused) accumulation, zero/odd lengths, and random seed 329.

`generate_tvx_bf16.py` builds a host SoftFloat oracle from the supplied source
without changing it. Binary32 additions/multiplications are independently
computed; packed results are narrowed with integer nearest-even rounding.
The checked-in header and JSON record inputs, expected results and hashes.
The super builder verifies the header hash. Arithmetic NaNs are checked as
quiet NaNs: payload/sign selection currently differs between host TVX and
the target software FPU. This does not weaken the existing exact-bit NaN
conversion tests. Arithmetic exception flags and trap behaviour are not
claimed by these tests.

Every arithmetic check emits a TVX-TIME UART record, measured before output
and verification. The validated runs use `instructions`, not silicon cycles;
a compound TVX operation counts as one guest instruction, so these readings
must not be treated as the hardware execution cost of a dot product. Input
preparation is outside the interval; calls and result staging are included.

All 94 image runs pass: 56 binary16/bfloat16 conversion runs (528,384 checks
per format), six super runs, four standalone arithmetic runs comparing
software and TVX, sixteen language/context runs, and twelve mixed-operation
regressions (SIMD, vector/GEMVBF, Q8DOTBF, block rotation, vector conversion,
and QGEMM retained activations). The opcode audit confirms the three TVX
instructions in compiled wrappers, and their absence in software wrappers.
Four deliberately corrupted UART logs are rejected by the new validator.
No TinyCC lowering or T4 arithmetic defect was found in this coverage.

The TVX super payload is 225,964 bytes, still within the 256 KiB test RAM.
It passes 1,023 native FP probes, 132,096 bfloat16 conversion checks and
1,116 arithmetic checks per boot. T805 and non-TVX T9000 images retain their
previous hashes. Artifacts, UART logs and hashes:
`logs/compiler-coordination/tvx-bf16-arithmetic-2026-11-20/validation.json`.

Reproduce the standalone arithmetic checks with `TCC`, `NETLOAD`, `T4_BIN`
and `TVX_ARITH_OUT` set, then run `bash transputer/tests/fpu-super/run_tvx_bf16.sh`.
The normal super build includes the arithmetic probes in its TVX variant.
This checkpoint covers the core bfloat16 arithmetic instructions; mixed
quantization/graphics kernels have regression coverage, not an exhaustive
SoftFloat edge audit of every opcode or every input combination.

## Exact TVX bfloat16 NaNs — 20 November 2026

T4 commit `5057020` and the matching software runtime now use the same
integer-bit NaN selection before performing BF16ADD2, BF16MUL2 and BF16DOT
arithmetic. The first NaN in logical operand order wins; preserve its sign
and payload, then set the quiet bit. Invalid operations without a NaN
operand return positive canonical `0x7fc00000` (bfloat16 `0x7fc0`).

Dot products apply this rule at each multiply and sequential add, then at
`acc + sum`. Thus an incoming NaN accumulator wins over a NaN sum, including
all low binary32 payload bits. This is a TVX result contract, not a change
to native T9000 NaN tables or a promise of matching exception flags/traps.
Mixed quantization/graphics kernels are outside this rule's current scope.

The generator uses an independent integer NaN-policy overlay around the
SoftFloat finite arithmetic oracle. The super and standalone validators
now require exact bits, with no NaN-class exception. Coverage grows to
1,036 packed pairs (each add and multiply) and 58 dot vectors (each aligned
and unaligned): **2,188 arithmetic checks**. Every one of the 254 bfloat16
NaN encodings is exercised in both lanes/operand positions. Dot vectors
also test empty input, competing NaNs, generated-invalid results and
binary32 accumulator payloads below bfloat16 precision.

All **94 image runs pass**, including both conversion suites, all six super
boot runs, four standalone arithmetic runs, sixteen language/context runs
and twelve mixed-operation regressions. Software and TVX arithmetic outputs
are bit-identical. The previous T4 fails 54 checks with the same new image;
the validator rejects deliberate sign, payload and quiet-bit mutations.
The initial standalone launch was retried after the compiler build finished.

The T9000+TVX super payload is 237,732 bytes within the 256 KiB guard. It
runs 1,023 native FP probes, 132,096 conversion checks and 2,188 arithmetic
checks per boot. T805 and standard T9000 super hashes remain unchanged.
UART timings are instruction counts excluding output/checking, not hardware
cycles; compound TVX opcodes still count as one guest instruction.

Artifacts and exact hashes:
`logs/compiler-coordination/tvx-bf16-nan-2026-11-20/validation.json`.
Use `super/fpu-super-t9000tvx-link.btl` for ROM link boot, or the corresponding
`-direct.btl` for T4's direct loader. T4's detailed contract is recorded in
`transputer/t4/TVX-BF16-NAN.md`; the vendored TVX headers pass the sync check.

## TVX bfloat16 flags and traps — 20 November 2026

T4 `cd542c9` extends BF16ADD2, BF16MUL2 and BF16DOT to the existing T9000
precise FP checkpoint. Flags accumulate across lanes/stages and bfloat16
narrowing. Nontrapping completion publishes sticky flags; trapping restores
original integer/FP operands and status, with the existing exception priority.
The shared checked helpers preserve the established exact NaN result policy.

The software runtime now saves status, masks intermediate traps, accumulates
flags and uses architectural scalar FP operations to deliver an enabled
exception before returning. It preserves existing sticky flags and handler
changes to enables. Repeated retries drain competing exceptions in priority
order. TinyCC adds T9000-only `__ldflags()` / `__stflags(value)` builtins and
`__T9000__` for this runtime path. The status builtin is rejected for T805;
older runtime targets keep their native fallback exception behaviour.

Validation uses nearest-even arithmetic: invalid, overflow, tiny/inexact
underflow and inexact, including narrowing exceptions, with FPError for
NaN/infinity operands. Exact tiny results alone do not signal underflow.
All 2,188 arithmetic cases now require exact result AND status bits against
independent SoftFloat finite flags plus the explicit TVX NaN/FPError policy.
NaN sign/payload checks remain exact. Twenty-three additional generated
scenarios test enabled traps, aggregate priority, competing lane exceptions,
retry operands, clearing one enable at a time, sticky flags and fptesterr.
The software fallback passes the same scenarios in separate timed images.

All **96 image runs pass**: 56 conversion runs, six super runs, four arithmetic
runs, two software trap runs, sixteen language/context runs and twelve mixed
TVX regressions. The prior T4 fails 2,090 checks with the new arithmetic image.
Flag mutations are rejected by the UART validator. The 50 architectural plus
six compatibility FP opcode audit and vendored-header sync check pass.
T805 and non-TVX T9000 super hashes remain unchanged.

The TVX super now has 1,023 native FP probes plus 23 exception/status probes,
132,096 bfloat16 conversions and 2,188 arithmetic result/flag checks. Its
236,156-byte payload fits the original 256 KiB guard. Repetitive NaN inputs
are generated procedurally, with full independent JSON expectations retained;
flags in stored packed vectors are compressed. No cases or guard space were
removed. UART timings exclude output/checking and are instruction counts,
not the latency of compound TVX operations in hardware.

Important ABI scope: a software trap occurs at an internal scalar raise
instruction, whereas native TVX traps restore opcode operands. Retrying after
changing enables is tested on both paths; a handler that skips or substitutes
an operation does not have an identical saved-register ABI. These are new
L-process trap tests. The existing protected route is reused in T4, but Linux
and new P-process trap stress tests were not run in this checkpoint.

Artifacts: `logs/compiler-coordination/tvx-bf16-flags-2026-11-20/validation.json`.
Updated images are under `super/`; use `fpu-super-t9000tvx-link.btl` for ROM
link boot and `-direct.btl` for T4's direct loader. Reproduce software trap
checks with TCC, NETLOAD, T4_BIN and TVX_TRAP_OUT set, then run
`bash transputer/tests/fpu-super/run_tvx_traps.sh`.

## Protected-process TVX traps — 20 November 2026

Twenty P-process scenarios now run independently on software and TVX paths,
through direct and link boot: **80 timed checks pass**. The probes verify
exception type, zero error-trap reason, P-mode in saved STATUS, no premature
sticky-flag publication, native integer operands, and retry from PDS.Eptr.
They include aggregate priority and successive invalid/overflow/inexact
traps with one enable cleared per retry. A pre-existing divide-by-zero sticky
flag survives throughout. Successful completion returns through syscall
(reason 8) with the expected result and flags in both the P process and PDS.

The supervisor saves FP context with fpstall, overwrites the live FP stack,
then restores it with fpldall before goprot re-entry. Deliberately replacing
fpldall with POP makes the dot/inexact case fail, proving that restoration
is exercised. Each protected process has its own test workspace. Identity
region descriptors allow the test harness memory; these are protected trap
and retry tests, not a new test of inter-process address-space isolation.
Software traps remain at the internal scalar raise site; retry, not skipping
that instruction, is the supported common contract.

A fresh **single 32 MiB Linux boot** on T4 `cd542c9` passed pthread/libc,
epoll/POSIX timers, timer-handler flags, CPU-bound timeslice delivery, two
signal workers, nested two-thread FP/MOVE2D handlers, two-context CoreMark
and the desktop. CoreMark validated both contexts' CRCs (60.332553 iterations/
second for this run, not a performance comparison). Files opened and closed,
and restored desktop pixels match the initial frame outside the clock.
The restored frame was also visually inspected.

This boot reused the validated kernel image from Linux `4726b24b92be`;
Linux and T4 implementation changes were unnecessary. Protected TVX probes
ran as standalone BTLs before the Linux regression boot, not as new Linux
applications. No kernel rebuild or Linux TVX signal stress is claimed.

Artifacts, UART logs, framebuffer captures and image hashes:
`logs/compiler-coordination/tvx-protected-2026-11-20/validation.json`.
The new protected images are `software/link.btl` and `tvx/link.btl`, with
matching `direct.btl` images. UART timings use instruction counts and include
supervisor save/restore/retry work, excluding UART output and C verification.

Reproduce with TCC, NETLOAD, T4_BIN and TVX_TRAP_OUT set, then run
`TVX_PROTECTED=1 TVX_TRAP_BACKEND=software bash transputer/tests/fpu-super/run_tvx_traps.sh`
and repeat with `TVX_TRAP_BACKEND=tvx` and a separate output directory.
Leaving TVX_PROTECTED unset retains the existing L-process trap suite;
its 23 cases also pass again through both boot formats.

## Linux TVX SIGFPE retry — 20 November 2026

Linux commit: `3d6e776a8f96`.

Linux now delivers precise T9000 FP error traps as SIGFPE, with the matching
FPE_FLTINV/DIV/OVF/UND/RES code and the faulting PDS.Eptr in both si_addr and
the saved instruction pointer. Aggregate FPError maps to FPE_FLTINV. Other
trap types keep their existing handling. Signal handlers start with FP trap
enables masked; sigreturn restores the saved enables unless the handler
edits its ucontext. FP and block-move register images retain the existing ABI.

The baseline kernel terminated both protected workers with cause 4 at their
first enabled FP trap. The fixed kernel completes **144 retries per backend**
(288 total) from overlapping software/native TVX pthreads. Nine cases cover
packed add/multiply and dot, NaN payloads, aggregate/invalid, overflow,
underflow and inexact. Tests verify exact results, pre-existing sticky flags,
no premature flags, si_code/si_addr, protected STATUS and successful retry
after clearing enables. Handlers do FP work and change flags; both sigreturns
must preserve the interrupted operation. A start barrier and per-round yields
keep the workers overlapping. The final run delivered 10 timer signals to the
software worker and nine to native TVX, with zero errors.

The rebuilt kernel and initramfs passed one **32 MiB T4 -s9 boot** with libc
stress, epoll/POSIX timers, CPU-bound and nested two-thread FP/MOVE2D timer
handlers, two-context CoreMark (both CRCs valid), and the desktop. CoreMark
reported 62.539337 iterations/second; this is a validation result, not a speedup
claim. Files opened/closed and restored pixels matched outside the clock;
framebuffer captures were visually checked. This is one emulated CPU with two
protected pthreads, not an SMP throughput test. TVX/timers overlap; CoreMark
and desktop follow in the same boot.

Mode matters: the older desktop runner used `-s806`. Its Linux smoke results
do not establish T9000 FP trap behavior. This checkpoint explicitly uses `-s9`.
T4 and TinyCC required no implementation changes; the fix is in Linux's
protected trap/signal path. Software traps still occur at the internal scalar
raise instruction, so retry is the common contract, not instruction skipping.

Build the guest with `transputer/tests/build_protected_tvx.sh`, setting TCC,
ELF2FLT, UCLIBC, LIBTCC1, PTHREAD_TEST_DIR and OUT. PTHREAD_TEST_DIR supplies
the tested startup/syscall objects, libc stress object, static_pthread main
built with TP_TEST_EVENT_TIME and TP_TEST_DESKTOP_INTEGRATION, and libpthread.
The builder compiles the same operation wrapper with -mt9000 and -mt9000tvx,
links the current software TVX runtime, and emits OUT/init and OUT/probe.elf.
Install that init in a rootfs containing two-context CoreMark and fbwm, then
rebuild Linux. TP_TEST_TVX enables this test in protected_event_time.c.

Artifacts and reproducible build/boot scripts:
`logs/compiler-coordination/tvx-linux-2026-11-20/validation.json`.
The boot image is `candidate/vmlinux.btl`; UART and framebuffer captures are
under `candidate/play`. Kernel checkpatch reports zero errors/warnings. The
build retains three existing modpost section-mismatch warnings from baseline.
The kernel contribution and tests were developed with Codex assistance.
