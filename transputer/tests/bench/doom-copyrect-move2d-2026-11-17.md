# MOVE2D for Doom status-bar rectangles

Doom restores the background under status-bar numbers and icons with
`V_CopyRect`. Each row advances by the 320-byte screen pitch. With
`DOOM_RECT_MOVE2D=1`, this routine can copy a strided rectangle in one
`MOVE2DINIT` and `MOVE2DALL` pair. The option is for capable CPUs on runtimes
that preserve the block-move state if the process is preempted. The pair is
emitted in a straight line with three operand loads between the two
instructions. See [the INMOS T9000 instruction set manual, §§7.15.1 and 9.9](https://www.transputer.net/iset/72-trn-240-01/t9000ism.pdf).

The guard compares the distance between the starting addresses with the
complete span `(height - 1) * SCREENWIDTH + width`. Both strides equal
`SCREENWIDTH`, so this proves separation across all rows even when two
screen indices alias. Full-width copies, short copies, and copies with
overlapping spans use the existing row loop. A same-screen copy shifted down one
row can read from a row that it wrote earlier. The fixture checks this
behavior as well as adjacent spans that touch exactly.

## Profile

The benchmark calls the actual Doom function and checks every byte of
both buffers and both guard areas. The dimensions and positions of the
number, short number, key and face use the included Freedoom WADs and
Doom's status-bar layout. The eight-row case measures the fast path's
lower height boundary. Each phase measures 256 calls.

| T800 phase | Shape | Row cycles | Opt-in cycles | Cycles saved | Row instructions | Opt-in instructions |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| Number | 39×16 | 387,085 | 344,077 | 43,008 (11.1%) | 126,218 | 71,178 |
| Face | 24×29 | 530,701 | 427,789 | 102,912 (19.4%) | 182,794 | 71,178 |
| Eight-row boundary | 12×8 | 233,485 | 227,341 | 6,144 (2.63%) | 91,402 | 71,178 |
| Short number | 12×6 | 209,421 | 211,469 | −2,048 | 82,698 | 85,514 |
| Key | 7×5 | 194,829 | 197,133 | −2,304 | 78,346 | 81,162 |
| Full refresh | 320×32 | 1,783,821 | 1,783,821 | 0 | 195,850 | 200,714 |

The T9000 build saves 11.35%, 19.58% and 2.98% modeled cycles on the three
eligible phases. Both targets use the T800 estimator. An initial emulator
binary left out the size-dependent transfer cost. The final profile uses
a rebuild from T4 `96241d6`, verified against the existing MOVE2D timing
test and the aligned target benchmark. T4 approximates partial and
unaligned rows. These figures measure the copy path in the emulator.

The corrected timing showed that setup cost outweighed the savings on
five- and six-row patches. They use row copies with about 1% dispatch
overhead in the opt-in build. The emulator trace counts 785 setup and
transfer pairs, including 768 timed calls and 17 eligible checks.

## Validation

- The complete Doom T9000 application links with the option enabled.
  Doom's eleven-phase benchmark matches all baseline outputs on T800 and
  T9000. The focused benchmark runs both paths on T414, T425, T800 and
  T9000. It verifies all 16 alignment pairs, zero and single-row cases,
  overlapping and adjacent spans, dirty bounds, and padding and source
  bytes. Both T414 builds produce an identical video object.
- CoreMark validates the reference CRCs on T800 and T9000. The raytracer
  matches all four reference scenes at one and four nodes.
- Complete POV-Ray box, basicvue and glass match the packaged 640×480
  images byte for byte with the recovered INMOS random generator. All
  three emulator runs exit normally. The glass SHA-256 is
  `79a805d547b93c68fc81c16e2c476ea09eed23aab17e091712fc8f0d1c9ef54c`.

Run `transputer/tests/bench/doom-copyrect.sh t800` with `TCC_DIR`, `T4_BIN`
and `UCLIBC` pointing at the toolchain and a size-aware T4 build. The
same script accepts `t414`, `t425`, `t805` and `t9000`.

Artifacts and the acceptance receipt are in
`logs/compiler-coordination/doom-copyrect-move2d/`.
