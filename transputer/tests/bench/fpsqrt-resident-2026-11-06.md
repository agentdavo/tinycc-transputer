# Resident arithmetic operands before sqrt — 6 November 2026

The candidate keeps a single-use arithmetic result in FA when the next
consumer is the hardware sqrt builtin. Previously, `__fpsqrt(x*x + 1)`
stored the result to workspace and immediately loaded it again. The float
case also copied the word into another spill slot.

`tp_gen_fpu_unary` now tries the existing `tp_fpu_take_resident` proof before
`save_reg` rewrites the operand. It first rejects any other live value in
the output return slots. The existing helper verifies matching precision,
unique consumption, an unobserved producer store and contiguous safe history;
calls, FP operations, branches and relevant slot accesses prevent cancellation.
The sqrt instruction sequence and final result store are retained. Abs and
other unary builtins are unchanged, as are software-FP paths.

The baseline is the backend at `1993ac15`, built independently from the
candidate snapshot. Intervening documentation commit `a51de099` does not
change code generation. Both compiler snapshots use the same runtime
archives, netloader, source inputs and isolated T4 `2853b29`.

## Correctness and representative workloads

* Expanded `test_fpsqrt_fpabs.c`: 27 checks pass for both compilers on strict
  T414/T425, T800 and T9000, eight complete executions. Coverage includes
  single/double arithmetic-to-sqrt, correctly rounded irrational roots,
  direct call operands, same-function pending return values, reused operands,
  signed zero, infinity, quiet NaN encodings, volatile loads and composition.
* Existing scalar64 alignment regression passes the candidate on all four
  targets with link pads 0 and 1027. This is the retained reduced alignment
  test, not a claim to have rerun the full historical random-seed corpus.
* Hardened Doom passes all eleven phase hashes and `check 0 0`, T800 and
  T9000. Baseline and candidate ELFs are byte-identical for each target.
* One-iteration CoreMark passes seed/list/matrix/state/final CRCs
  `e9f5/e714/1fd7/8e3a/e714`, T800 and T9000. ELFs and measured counts match
  for each target. This is a diagnostic run, not a publishable score.
* All four 256×256 raytracer reference images match byte for byte at one
  and four nodes, for both compiler snapshots: sixteen full comparisons.
* Full rebuilt generic POV-Ray `box` and `basicvue` match their packaged
  640×480 PPMs for both compilers, four complete image comparisons. Both
  strict links have empty diagnostic logs and every render exits normally.

The independent code review found no safety defect and requested the
same-function pending-return cases, which were added and passed. Emulator
checks do not establish native silicon/RTL exception-flag behavior.

## Instruction evidence and size

The probe functions below were compiled for both hardware-FP targets:

```c
float unary_f(float x) { return __fpsqrt(x*x + 1.0f); }
double unary_d(double x) { return __fpsqrt(x*x + 1.0); }
```

| Target / function | Baseline bytes | Candidate bytes |
| --- | ---: | ---: |
| T800 `unary_f` | 56 | 46 |
| T800 `unary_d` | 73 | 67 |
| T9000 `unary_f` | 46 | 36 |
| T9000 `unary_d` | 54 | 48 |

Both now feed the arithmetic result directly into sqrt. T800 still uses
first/two-or-five-step/last; T9000 still uses its native secondary. The float
probe's workspace shrinks by one word. Horner and Whetstone-array probe
sizes are unchanged; the T414 probe object is byte-identical.

| Profiled workload | Baseline instructions | Candidate instructions | Baseline modeled cycles | Candidate modeled cycles |
| --- | ---: | ---: | ---: | ---: |
| Ray scene 3, 256×256, one node | 1,173,588,908 | 1,173,322,949 | 3,639,070,909 | 3,638,538,980 |
| Rebuilt generic POV box, 640×480, one node | 1,844,165,071 | 1,842,145,515 | 5,733,739,803 | 5,728,690,913 |

The ray saves 265,959 instructions and 531,929 modeled cycles (0.0146%).
The full POV box saves 2,019,556 instructions and 5,048,890 modeled cycles
(0.0881%). These are modest compiler gains, not measured hardware speedups.
The generic POV ELF shrinks from 378,316 to 377,868 bytes.

POV's opcode differences are exactly `ldlp -1,009,778`,
`fpstnldb -504,889`, `fpldnldb -504,889`. All other opcode totals match,
including branches, calls and arithmetic. The instruction savings attribute
to `Create_Ray` (1,228,800), `do_light` (401,716), `do_phong` (289,368),
`Box_Normal` (99,612), noise initialization (40) and viewpoint parsing (20).
Scene statistics and image bytes match.

The private profiler disables emulator instruction combination and records
decoded instruction lengths, verified against ELF encodings for the ray.
The timing model is the T800 instruction table with prefix costs. The POV
fixture links an existing T9000 libc, so its entire-model total is not a
T9000 silicon cycle prediction; the removed instructions have defined T800
costs. Do not compare absolute modeled totals to older reports using other
emulator/profiler versions or instruction-length accounting.

Ordinary T4 production `heartbeat` totals suggested a 0.49% POV increase.
They count decoder dispatches with instruction combinations enabled and are
not comparable architectural instruction totals when code patterns change.
The separate decoded profiles resolve this: only the three removal opcodes
change, all other totals match, and both images remain reference-exact.

## Full POV fixture and provenance

`povray/run.sh` prepares copies of the original generic renderer, retains
copyright notices, and recompiles all 35 renderer/display and 22 fdlibm
modules with the selected compiler. The host adapter uses real iserver
open/read/write/seek traffic, initialized libc and bounded anonymous heap
space. This is a single-process numerical fixture, not the ICC process ABI
or a replacement operating system. It uses 64 MiB guest RAM with workspace
above the complete loaded image. Links reject unresolved symbols and every
link diagnostic; full output bytes are compared with `pictures.zip`.

Renderer/math code uses the T800 subset, but the existing libc contains
T9000 instructions, so full POV execution uses `-s9`. This is not a physical
4 MiB T800 or a TinyCC-built sixteen-node application. Those remain runtime
porting work, distinct from the historical array analysis.

Example from the repository root, using absolute tool/header/work paths:

```sh
TCC_DIR=/path/to/snapshot T4_BIN=/path/to/isolated/t4 \
  UCLIBC_HEADERS=/path/to/dereferenced/uclibc/include \
  WORK=/path/to/fresh/pov-run bash transputer/tests/bench/povray/run.sh
```

Each run records compiler/emulator/runtime/source hashes and build logs.
Paired Doom/CoreMark/ray artifacts, probes, decoded histograms, scalar
alignment results and all target regression outputs are retained under
`logs/compiler-coordination/sqrt-resident/`. Full POV builds and receipts
are in `logs/compiler-coordination/pov-tinycc/final-{base,candidate}/`.

| Input | SHA-256 |
| --- | --- |
| Baseline compiler | `2ab4cb2aeaea5678a5bedaa4176f9714a821a70b692c67361ec3efe337de82f3` |
| Candidate compiler | `e5cf142febdb1f28fa9f0310425dc5323d61791db52e9c68dc6ff87c59315d39` |
| Candidate `transputer-gen.c` | `a5eb7433cf779fd1364ced96fea6afb7a4bb0096909385ef21e81bcadedd5c29` |

The complete POV image hashes, identical for both compilers and their
packaged reference files, are:

| Scene | Bytes | SHA-256 |
| --- | ---: | --- |
| `box` | 921,615 | `fcaeaccaea31d263bede7a2f8774e2e7af8dcfc6f640a893a3fd1d3b8d1d6db0` |
| `basicvue` | 921,615 | `d537ba4e52842f8cd3e35f8c59bd3d30ac2c3008b60eafc10f7b6ff0cf17cde7` |

Procedural-noise `glass` is not an accepted image oracle for this portable
runtime: even the generic host renderer differs from the packaged image.
Its noise initialization depends on library `rand()` sequences; the BTL
audit identifies a different historical generator. Partial/time-limited
glass trials are unaccepted controls, not a passing compiler gate. The
fixture defaults to the two complete reference-matching scenes above and
keeps exact comparison rather than introducing a pixel tolerance.
