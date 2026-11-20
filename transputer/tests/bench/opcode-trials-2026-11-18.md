# Transputer opcode trials, 18 November 2026

## Linux `LDIV`

Linux commit `862f42ce8a46` replaces the generic shift/subtract loop in
`__div64_32` with one or two native divides. Host-generated quotient and
remainder vectors pass on T414, T800 and T9000. The rebuilt Linux BTL boots
with hard-P enabled and its desktop framebuffer matches byte for byte.
Separate boot samples average 6,839 versus 98 modeled cycles per call.
The boot samples have different call counts.

## CoreMark and Doom diagnostics

| Trial | Baseline | Candidate | Change |
| --- | ---: | ---: | ---: |
| T425 CRCBYTE, timed modeled cycles | 4,970,599 | 3,925,490 | −21.0% |
| T800 CRCBYTE, timed modeled cycles | 4,970,600 | 3,925,491 | −21.0% |
| T9000 CRCBYTE, timed T800-model cycles | 3,271,251 | 2,634,429 | −19.5% |
| T9000 matrix walk, timed T800-model cycles | 3,271,251 | 3,019,051 | −7.7% |
| T9000 matrix walk, whole-run groups | 443,607 | 413,548 | −6.8% |
| T9000 matrix walk, whole-run T9000-model cycles | 660,260 | 620,155 | −6.1% |
| Doom T800 column with LEND, T800-model cycles | 2,608,228 | 3,283,108 | +25.9% |
| Doom T9000 column with LEND, T800-model cycles | 3,262,948 | 3,277,348 | +0.4% |
| Doom T9000 through first column marker, grouped model cycles | 1,225,782 | 1,187,382 | −3.1% |

The CoreMark port's opt-in `COREMARK_TRIAL=crcbyte` and
`COREMARK_TRIAL=matrix-walk` generate trial sources in the build directory.
Both preserve the five expected CRCs. The CRC sequence reflects the XOR of
the data and state, advances it eight bits with `CRCBYTE`, then reflects it
back. The matrix trial keeps row bases and advances the strided index,
while retaining the original output load and store inside the loop.
These one-iteration runs use modified input and are diagnostic figures.

Use the standard Transputer CoreMark build script with `CPU=t9000`,
`ITERATIONS=1` and a separate `BTL` output path. Compare the default build
with each trial under `T4_EST_CYCLES=1`. Set `T4_T9000=1` to record the
grouping model separately.

The Doom experiment moved the existing column body into a leaf routine
with a two-word LEND index/count descriptor. All eleven hashes match on
T800 and T9000. The T9000 column drops from 1,119,084 to 1,001,644 decoded
instructions. The T800 regression and the small whole-run change in the
T9000 grouping model leave the existing Doom loop in place.

## POV-Ray floating-point loads

The scratch renderer replaces the three-term `VDot` macro with a leaf
assembly routine. It uses `FPLDNLDB`, `FPLDNLMULDB`, and `FPADD` in
the original arithmetic order, then stores the result once. It runs
832,099 times on the box scene and produces a byte-identical reference
image. The whole render rises from 4,599,992,317 to 4,625,342,023 modeled
cycles (+0.55%). That result includes calling and preparing operands for
the leaf routine. The slower variant was dropped at the box gate, before
the longer glass and raytracer checks.

The CoreMark matrix result measures the value of induction-variable
strength reduction in this workload. It leaves implementation of a
generic TinyCC loop rewrite and group-aware instruction selection open.
