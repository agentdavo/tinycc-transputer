# One block move for non-terminal double copies

All five matrix vector transforms already emitted a 24-byte `MOVE` in
`0a904f8c`. Their rebuilt `matrices.o` is byte-identical to that baseline.
The next two sites are in POV-Ray's `painted2` texture. Each branch copies
the three RGB doubles from a frame local through a caller-supplied pointer,
then stores alpha separately.

The prologue records which incoming ABI word supplies each nonvolatile
pointer parameter. The function pass requires that exact capture in the
entry block, with no other write, indirect access or escape of the pointer
home. The six source words are in the callee's frame. A pointer that already
denoted caller storage when the callee began cannot overlap them. All three
typed copies must be consecutive in one basic block, and workspace
renumbering keeps the six source words together.

For a non-terminal copy, the pass follows the integer stack effects of
fallthrough instructions. Each read must use values computed after the copy
until all three registers are accounted for or an independent return is
proved. The rewrite restores the last parked destination address if it
may still be used. In `painted2` the following alpha store overwrites
that slot before reading it.

## Profile

The paired 128×96 renders use [the painted2 scene](painted2-copy.pov), the
same POV-Ray sources, the recovered INMOS random generator, and buffered
RGB output. The instruction histogram uses the T800 timing model. The
linked renderer runs in T9000 emulator mode for libc.

| Measurement | `0a904f8c` | Candidate | Saved |
| --- | ---: | ---: | ---: |
| `painted2` modeled cycles | 2,055,103 | 1,971,467 | 83,636 (4.07%) |
| Whole render modeled cycles | 103,248,360 | 103,164,724 | 83,636 (0.0810%) |
| Whole render decoded instructions | 32,475,086 | 32,409,372 | 65,714 (0.202%) |
| Renderer ELF bytes | 325,628 | 325,572 | 56 |

The two RGB paths execute 1,444 and 1,543 times. Each `MOVE` copies six
words and saves 22 decoded instructions and 28 modeled cycles. Across the
render this removes 41,818 workspace loads and 8,961 workspace stores.
The rendered image is byte-identical to the baseline. Its SHA-256 is
`87e778f2f8cf9afaa0a581ef162bbf60bdebbd1439bbc1fe7cf33e45bc8f7d0e`.
The stock box and basicvue profiles reproduce their baseline counts.

## Validation

- Doom matches all eleven phase hashes, modeled cycle counts and `check 0 0`
  on T800 and T9000. CoreMark matches the reference CRCs and both tick totals.
- The raytracer matches all four reference images at one and four nodes.
- The full 640×480 POV-Ray box, basicvue and glass images match the packaged
  references byte for byte with the recovered INMOS random generator. The
  emulator exits normally. Glass SHA-256 is
  `79a805d547b93c68fc81c16e2c476ea09eed23aab17e091712fc8f0d1c9ef54c`.
- The copy fixture passes 16 runs across two compilers, four CPU models and
  two instruction layouts. Disassembly checks the block move before a live
  integer return and before a fourth store through a stacked pointer argument.
  The overlapping, escaped-pointer and branch-entry cases use scalar stores.
- Another 46 UART regressions pass. Differential fuzzing passes seed 329
  and seeds 24501–24600 at both code pads.

| Frozen input | SHA-256 |
| --- | --- |
| Baseline compiler | `147dafab1fc2a0dfc6b045e79cd22d95c5db6c576710d768c8c03d80872f24b0` |
| Candidate compiler | `0b7c47027b1a4c97f44e2c223bb730d91cf32520c3f15a90f5a163afb63ccb96` |
| Candidate `transputer-fnopt.h` | `f5dc3f51c4941c271f4378afc496558954f1bbca567d6f866930ea782d418617` |
| Candidate `transputer-gen.c` | `db3dc6375b67d41f6cd76308963c3713a652beef28a87edaa129473304aee0bf` |

Profiles, disassembly, render scripts and the acceptance receipt are in
`logs/compiler-coordination/double-copy-nonterminal/`.
