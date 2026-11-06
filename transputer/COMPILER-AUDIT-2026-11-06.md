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

The review session owns a conservative purity-classification fix and packed
call/memcpy regression. Check all consumers of the predicate, not only memcpy;
preserve aligned fast paths, volatile access counts and scratch-slot safety.
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
raytracer session, which owns scheduler changes, has been asked to investigate
reachability and conservatively veto readers a rewrite cannot redirect.
[Harness](../logs/compiler-audit-2026-11-06/middle-copyprop-probe.c) ·
[preconditions/invocation](../logs/compiler-audit-2026-11-06/middle-copyprop-probe.txt) ·
[exact output](../logs/compiler-audit-2026-11-06/middle-copyprop-probe-output.txt).

## Next performance experiments

None of the savings below is a measured workload improvement yet. Measure
instruction/encoding changes first, then fixed-work guest cycles, hashes/CRCs
and frame cost against a matching baseline.

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
Backend shift staging is distinct from the already-landed runtime helper
improvement `8e88385c`.

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

* T800 INT64→REAL32 currently has an acknowledged double-rounding difference
  above 2^53. The regression accepts either adjacent result for a chosen case.
  This is not a newly established unconditional C violation. If a correctly
  rounded cross-target contract is required, evaluate the prescribed TCWG
  `fpunoround` conversion sequence or an exact helper and tighten the oracle.
* `tp_gaddr_cache` changes frame bookkeeping before later relaxation bailouts.
  The audit did not establish emitted-frame corruption; transactional state
  cleanup remains a lower-priority question.
* Broader memset/fill lowering needs a costed design, preserving overlap,
  volatility, device access and return semantics. MOVE2DZERO is not memset.
* Do not re-propose existing mint selection, narrow stores, indexed FPU loads,
  explicit builtin CPU categories, indexed-copy scheduling or the call-staging
  remapper as missing features.

## Delivery order

Land the packed-load correctness fix first, investigate the scheduler
counterexample with its current owner, then take one contained performance
experiment per owner. Refactors should preserve objects/diagnostics before
changing instruction selection. Record a separate validated commit for each
group; rejected trials should not be merged merely to keep commits frequent.
See [coordination](COMPILER-COORDINATION.md) for ownership and
[protection review](LINUX-PROTECTION-REVIEW-2026-11-06.md) for the separate
trap-syscall/isolation blockers.
