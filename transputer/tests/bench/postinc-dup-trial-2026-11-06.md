# Post-increment duplicate-load probe

The fixed-size block-copy investigation first scanned Doom, CoreMark and the ray tracer for loops that copy between distinct, fixed-size local arrays. No qualifying source loop was found in that scan. TransLocal's destination is a pointer parameter, so that narrow proof does not cover it.

A private compiler prototype then scanned for consecutive loads of the same workspace slot after the existing scheduler. It changed the second `LDL` to `DUP` only for compiler-owned register slots outside the outgoing argument area, in functions without escaped reserved slots, on targets supporting `DUP`. It required the whole instruction interval to pass the scheduler's existing relocation and branch-target check. It did not change the frontend, aggregate calls, loop pooling or the compiler checkout.

The small post-increment probe produced repeated loads from local-variable slots outside that safe subset. These slots cannot be assumed non-volatile after decoding machine code. The ray tracer's `raytrace.c` had one qualifying site, in `wrch`; the five CoreMark core source files had none. The existing `test_post_inc.c` produced `POSTINCOK` with both compilers, including the trial's one eligible site in that test. This is a focused check, not a full scheduler or compiler validation.

For model 3 at 256x256, one node, the baseline and trial both rendered a byte-for-byte match to `pic/ray3.ppm`. Using the same private T4 build with its existing T800 counter printed at exit, both executed 1,175,888,819 instructions and consumed **3,654,480,420 estimated cycles**. Prefix bytes increased from 691,295,225 to 691,295,306 in the trial. The direct load from these low slots is one byte; `DUP` takes a prefix plus its opcode, so the modeled cost does not improve. The cycle counter includes the complete run, including startup and output, and is not the separately attributed ray-only histogram.

The trial was rejected and no compiler change was retained. Since it showed no ray-tracer cycle gain, no broader three-workload cycle matrix or all-scene image sweep was run. It does not provide the missing volatile, bounds, provenance, escape or control-flow facts for a loop-to-block-copy rewrite.

Baseline compiler SHA-256: `a16663ebc15f358f3ac614057861ff751224e8164372c664602f4e09a9b7062f`. Trial compiler SHA-256: `c2a580812336d8a741649b50ef36bd5fd54e2e227802e7097a19266ac38fbaf5`.
