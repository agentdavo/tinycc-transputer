# Store double arithmetic directly to memory — 10 November 2026

A double result assigned to an array element, a member through a pointer or
a global is now stored by the FPU at the destination's parked address. It was
stored to a temporary pair and copied a word at a time. Complete rebuilt POV
box, basicvue and glass images match their references with both compilers;
Doom, CoreMark and the raytracer also pass.

## Why this one

The 10 November re-profile of the full POV box render found current `main`
instruction-for-instruction identical to the 6 November slot-unit candidate
(1,592,403,106 instructions, 5,091,332,488 modeled T800 cycles), and the
executed `ldl; ldl; stnl` triples at 4.2% of its cycles. The 6 November
local-store rule covered locals and parameters only; `answer_array[i] = ...`
in `MInverseTransformVector` still took thirteen instructions per store.

## Change and safety boundary

```text
before: ldlp x; fpstnldb; [ldl x+1; stl h;] ldl x; ldl p; stnl 0;
        ldl p; ldc 4; bsub; stl p; ldl h; ldl p; stnl 0
after:  ldl p; fpstnldb
```

`tp_stmt_double_store_mem` runs inside the statement-end history window,
beside the local rule. It requires:

* the exact ten- or twelve-instruction copy above as the whole remainder of
  the window, so no address evaluation, call, branch or FPU operation
  intervenes and nothing later in the window reads the pair, `h` or `p`;
* `x`, `x+1` and the optional high-word copy `h` in the float register slots
  and absent from the value stack;
* `p` an integer register slot absent from the value stack. The copy leaves
  `p` advanced by four; the rewrite leaves it unchanged, and it has no reader.

The same `fpstnldb` rounds and stores the same result once; only its
destination changes. Both words go to the addresses the two `stnl`s wrote,
each written once. No arithmetic is reordered and no value is kept across a
call. Packed and odd-address destinations use byte stores and never match.
T414/T425 soft-float code has no `fpstnldb` and is unchanged.
`TCC_TRANSPUTER_DBL_MEM_STORE=0` keeps the copy for an A/B run.

Not covered: a memory-to-memory double copy (`r->x = t[0]`), which has no FPU
result to redirect and still copies two words.

## Measured real-renderer gain

Baseline: `main` at `6e2f6d81`. Both private compilers use the same runtime
archives, libc and isolated emulators as the 6 November acceptances.

| Complete generic POV box | Baseline | Candidate |
| --- | ---: | ---: |
| Decoded instructions | 1,592,403,106 | 1,457,506,593 |
| Modeled T800 cycles | 5,091,332,488 | 4,848,890,928 |
| Executed prefix bytes | 983,238,790 | 952,199,454 |
| Linked ELF bytes | 349,204 | 340,852 |

The reference-exact render saves **134,896,513 decoded instructions (8.471%)
and 242,441,560 modeled T800 cycles (4.762%)**; the ELF shrinks 8,352 bytes.
Every FPU opcode total is unchanged (`fpstnldb` 25,485,812 both). `stnl`
falls 64,099,334 → 45,038,568, `ldl` by 57.3 M and `stl` by 29.0 M. `rev`
rises by 325,996 and `eqc` by 100,429 where the later scheduler pass sees
different code; both are inside the totals above.

| Routine | Fewer modeled cycles |
| --- | ---: |
| MInverseTransformVector | 75,260,568 |
| MInvTransVector | 60,329,092 |
| Create_Ray | 44,236,800 |
| do_light | 18,679,794 |
| Colour_At | 8,894,540 |
| Sphere_Normal | 8,609,964 |
| Compute_Reflected_Colour | 4,820,592 |
| Ambient | 4,720,163 |

Profiling disables emulator instruction combination and uses decoded
single-node counts with the T800 timing table. These are model results, not
native hardware timings or sixteen-node scaling measurements.

## Validation

* **Fixtures:** the new nineteen-case `test_double_mem_store.c` (indexed,
  member, global, aliased and self-referencing destinations, `*p++`, chained
  and value-used assignments, a pending statement expression, for-step,
  branches, loops, the POV matrix shape, volatile, packed and odd-address
  destinations, signed zero, neighbouring words, 2-D arrays) with
  `test_fp_neg_store` and nine retained suites passes baseline/candidate ×
  four CPUs × two layouts: **176 executions**. The normal UART runner passes
  eleven fixtures with the candidate at `-mt800` and `-mt9000`, including
  every existing test whose object changes.
* **Object identity:** of the local tests and c-testsuite sources, all 449
  (`-mt414`) and 453 (`-mt425`) objects are byte-identical; seven change at
  `-mt800` and at `-mt9000`.
* **Mixed-version ABI:** 32 runs. **VLA return:** eight. **Generated
  expressions:** both compilers on four CPUs, 384 bit comparisons.
* **Doom:** hardened phase hashes and `check 0 0` on T800/T9000; the paired
  outputs are identical. **CoreMark:** CRC sequence
  `e9f5/e714/1fd7/8e3a/e714` validated with both compilers on both CPUs.
* **Raytracer:** four reference images at one/four nodes with each compiler,
  sixteen exact comparisons.
* **POV-Ray:** box, basicvue and glass match their 640×480 references with
  both compilers (recovered INMOS RNG, buffered rows), six exact comparisons;
  the two profiled box runs match as well.

| Scene | SHA-256, both versions and reference |
| --- | --- |
| box | `fcaeaccaea31d263bede7a2f8774e2e7af8dcfc6f640a893a3fd1d3b8d1d6db0` |
| basicvue | `d537ba4e52842f8cd3e35f8c59bd3d30ac2c3008b60eafc10f7b6ff0cf17cde7` |
| glass | `79a805d547b93c68fc81c16e2c476ea09eed23aab17e091712fc8f0d1c9ef54c` |

The full c-testsuite and the full local suite were not rerun; the object
comparison shows which of their programs change.

## Found on the way, not fixed here

At `-mt425`/`-mt414`, a packed double used as an arithmetic operand through a
pointer (`p->d * 2`, `p` a `struct __attribute__((packed)) { char c; double
d; } *`) is read with `ldnl 0`/`ldnl 1` at the odd address, which reads the
containing words. Plain loads and stores of the member use bytes and are
correct. The baseline has it too; the new fixture skips its packed case on
soft-float parts until it is fixed.

## Reproduction evidence

| Frozen input | SHA-256 |
| --- | --- |
| Baseline compiler | `652053443219663b6fb1d98a037b9ed31c5da33fa9e18fdaa365322c301aaace` |
| Candidate compiler | `83dadefca105c4c3f2035e171ec7f25e53393afdfbcf2fbe2a5f7b797dc37767` |
| Candidate backend | `6af8957d8f88fcbf1c9c706c5ede65adc007cf77fa47fd65877b4fd715d5be37` |

Scripts, compilers, images, logs and decoded profiles are retained in
`logs/compiler-coordination/double-mem-stores/`; the re-profile is in
`logs/compiler-coordination/reprofile-2026-11-10/`.
