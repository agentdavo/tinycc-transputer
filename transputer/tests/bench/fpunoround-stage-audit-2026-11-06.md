# FPUNOROUND primary-source and staged T4 audit, 2026-11-06

No emulator or compiler change is proposed. The TCWG 7.9.3 INT64-to-REAL32
example explicitly selects round toward minus infinity. Its Appendix G treats
`FPUNOROUND`'s resulting FPU register value as `ex`: a partially computed
intermediate, rather than specifying its representation bit by bit. The
T414/T800 Instruction Set Manual describes changing REAL64 to REAL32 without
rounding the mantissa, for normalized inputs and zeros. Neither source here
fully specifies all hidden bits, save/restore behavior, or other operations on
that intermediate. Do not substitute this directed sequence for nearest-even.

The literal assembled TCWG routine was checked by decoding the object bytes:

```text
71       ldl 1            ; pointer to the low word
25fa     dup
29fa     fpb32tor64
4d 2afb  ldc 13; fpentry  ; fpunoround
51       ldnlp 1          ; pointer to the high word
29f8     fpi32tor64
4a 2afb  ldc 10; fpentry  ; fpuexpinc32
4d 2afb  ldc 13; fpentry  ; fpunoround
45 2afb  ldc 5; fpentry   ; fpurm
28f7     fpadd
72 28f8  ldl 2; fpstnlsn ; store result through output pointer
22f0     ret
```

The input is a little-endian low/high word pair. The output-pointer load and
store are test scaffolding, not part of TCWG's conversion sequence. On `-s8`,
the separate current-path control in the earlier oracle matched compiler
signed results for all 350 inputs; it is not the T9000's `fpadddbsn` path.

## Staged observations on current T4

A private emulator built from T4 `2853b29` includes the FPU load/store fixes
`fe000b2` and `2853b29` and MOVE2D timing `8177e21`. Its binary SHA-256 was
`0cf35980e79727793d88f2e0beef6012c93ca8b80b0d8b25ae458c8f73f4acf2`.
On the 350-case integer-oracle matrix its results were identical to those in
[the earlier investigation](int64-real32-investigation-2026-11-06.md): on
T800/T805, 44 signed and 26 unsigned nearest-even differences, and 100
mismatches between the literal TCWG sequence and the signed downward oracle.
The current compiler results in this sample were still adjacent values;
no new C contract violation is asserted.

For the exactly representable signed input `-1` (`hi=ffffffff`,
`lo=ffffffff`), the staged T4 probe observed:

| Stage | Stored bits | Mathematical value |
|---|---|---:|
| `fpb32tor64` of low word | `41efffffffe00000` | 4,294,967,295 |
| low `fpunoround`, then a single store | `4f7fffff` | 4,294,967,040 |
| signed high conversion and scale, stored as double | `c1f0000000000000` | −4,294,967,296 |
| high `fpunoround`, then a single store | `cf800000` | −4,294,967,296 |
| complete TCWG sequence | `c3800000` | −256 |

The separate intermediate stores are diagnostic observations; they do not
prove what physical hardware retains inside an unrounded FPU register. T4's
source does establish its own behavior: `fp_norounddb` returns a 32-bit host
float representation after shifting away the low 29 mantissa bits, and
`fp_dobinary` dispatches two SN operands to host single-precision arithmetic.
Once T4 has reduced both operands to those two stored binary32 values, their
sum cannot recover `−1`. This explains T4's result and demonstrates why a
plain packed-binary32-only intermediate cannot implement the documented
sequence's intended exact cancellation. It does **not** determine whether
real T800 hardware keeps extra significand bits, a sticky state, or another
representation, nor how every subsequent FPU instruction or context save must
handle it. A general semantic patch would require that missing evidence.

The staged input set also includes zero, +1, `2^32−1`, `2^32`, `2^32+1`,
`−2^32−1`, `−2^32`, `−2^32+1`, INT64 endpoints, and a large halfway-neighbor.
A separate FPUNOROUND/store probe records +0, −0, normalized 1.0, 1.0 plus
one REAL64 fraction bit, minimum-normal REAL32, two values below that boundary,
and a large finite value. The manual's normalized-or-zero restriction means
the below-normal results should not be interpreted as a required hardware
contract. In particular this audit does not infer semantics for unsupported
subnormal or exceptional inputs from T4 output.

## Reproduce

```sh
TCC_DIR=/path/to/compiler-and-archives T4_BIN=/path/to/t4 \
  WORK=/path/to/output bash transputer/tests/test_int64_fpunoround_stages.sh
```

For each `I` row the probe prints index, low REAL64 bits, low post-FPUNOROUND
single-store bits, scaled-high REAL64 bits, high post-FPUNOROUND single-store
bits, and the complete TCWG-sequence result. `N` rows report the separate
FPUNOROUND/store probes. Source and detailed output are retained in `WORK`.
Use [the integer-oracle harness](../test_int64_real32_oracle.sh) for the full
six-target matrix. A correct emulator fix needs primary or hardware-backed
semantics for this transient state, including stack movement, other operations,
rounding, exceptions and explicit FP state save/restore; changing only
`fp_norounddb` to an inferred nearest or sticky rule is not sufficient.
