# Linux 7 aligned fill measurements

The Linux 7 port at `8708fa3c6263` contains the validated wider fills.
The user requested an inclusive history squash beginning at
`842b0d5cfa08813f1703d13961b5ee2b144819e5`. All 18 commits through
`b89c22cc9507` were combined with parent `6628e7d4c6b2`.
The tracked source tree is identical before and after the squash,
`5d645ed2e7eb5fc4c2e2e964a7ea936163ff99fb`. The original history is retained
at `codex/linux7-before-squash-20261117` in the nested Linux repository.

The fill changes were individually validated and committed before squashing.
The zero-fill remainder uses sixteen word stores per pass. Nonzero fills
of at least sixteen bytes align their destination, build a repeated-byte
word, and use sixteen stores per bulk pass with exact word and byte tails.
The framebuffer row clear uses sixty-four word stores per pass.

| Profiled kernel routine | Before instructions | Final instructions | Before modeled cycles | Final modeled cycles |
| --- | ---: | ---: | ---: | ---: |
| `memset` | 3,549,513 | 3,239,991 | 6,372,953 | 5,821,511 |
| `fbt_clear_row` | 4,481,400 | 4,001,400 | 7,765,200 | 7,705,200 |

These routines save 789,522 decoded instructions and 611,442 modeled T800
cycles in the profiled boot. The zero-fill widening alone saves 552,013
cycles in `memset`. The nonzero path adds 571 cycles to this boot while
reducing the mixed boundary workload from 19,676,472 to 2,350,326 cycles
(88.1%). Framebuffer widths of 16, 32, 64 and 128 were compared.
The 64 and 128 word versions tie in modeled cycles, and 64 uses less code.

The [target fill regression](../test_linux7_memset.c) verifies 2,020 cases
across all four byte alignments, zero and short lengths, block and page
boundaries, negative and oversized byte values, original return pointers,
and surrounding canaries. The exact kernel console objects pass a separate
fixture that clears all 37 text rows, wraps the cursor, and checks the
remaining scanlines and adjacent canaries.

The strict rebuild uses TinyCC `f2ac20b4` and the retained rootfs. The saved
`tpos_defconfig` selects the SPINE framebuffer map at `0x81000000`.
Ordinary and instrumented T4 `-s806 -sm 25` runs pass the hard-P protection
self-test and reach `DESKTOP_LOOP`. The strict link reports zero unresolved
symbols and the call audit reports zero mismatches across 81,427 relocations.
The three existing modpost section-mismatch warnings remain.

The final framebuffer capture matches the baseline byte for byte.
The BTL decoder reconstructs the ELF load segment exactly. Final
`vmlinux.btl` SHA-256 is
`07a2a233c039e991e41832bd9a0202bcd9ac7195dad22ce724762f65c066ea9b`.
Raw receipts, profiles and the BTL are in
`logs/compiler-coordination/linux7-fills-2026-11-17/`.
