# Compiler coordination — 6 November 2026

This is a working ownership and evidence record for the active TinyCC
Transputer sessions. Check current source, commits and session workloads before
starting another change. `STATUS.md` contains historical baselines; it does not
describe the current compiler or the active `linux-7` kernel.

## Current ownership and next avenues

| Session title | Owned work | Next evidence to collect |
| --- | --- | --- |
| Explain per-architecture libtcc1 builds | Direct int-to-FPU change integrated as `16e7e460`; avenue 3 pool profitability investigation | Measured workload binaries match the integration snapshot; reject the redundant dead-initializer trial unless another profitable direction is demonstrated |
| Review and implement afcc4dfe | Seed 329 independently verified in `05b0cc9f`; next, missing MOVE2D transfer timing model | Derive cost/runtime inputs from primary timing tables; coordinate nested T4 ownership and retain target/timing-model distinctions |
| Compare raytracer binaries | Scheduler correctness baseline; avenue 3 loop-weighted slot/cache profitability in `tp_slot_remap` / `tp_gaddr_cache` | Preserve implicit ABI slots, aggregate units, branch targets and relocations; account for initialization/frame costs; leave pool functions to the conversion session |
| Refactor transputer code layout | Shared symbol emitter `3628b7e0`, effect design `51c35b4c`, accumulator helper `265309e9`, word-result table `3783a69a` | Keep effect consumers conservative; migrate narrowly after equivalence checks and coordination with scheduler/slot owners |
| Build per-part uClibc | Scalar64 alignment repair `fc344c0b` landed; rebase and retest separate whole-struct MOVE trial | Rerun fuzz/oracles and matched workload matrix on the repaired baseline, preserving aggregate padding and nested-call behavior |
| Optimize 64-bit division | Linux continuation repair `cbf812242ba1`; frame ABI `bc2969c2ea63` + root stub `a747d3b2`; HRM descriptor/state audit | Replace physical aliasing, preserve FP/block-move state, isolate remapped FP-load defect; trap-first remains opt-in |
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
* Whole-struct MOVE has a promising targeted result, but fuzz seed 329 still
  requires post-repair validation before landing. `fc344c0b` fixes an unaligned
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
* **Whole-struct MOVE:** the independently inspected Doom phase tables and
  five-iteration CoreMark logs match; the raytracer image matches. The seed 329
  alignment repair is committed separately as `fc344c0b`, with a pre-fix-failing
  source regression in c-testsuite `09118` and direct/nested/indirect/packed
  scalar-argument tests. The owner is restoring the aggregate trial on top of
  that fix and will rerun its fuzz and workload gates before committing it.
  A rebased trial matches all three workloads and seeds 329/367, but integration
  is held for a qualifier-loss issue: argument casting removes source volatility
  before backend lowering. Preserve volatile aggregate/member behavior before
  enabling the one-MOVE path.
  Exact compilers, ELFs, relocations, disassembly, host oracle and hashes remain
  preserved in the owner's `codex-struct-results`.
* **Pool initializer trial:** source-level dead initialization is real, but
  the normal scheduler also removes it. Current before/after objects and all
  three workload ELFs are identical; the isolated improvement with scheduling
  disabled is not a default-compiler performance win. Do not merge it merely
  because an earlier pass does less work. Report committed as `c6de60f9`.
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

The Doom session found that the current T4 estimated-cycle path did not charge
the MOVE2D transfer itself. Until corrected and verified, those estimated-cycle
results cannot establish a MOVE2D speedup. Keep padded-row/canary correctness
results, target timing models and host emulator throughput separate.

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
  rounding for normal numbers and zeros, in prescribed INT64 conversion
  sequences that avoid double rounding. It is not general no-round arithmetic.
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
  (ISM ch.13). The Linux owner is preserving a failing remapped `FPLDNLSN`
  case separately from the FP-state preservation investigation.
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
