# Bfloat16: SoftFloat, TinyCC software conversions and TVX

This bare-metal suite tests actual `__bf16` C casts compiled with `-mt9000`
and `-mt9000tvx`, separately. Standard T9000 has no bfloat16 instructions:
widening uses an integer shift and narrowing calls `__truncsfbf2`. TVX is
an optional extension with BF16TOR32 and R32TOBF16 instructions.

The oracle uses the supplied SoftFloat fork's **root headers and sources**,
which include bfloat16 and select RISC-V/default-NaN behavior. Its separate
upstream-style `include/` and `8086-SSE/` directories do not contain this
bfloat16 API. Sources are copied into a temporary directory for compilation;
the supplied tree is not modified. No host floating-point arithmetic is
used to construct inputs or expected values.

## Coverage and interpretation

- All **65,536** bfloat16 encodings are widened by SoftFloat and by each
  target path. The generator checks every reference result before using
  its compact equivalent in the target's expected values.
- **13,312** binary32 narrowing inputs span both signs and every exponent,
  even/odd retained significands, zero/one discarded bits, just below/at/
  above ties, maximal discarded bits, plus 1,024 seed-329 random patterns.
  This includes subnormals, overflow, infinities and 47 NaN inputs.
- Narrowing uses round-to-nearest-even, the documented TinyCC/TVX contract.
  It does not claim support for dynamic directed bfloat16 rounding modes.
- Finite values and infinities compare bit-for-bit with SoftFloat.
  **NaN policies differ:** the supplied oracle produces canonical NaNs;
  TinyCC/TVX widening preserves all input bits, while narrowing preserves
  the retained sign/payload and sets the quiet bit. All 254 widening NaN
  encodings are checked under this explicit policy (253 raw outputs differ
  from the canonical oracle). Reference results and exception flags are
  retained in `reference.json`; NaNs are not silently discarded.
- SoftFloat exception flags are recorded, not imposed on these integer/TVX
  conversions, which do not implement the SoftFloat flag interface.
- The validator decodes the compiled `widen`/`narrow` function bodies,
  requiring the respective TVX opcodes in the TVX build and excluding TVX
  opcodes from the software build. This avoids accidentally testing the
  same backend twice.

Seven image parts keep tables small and retain the 256 KiB bootstrap layout.
Part 0 runs exhaustive widening and its narrowing partition; parts 1–6 run
the remaining narrowing vectors. Run **all seven** parts for complete coverage.
Each part has direct-loader and executable link-bootstrap variants, using
the same format as `../fpu-super`. Only `link.btl` is for link-0 streaming.

UART reports the path/part, mismatches with exact input/result/expected bits,
256-conversion batch timings and a strict final count. Timing uses T4's
instruction counter through `ldtimer` with `T4_EST_CYCLES=2`. Conversion
loops, calls and result staging are measured; comparisons and UART output
are outside the interval. These are **instruction counts, not silicon cycles**.

## Reproduce

From Linux/WSL:

```sh
TCC=/absolute/path/transputer-tcc \
NETLOAD=/absolute/path/transputer-netload \
T4_BIN=/absolute/path/t4 \
BF16_OUT=/absolute/path/results \
SOFTFLOAT_SOURCE=/absolute/path/transputer/softfloat-3e \
bash transputer/tests/bf16-softfloat/run.sh
```

This requires the supplied fork, GCC, Python and the built Transputer tools.
UART is staged on the Linux temporary filesystem to avoid byte-at-a-time
Windows-mounted-file overhead. Results are copied to `BF16_OUT` before
validation. Emulator exit 255 on the final parked process is normal; the
validator requires complete counts and a zero-failure UART summary.

## Validated checkpoint: 19 November 2026

With compiler `d87fecd9` and T4 `1d88eaf`, all **28 image runs** pass:
**315,392 conversion checks**, comprising 78,848 inputs per backend per
boot format. Direct/link instruction counts agree. No compiler/runtime or
emulator fix was required.

| Measured batches, one boot format | Software | TVX |
| --- | ---: | ---: |
| 65,536 widening conversions | 2,033,920 instructions | 2,230,528 instructions |
| 13,312 narrowing conversions | 745,710 instructions | 506,376 instructions |

TVX narrowing reduces this instruction total by about 32%; widening increases
it by about 10%, reflecting the current compiler's surrounding conversion
sequence. This is a useful future compiler optimization target, not a claim
about hardware opcode latency.

Artifacts: `logs/compiler-coordination/bf16-softfloat-2026-11-19/`, including
`software-0/` through `software-6/`, `tvx-0/` through `tvx-6/`, reference
source hash, raw reference results/flags, BTLs, UART logs and `validation.json`.

## TVX widening optimization — 19 November 2026

`gen_cvt_bf16_to_float` now writes BF16TOR32's integer result straight to
the floating-result workspace slot. The previous sequence stored to scratch,
loaded through the FPU and stored again. The software shift path already
used the direct store; the two paths now share this implementation. No
floating-stack operation or reset is needed for this bit conversion.

The exhaustive suite again passes all 315,392 checks across 28 image runs.
TVX widening drops from **2,230,528 to 1,968,384 instructions** over 65,536
conversions: four instructions removed per conversion, about 11.8% of the
measured workload. Software widening remains 2,033,920; software/TVX narrowing
remain 745,710/506,376. Each conversion removes a temporary workspace store,
an FP memory load and an FP memory store, replacing them with one direct
result-slot store (two fewer memory accesses overall).

The validator now rejects FP load/store instructions in this integer-result
widening function. It rejects the old image and accepts the optimized one.
The separate existing bfloat16 language test passes all 31 checks on both
backends in both boot formats, including arithmetic, compound assignment,
parameter/return, structure and array use. T4 `1d88eaf` is unchanged.

Artifacts: `logs/compiler-coordination/bf16-widen-opt-2026-11-19/`.
All seven parts per backend are required, as above; `language/` contains the
four language-regression runs and `old-code-regression.log` records detection
of the former redundant sequence. Instruction savings are compiler-sequence
measurements, not hardware cycle predictions.

## Binary16 and integrated software bfloat16 — 19 November 2026

Set `FLOAT16_FORMAT=f16` to run the same infrastructure for IEEE binary16
using actual `_Float16` casts, `f16_to_f32`/`f32_to_f16` SoftFloat references,
and TVX F16TOR32/R32TOF16. The default remains `bf16`. Binary16 is not
bfloat16: it has five exponent bits and ten fraction bits. All 65,536
binary16 patterns and 13,312 narrowing inputs are tested. The oracle checks
every widening value against the compact integer expected-value function,
including subnormal normalization. The 2,046 NaN input encodings follow
the same explicit canonical-oracle versus payload-preserving target policy.
UART identifies this suite as `F16`, with the format also in the path label.

TVX binary16 widening now stores its integer result directly into REG_FRET,
removing the redundant scratch/FPU roundtrip. The exhaustive widening
workload drops from 2,230,528 to **1,968,384 instructions**, four saved per
conversion. The software binary16 totals are 4,396,976 widening and
1,029,803 narrowing; TVX narrowing is 506,376. These include loop/call/result
staging overhead, not checking or UART. The bfloat16 totals remain unchanged.
Both formats pass all 28 image runs each (630,784 checks combined). The
binary16 language test passes 30 checks on both paths and both boot formats.
No T4 changes were needed.

The generator also emits `super-vectors.h` and `super-reference.json` for
bfloat16, containing the complete narrowing corpus in compact input/result
arrays, reference provenance and the header hash. These generated files are
committed so ordinary super-BTL builds do not require the SoftFloat tree.
To regenerate, run `generate.py --format bf16` with the supplied source tree,
then copy the two `super-*` files from the output directory into this folder.
The super builder verifies their hash before use.

The T805 and T9000 super images now include all 78,848 **software** bfloat16
checks by default. They do not execute TVX instructions. See the super-suite
README for the separate native-FP and bfloat16 summaries and final combined
acceptance marker. Standalone TVX validation remains separate.

Artifacts: `logs/compiler-coordination/f16-widen-super-2026-11-19/`, with
`f16/`, `bf16/`, `before/`, `language/`, `super/` and `validation.json`.

## Binary16 NaN contract for RTL acceptance

The runtime and TVX **do not canonicalize NaNs**. For a binary32 NaN input
`x`, narrowing is exactly:

```c
((x >> 16) & 0x8000) | 0x7e00 | ((x & 0x007fffff) >> 13)
```

This keeps the sign and retained payload bits and forces the binary16 quiet
bit (0x0200). For example `0xff812345` becomes `0xfe09`. For a binary16
NaN input `h`, widening is exactly:

```c
((h & 0x8000) << 16) | 0x7f800000 | ((h & 0x03ff) << 13)
```

Widening preserves signalling state; `0xfc01` becomes `0xff802000`. These
integer/TVX conversions do not update the T9000 IEEE exception flags. The
reference JSON's `softfloat` field records the supplied fork's canonical
NaN result; **`target` is the exact narrowing acceptance result for runtime
and RTL**. Widening reference compression is checked for all 65,536 inputs,
with the documented raw-payload policy applied on the target. Canonical
oracle answers must not be substituted for payload-preserving RTL results.

## Workspace narrowing optimization — 20 November 2026

Both TVX narrowing functions now share `tp_gen_tvx_float_narrow`. Aligned
binary32 locals and pseudo-register workspace slots are read as integer
bits directly, without loading the FP stack or storing/reloading scratch.
Constants, indirect and packed operands keep the existing staging path.
A pending REG_IRET value is spilled before selecting the input, and direct
loads invalidate the integer-stack tag rather than reusing a volatile read.

Each 13,312-input TVX narrowing workload drops from **506,376 to 453,128
instructions**: four saved per conversion, about 10.5% of the measured
workload, with two fewer memory accesses per optimized conversion. Widening
and software-path timings are unchanged. The emitted-code audit rejects
the old narrowing FP roundtrip and accepts the direct workspace load.

All 56 standalone conversion image runs pass (630,784 checks). Both super
BTLs pass through both boot formats, including all software bfloat16 tests;
they are byte-identical to the previous non-TVX super images. Sixteen
additional runs cover both formats/backends/boot paths with the existing
language suites and `test_tvx_narrow_workspace.c`. The new 12-check context
test covers volatile updates, alias reloads, packed fields, nested calls,
arithmetic results and signed zero. In total **76 image runs pass**.

Artifacts and hashes: `logs/compiler-coordination/tvx-narrow-workspace-2026-11-20/`.
These are instruction-count improvements including loop/call/result staging,
not hardware-cycle claims. RTL acceptance must measure cycles with memory
latency separately. No T4 semantic change was needed for this optimization.

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
