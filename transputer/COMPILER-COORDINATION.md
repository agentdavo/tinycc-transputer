# Compiler coordination — 6 November 2026

This is a working ownership and evidence record for the active TinyCC
Transputer sessions. Check current source, commits and session workloads before
starting another change. `STATUS.md` contains historical baselines; it does not
describe the current compiler or the active `linux-7` kernel.

## Current ownership and next avenues

| Session title | Owned work | Next evidence to collect |
| --- | --- | --- |
| Explain per-architecture libtcc1 builds | Runtime selection and per-target libc links; T414 userspace image placement | Fix the reserved syscall-gate/image overlap using link placement/provenance, not an address-value exemption; then CoreMark component profiles |
| Review and implement afcc4dfe | Runtime division, its regression/benchmark, review findings; MOVE2D semantic coverage | Signed/unsigned boundaries and cycle estimates; non-square, padded and conditional 2D transfers |
| Compare raytracer binaries | Frame address/constant cache, binary32 literals, `TransLocal` copy-loop experiments | General three-element copy reproducer, image equality and modeled cycles; preserve alias/volatile semantics |
| Refactor transputer code layout | Builtin descriptors, operand staging and target diagnostics | Explicit availability metadata replacing table-position gating; equivalent output/diagnostics on six targets |
| Build per-part uClibc | Doom workspace remapping in the separate `doom-frame-layout` worktree; rectangular-copy experiment | Call-frame/implicit-slot safety, CoreMark CRCs and Doom hashes; integrate without overwriting root changes |
| Optimize 64-bit division | Currently glyph-fix integration and Linux L/P isolation review, despite the historical title | Review task-private trap ABI, state lifetime, scheduler/fault handling, broad maps and SPINE/T4 differences; propose trap-default acceptance gates |
| Improve TinyCC Transputer compiler | Coordination, assembler/linker work, protection docs and full backend audit | Three read-only audit agents cover snapshot lines 1–4700, 4701–9400 and 9401–14025; collect session lessons and rank concrete proposals |

The [full-file audit](COMPILER-AUDIT-2026-11-06.md) is complete. The review
session has priority ownership of its reproduced packed-memcpy purity defect;
the raytracer session is investigating an indirect-reader copy-propagation
counterexample; the CoreMark session is testing narrowing-cast spill removal.
Keep those functions separate from other experiments.

Several sessions share one checkout and Git index. Use private source snapshots
for experiments, keep intermediate shared edits buildable, and commit only
owned changes. Inspect the staged diff immediately before committing. Separate
maintenance refactors, semantic fixes and performance experiments into reviewable
commits. A session's title can lag behind its current workload.

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
* The [Linux isolation review](LINUX-PROTECTION-REVIEW-2026-11-06.md)
  reproduced a trap-first PID 1 panic after nested spawn reused its PDS/RODS
  state for a child. Its C-call control reached `DESKTOP_LOOP`. Parent
  continuation preservation is a release blocker for changing the default.
  The current protection host suite was rechecked and returned ALL PASS;
  that does not validate nested Linux sessions or strict isolation.
* Current code includes broad quarter mappings, a Q3 stack mirror, and
  non-SPINE T4-specific cause/leave-P mechanisms. Entering P-mode alone does
  not prove complete process isolation or hardware portability.

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
