# INT64 to REAL32 rounding investigation, 2026-11-06

No compiler change is proposed. The current T800 signed conversion can
round twice; the T9000 uses `fpadddbsn` for the signed single-precision
result. Unsigned conversion calls `__floatundisf` in `lib/libtcc1.c`; it uses
an intermediate `long double`, which this target represents as REAL64. The
T414/T425 signed soft-float `__floatdisf` uses the integer `f_round` path.

## Contract

The C committee draft specifies that an in-range integer which is not exactly
representable in the destination real type converts to either adjacent value,
chosen in an implementation-defined manner: [WG14 N843 §6.3.1.4](https://www.open-std.org/jtc1/sc22/wg14/www/docs/n843.htm).
The existing backend comments and `test_float_int_cvt.c` explicitly acknowledge
the T800 double-rounding difference and allow either adjacent result in the
chosen case. `FLT_ROUNDS=1` in this tree describes rounding to nearest; it
does not by itself establish that every integer conversion is correctly
rounded to nearest-even. Consequently the observed adjacent results are a
cross-target nearest-even inconsistency, not by themselves a newly proved
violation of the current C conversion contract.

The test's reference is a pure-integer binary32 oracle using guard, remainder
and ties-to-even; it does not pass values through host double precision. A
separate GCC host check matched all 350 signed and unsigned nearest-even
results. The 350 deterministic bit patterns include exact values, halfway
points and neighbors around powers from 2^24 through 2^63, positive and
negative values, INT64 endpoints, UINT64 values near 2^64, and 128 seeded
pseudorandom words. For this sample every *current compiler* result was one
of the enclosing representable binary32 values. This is a bounded test, not a
proof for all 2^64 inputs.

| Target | Emulator | Signed nearest-even mismatches | Unsigned nearest-even mismatches |
|---|---|---:|---:|
| T414 | `-s414` | 0 | 4 |
| T425 | `-s425` | 0 | 4 |
| T800 | `-s8` | 44 | 26 |
| T805 | `-s8` | 44 | 26 |
| T9000 | `-s9` | 0 | 4 |
| T9000TVX | `-s9` | 0 | 4 |

For example, signed `0x1000001000000001` rounds to `0x5d800001`
nearest-even but the T800 path returns `0x5d800000`. For unsigned
`0x8000008000000001`, nearest-even is `0x5f000001`, while all six target
builds returned `0x5f000000`. The T805 row uses the T800 emulator model;
the TVX row does not exercise TVX instructions.

## TCWG sequence and modeled cost

TCWG 7.9.3's documented `fpb32tor64; fpunoround; fpi32tor64;
fpuexpinc32; fpunoround; fpurm; fpadd` sequence is specifically for
round-toward-minus-infinity, not round-to-nearest. It is therefore not a
justified replacement for the backend's default nearest-rounding path.
The test uses the complete address sequence from TCWG, with explicit
`ldc; fpentry` pairs for the named FPENTRY operations.

On the current T4 emulator, this literal sequence disagreed with an exact
signed downward-rounding oracle for 100 of the 350 words. Notably, signed
`-1` produced `0xc3800000` (`-256.0f`) instead of the exactly representable
`0xbf800000` (`-1.0f`). The emulator's `fp_norounddb` discards low mantissa
bits; whether its handling of FPUNOROUND and subsequent FP operations fully
matches real T800 internal semantics requires separate primary-source and/or
hardware validation. This observation does not establish a defect in the
published TCWG sequence or on physical hardware. It does rule out treating
this T4 result as evidence that the sequence fixes nearest rounding.

As a control, a hand-written call-framed version of the current T800
REAL64-add-then-REAL32 sequence matched compiler-generated signed results for
all 350 inputs on `-s8`. The T9000 compiler uses a different single-round
instruction, so this control is not expected to match that target.

On the T800 estimator, two otherwise equivalent call-framed loops of 1,000
conversions of the same halfway value measured 103,037 cycles for the current
sequence and 108,038 for the directed TCWG sequence, including identical
loop/call/timer overhead. Both produced `0x5d800000` for this value. The
5,001-cycle difference is specific to this emulator model and probe, not a
hardware timing or a cost comparison of two equivalent rounding contracts.

## Reproduce

Build a compiler and its four target `libtcc1` archives, then run:

```sh
TCC_DIR=/path/to/compiler-and-archives \
T4_BIN=/path/to/t4 WORK=/path/to/output \
  bash transputer/tests/test_int64_real32_oracle.sh
```

The script builds the generated host oracle check, runs each target, then runs
the T800 cost probe. It retains `vectors.txt`, generated source, each target's
UART output and emulator/link logs under `WORK`. `S` and `U` lines denote
nearest-even mismatches, `D` denotes a manual directed-sequence mismatch, and
`C` compares the hand-written current T800 sequence to the compiler result.
The `TOTAL` fields are hex: case count, signed nearest mismatches, unsigned
nearest mismatches, directed-sequence mismatches. `C` lines on T9000 are
expected because that compiler uses `fpadddbsn`.

The frozen source revision was `5b7e89f1`. Compiler SHA-256:
`614dd20908d69c4bc88740db14b3de86db81e68c00f0fc0fb906f3841a5a3688`.
The T4 binary used here was
`4175a6609d9222ca402e0a033000522e79dc137da33be256f389da13abb0966a`.
T414/T425/T800/T9000 runtime SHA-256 values were respectively
`c1f955193dbb0a3ad0a9fa4e05c8210459714239a181439ed9993f5b0a7bc79d`,
`8b62fa1bcc539005c70b261cf2900d41a966bf37fe861b19231c732dee6a50af`,
`dbb6d39c4e13aec053eea8fbae2d550e1dbc64bc3cfb1b8b6fa5b006f3169e06`, and
`6750477284c50b500a90ec79e18f8918247d0dafd2c14849aca1ebb8cb72c818`.
Archives were existing builds; their exact source revisions were not established.
