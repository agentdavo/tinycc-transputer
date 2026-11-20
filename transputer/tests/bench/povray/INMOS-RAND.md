# Historical POV-Ray random compatibility

`inmos-rand.c` is an explicitly linked POV fixture object, not a replacement
for the compiler runtime or installed libc. It reproduces the supplied ICC
image's `srand`/`rand`: 55 initialized words, unsigned 32-bit wrapping,
seed multiplier 69069/addend `0x66d619e1`, initial indices 23/54, and the
observed decrement/zero-wrap order. Unseeded calls use seed 1 for C callers;
the historical POV path explicitly calls `srand(0)`.

`inmos-rand-vectors.h` contains 128 outputs captured from the unchanged
historical BTL, including both index-wrap boundaries. `test-rand.sh` checks
the whole sequence and reseeding on strict T414/T425, T800 and T9000. A
native UBSan build of the same fixture also passes.

With this object linked, the complete TinyCC generic renderer reproduces
the packaged 640×480 `box`, `basicvue` and **glass** images byte for byte.
Glass is 921,615 bytes, SHA-256
`79a805d547b93c68fc81c16e2c476ea09eed23aab17e091712fc8f0d1c9ef54c`.
All three emulator runs exit normally and strict link diagnostics are empty.
Renderer/fdlibm use the T800 subset; the existing libc requires T4 `-s9`.

Evidence is retained in `logs/compiler-coordination/pov-rgb-rows/rand-test/`
and `pixel/`, including source hashes, the linked executable, full images
and emulator logs. A live edit of the long-running shell driver caused a
trailing `rep: command not found` after its three successful image checks;
that driver status is not a rendering failure. Independent complete-file
comparisons and normal emulator-exit logs establish the accepted images.
Keep validation runners unchanged while they are running.

The [BTL analysis](../../../POVRAY-BTL-ANALYSIS-2026-11-06.md#recovered-historical-random-number-contract)
records the original disassembly, seed-state proof, captured sequence and
native host glass control. Further seeds are algorithm reconstructions,
not additional captured historical oracles.
