# Scheduler costs for workspace loads and stack shuffles

The local scalar estimate charges one cycle per instruction byte, including
prefixes, and one additional cycle for `LDL`. This gives `REV` a cost of one
and `DUP` a cost of two. It is a local T800 estimate.

The scheduler records adjacent loads only when both reads carry
nonvolatile local provenance and no relocation or branch entry separates
them. It compares the second load with `DUP` after workspace remapping.
At slot 15, the estimated cycle cost ties and `LDL` uses one fewer byte.
At slot 16, `DUP` saves one modeled cycle at the same width. At slot 256,
it also saves a byte. T414 cannot use `DUP`.

The host fixtures exercise both directions across the slot-15 boundary,
missing nonvolatile proof, branch entry, and the cost of the row-clear
setup. For a short workspace slot, `LDC 0; LDL` costs three cycles, while
`DUP; LDC 0; REV` costs four before the common store.

| Linux 7 row clear | Baseline | Candidate |
| --- | ---: | ---: |
| Fixed pixel stream, modeled T800 cycles | 5,763,408 | 5,763,408 |
| Fixed pixel stream, executed LDL + STL | 1,236,816 | 1,236,816 |
| Boot through desktop, modeled cycles over 126 clears | 8,645,112 | 8,645,112 |

Linux revision `2fdfdae954833` rebuilds to an identical BTL. It boots to
`DESKTOP_LOOP` with hard-P enabled and passes the protection and BusyBox
checks. The strict link checks 81,401 call relocations with zero mismatches.
The framebuffer oracle and captured 800 × 600 desktop match byte for byte.

- Doom phase measurements and hashes match the baseline on T800 and T9000.
- CoreMark validates all CRCs and matches baseline ticks on both targets.
- Four raytracer scenes match the references on one and four nodes.
- POV-Ray box and basicvue match after fresh renders. The rebuilt BTL
  matches the earlier fully rendered BTL byte for byte. The emulator and
  glass run settings match, including the recovered INMOS generator. The
  retained completed glass image matches the packaged reference.
- The host scheduler and indexed FP code-generation checks pass, as do
  differential seed 329 and seeds 24601–24625 at both padding settings.

Candidate compiler SHA-256
`d762873e178e3bcfddc2db8b73d466f6d229d594304eedb413eab9123d6d978e`.
