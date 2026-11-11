# Compiler coordination — 6 November 2026

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
(more cycles). Open: only an escaped object's own words should leave the
range.

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
