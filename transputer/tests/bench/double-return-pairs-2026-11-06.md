# Return existing double pairs directly — 6 November 2026

`gfunc_return` previously moved a computed double pair through workspace
slots IRET/IRE2 before loading the integer return registers. It now loads
the actual pair directly, high word first, so the ABI remains A=low, B=high.
For a typical arithmetic result in slots 5/6, the return transfer changes
from six instructions to two:

```text
before: ldl 5; stl 7; ldl 6; stl 8; ldl 8; ldl 7
after:  ldl 6; ldl 5
```

The final FPU store that transfers the result bits into workspace remains.
This removes the subsequent redundant integer copy; it changes neither
floating-point operation order nor the calling convention.

## Eligibility and scope

The fast path requires a matching `double`/`long double` result type and two
distinct raw pseudo-register numbers within the workspace register range.
It uses the actual `r2`, including nonadjacent pairs. Raw-number bounds
exclude lvalues, symbols, constants and deferred cast/bounds flags. Memory
and volatile operands retain their existing materialization. Values saved
in locals across cleanup calls also use that path. Integer, float, long
long and struct returns are unchanged.

The hardware evaluation-stack bookkeeping is reset before loading B/A.
Existing early-return and wide-frame epilogues remain in place. No jump is
introduced with live ABI return words. The existing frame pass can remove
IRET/IRE2 storage when there are no remaining references.

## Accepted workloads

The baseline is the backend accepted in `e2e11475`. Frozen compiler/source
hashes below identify the exact pair, with identical runtime archives and
isolated T4 `2853b29`. Other concurrent repository work is outside this
change.

* **Doom:** all eleven hardened phase hashes pass with `check 0 0` on T800
  and T9000, for baseline and candidate. Each target's paired ELFs are
  byte-identical. This is the extracted phase benchmark.
* **CoreMark:** all four runs retain CRCs `e9f5/e714/1fd7/8e3a/e714`.
  The one-iteration run is a diagnostic, not a published score. Whole-fixture
  modeled cycles improve 5,333,324 → 5,333,186 for T800 code and
  3,539,635 → 3,539,497 for T9000 code, using the same T800 model.
* **Raytracer:** four scenes at one and four nodes match all reference
  pixels for both compilers: sixteen comparisons. The paired ray ELF is
  byte-identical, so no ray performance gain is claimed.
* **POV-Ray:** complete 640×480 box, basicvue and glass renders match the
  packaged references for both compilers: six comparisons. The recovered
  INMOS random generator and buffered RGB rows are fixed across both builds.
  Strict link logs are empty; emulator exits and renderer statistics match.

| Full POV scene | Bytes | SHA-256, both versions and reference |
| --- | ---: | --- |
| box | 921,615 | `fcaeaccaea31d263bede7a2f8774e2e7af8dcfc6f640a893a3fd1d3b8d1d6db0` |
| basicvue | 921,615 | `d537ba4e52842f8cd3e35f8c59bd3d30ac2c3008b60eafc10f7b6ff0cf17cde7` |
| glass | 921,615 | `79a805d547b93c68fc81c16e2c476ea09eed23aab17e091712fc8f0d1c9ef54c` |

POV recompiles the generic renderer and fdlibm with each compiler; existing
libc remains fixed and requires T9000 emulator mode. This gate is the
single-node source fixture, distinct from the historical sixteen-node ICC
BTL. Results below are instruction counts and modeled cycles, not native
hardware timings or array scaling measurements.

## Traffic and size

| Full generic POV box | Baseline | Candidate |
| --- | ---: | ---: |
| Decoded instructions | 1,802,101,877 | 1,801,929,729 |
| Modeled T800 cycles | 5,596,242,656 | 5,595,984,309 |
| Executed prefix bytes | 1,110,311,195 | 1,110,311,070 |
| Linked ELF bytes | 377,052 | 376,652 |

The decoded profile removes **172,148 workspace instructions**: exactly
86,074 `ldl` and 86,074 `stl`. All FPU opcode totals are unchanged. The
258,347 modeled-cycle saving is **0.00462%** of the full box render.
`__ieee754_pow` accounts for 164,084 removed instructions; the remaining
savings are in sin/cos and their kernels. Profiling disables instruction
combination, so decoder dispatch counts are not substituted for decoded
instructions.

For `double horner(double a,double b,double c) { return a*b+c; }`, T800 and
T9000 function sizes shrink 23 → 19 bytes and frames shrink 10 → 8 words.
The double sqrt-composition probe shrinks 59 → 55 bytes on T800 and
40 → 36 on T9000; its float counterpart is unchanged.

A focused 4,096-call T800 leaf kernel improves 500,006 → 475,430 modeled
cycles (**4.915%**). Two 20,000-iteration loop probes change
1,520,371 → 1,520,371 and 980,596 → 980,590. These include timer and label
overhead and demonstrate that a return-path improvement is concentrated at
call boundaries, rather than throughout every loop.

## Return and ABI checks

The new nineteen-case fixture checks arithmetic, external forwarding,
pending nested calls, indirect calls, conditional results, early returns,
integer conversion, long double, distinct low/high halves, float narrowing,
packed/volatile fallback, cleanup calls, wide-frame epilogues, signed zero,
subnormal transport, infinity and NaN payload preservation. The shared UART
runner includes its helper and an exact expected-output oracle.

The new fixture plus the retained double-parameter, scalar64 alignment,
sqrt composition and return-epilogue suites pass baseline/candidate ×
T414/T425/T800/T9000 × link pads 0/1027: **80 executions**. Separately
compiled callers/helpers pass in both directions for both double fixtures
on all four targets and both layouts: **32 interoperability runs**.

Eight further baseline/candidate target runs check computed double returns
out of VLA scopes, nested scopes, early exits and repeated arena reuse.
Volatile-double-only, variadic, integer and float control functions remain
byte-identical on all four targets.

| Frozen input | SHA-256 |
| --- | --- |
| Baseline compiler | `36a5860a650d0eabeede39563dfb3898c58ec2f70bc88f00f78ab758dfadeeee` |
| Candidate compiler | `a0348612c309c4346ad20a893684d55b20892b05f008d0f8b87bdb79bb0cf9b6` |
| Baseline backend | `5bf212b7d7955dd70d6b6192e4d51c4e0cf426420a88f6b4a8b60a1b1acaf792` |
| Candidate backend | `79fa7f78d32b999e81de7a2e92b336e7619e052640e34726f60de25d7ab0016d` |

Frozen sources, scripts, strict logs, complete applications/images,
disassemblies, decoded histograms and the independent acceptance receipt
are retained in `logs/compiler-coordination/double-returns/`. No installed
runtime archive, kernel, historical BTL or original POV math source changed.
