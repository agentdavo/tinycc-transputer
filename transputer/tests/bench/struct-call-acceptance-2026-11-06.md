# Whole-structure call argument acceptance, 2026-11-06

The baseline was `5b7e89f1`; the integrated candidate was `c96e1bcd`, on
top of the independently fixed scalar-spill alignment (`fc344c0b`). Both
compilers were built from their exact committed sources. The same baseline-built
`-mt800` runtime and the same t4 `fe000b2` binary were used for both runs.

| Input | SHA-256 |
|---|---|
| Baseline compiler | `44a61cb0eb40c32d74324980e7a6e168625bb102d38285fe83af6a31b1534cc7` |
| Candidate compiler | `0dfa4143095fb1a48941e802a0be91ee48ee0a7e8c6955a98a989faabccfe80b` |
| Shared T800 runtime | `c26e5fc2d7d0eeb976ffe095a74d56854451be667ca6d38459249decf7f40d6e` |
| t4 `fe000b2` | `9ce2738daf5251acb012a09a0cfd0685a3bba9a0b0aa0e685f7faa12bcabaaee` |
| Candidate `transputer-gen.c` | `87938f1c01eb8e24474ecd31e56c630c1b721f7578b2933e757d78f2dda85b3e` |
| Candidate `tccgen.c` | `476af2822a8de771a0a9c4405b4e8ab65faa06c89cd6748d09188ba37ccda302` |

The source hash differs from the earlier private matrix because the candidate
was rebased over a TVX builtin refactor before integration. This report uses
the exact integrated source and repeats the measurements after that rebase.

## Results

The dedicated benchmark executes 2,000 calls with 7-, 16- and 65-byte
by-value structures. `struct_call.sh t800` reported 1,854,033 estimated
cycles before and 1,064,031 after; both hashes were `1747770740`. Its
compiled object text shrank from 861 to 625 bytes.

The Doom T800 phases had identical hashes and estimated cycles before and
after. Examples: column `b6eadcb0`, 2,608,228 cycles; span `05d26359`,
4,413,304; segloop `6b9d321f`, 2,445,729; masked `9c0f0700`, 1,463,818;
mapplane `e3973655`, 8,783,742. The other phases and the FixedMul/FixedDiv
checks also matched.

Five CoreMark iterations produced matching CRCs (`seed e9f5`, list `e714`,
matrix `1fd7`, state `8e3a`, final `f24c`) and 25,368,644 estimated whole-run
cycles for each compiler. CoreMark's displayed score is not meaningful when
the estimator replaces the hardware timer.

Both ray-tracer builds rendered model 3, 256×256, one node, 405,061 rays,
byte-identical to the reference picture. Each reported 3650.81 modeled timer
seconds: approximately 3,650,810,000 cycle ticks, rounded to the program's
0.01-second display precision. This is an emulator estimate, not measured
silicon time.

The candidate passed the 244-test C suite at T9000, T800 and T425, and 400
differential fuzz seeds at each of link pads 0 and 1027. Focused aggregate,
packed, scalar-staging and call tests passed on T9000, T9000+TVX, T805, T800,
T425 and T414. `test_struct_call_volatile.py` verifies that volatile globals,
pointers, nested array members and union members keep the prior per-word
access shape, while nonvolatile objects use one exact-size MOVE.
