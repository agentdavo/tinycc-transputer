# Retained-surface slot 3 bounds repair

**Fixed in `685532a4` on 6 November 2026.** Slot 3 is now 400×284,
and a compile-time check requires the header and all advertised slot pixels
to fit in the existing 2 MiB bank. No slot offsets or reserved mappings moved.
The final slot ends at `0x81dfff00` in the default profile and `0x813fff00`
in the SPINE profile, leaving 256 bytes unused in either bank.

FBWM and all guest clients must be rebuilt together. The shared-memory
version is unchanged; an older binary still contains the unsafe capacity.
A 400×300 surface can still use slot 1 when available, but cannot use slot 3.
The fourth 320×240 guest still fits in slot 3. This repair is committed
locally; rebuilding/deploying a full rootfs is separate work.

## Original defect

The default T4 profile reserves the shared bank at
`[0x81c00000, 0x81e00000)`, including a 4096-byte header. The simplefb
framebuffer begins at `0x81e00000`. The fixed slot layout in `fbwm_shm.c`
previously advertised these pixel extents (end exclusive):

| Slot | Capacity | Extent |
|---|---:|---|
| 0 | 320×240 | `[0x81c01000, 0x81c4c000)` |
| 1 | 640×400 | `[0x81c4c000, 0x81d46000)` |
| 2 | 320×240 | `[0x81d46000, 0x81d91000)` |
| 3 | 400×300 | `[0x81d91000, 0x81e06300)` |

Slot 3 extended **25,344 bytes (6,336 pixels)** beyond the bank, into the
first 6,336 framebuffer pixels. Its zero-based pixel `[284][64]` is the
first word at the bank end. In the SPINE profile the same relative overflow
is `[0x81400000, 0x81406300)`, overlapping the present-scratch carveout.

This was reachable without malformed metadata. `tpos_shm_claim_slot` returns
the first free slot whose advertised dimensions fit. When slot 3 is selected,
`wm_shm_prepare_guest` clears its entire advertised 400×300 capacity, even
for a smaller guest, and therefore writes the adjacent bank. FBWM's surface
publisher can also copy a full 400×300 surface into it. Doom requests 320×240;
its own rows fit within the bank at the slot-3 stride, but the window-manager
full-capacity clear still reaches the overflow if that path selected slot 3.

## Original canary reproducer

`transputer/tests/bench/fbwm_blit.sh t800 row` runs the real shared-memory
layout in t4. The benchmark writes a canary at `TPOS_SHM_BASE + TPOS_SHM_SIZE`,
writes slot 3 at the corresponding pixel index only if it is within the
advertised capacity, and restores the adjacent word. With the original layout
it reports `slot3_overflow 1`. This is a diagnostic of the existing layout,
not a failure of the compositor's row-copy or MOVE2D comparison. Run only in
the emulator: the probe intentionally touches the word after the shared bank.

The chosen safety repair reduces slot 3's advertised
height from 300 to 284 and adds a compile-time assertion that the
header plus all advertised slot capacities fit in `TPOS_SHM_SIZE`. That
leaves 256 bytes unused and prevents the full-capacity clear from crossing
the bank. It changes availability: a 400×300 request can still use slot 1
when available, but can no longer use slot 3. Preserving every advertised
capacity concurrently requires a different, coordinated layout or allocation
scheme and an assessment of the cross-process shared-memory contract. This
repair does not enlarge a mapping.

## Target-emulator and host acceptance

The chief reproduced `slot3_overflow 1` before applying the repair, using
the original `82c5413b` source and T800 row-copy benchmark. Afterward, all
twelve target/profile combinations completed with `slot3_overflow 0`,
`fail 0` and reference-checked framebuffer hash `3056038533`:

| Target build / emulator | Modes | Address profiles |
| --- | --- | --- |
| T800 / `-s8` | Row and MOVE2D | Default and SPINE |
| T9000 / `-s9` | MOVE2D | Default and SPINE |
| T805 / `-s8` | MOVE2D | Default and SPINE |
| T425 / `-s425` | MOVE2D | Default and SPINE |
| T414 / `-s414` | Row fallback | Default and SPINE |

The benchmark exercises the full repaired slot capacity, negative and
right/bottom clipping, small rectangles, unusual strides, alternate
destinations, source preservation and the bank-end canary. Its final frame
hash differs from the old benchmark because the tested slot height changed;
each repaired run matches its independent pixel reference. The runner now
explicitly requires both zero failures and zero overflow.

The host suite passes in both profiles, exercising the actual guest clear,
clipped surface copy, last valid row, fourth Doom-sized allocation and
400×300 allocation fallback. Both canonical frames retain SHA-256
`17a5ccefc0dc76e6fc2ebc3240a0cdb0feb45784c0d3ad5cb587fe395b01e976`.

The T800 320×240 measured section remains 2,107,020 modeled cycles for row
copies and 1,918,248 for MOVE2D. These are the existing compositor timings,
not a new speedup from reducing capacity. T4's T800 estimates are not silicon
timings, and the SPINE address-profile runs are not RTL/hardware validation.

| Pinned input | Revision / SHA-256 |
| --- | --- |
| Compiler snapshot | `681cbe51`; `6317ac30c76813e45d092c0b98e65c615a470d96f4135d8e5e47db8656f3d0a5` |
| Isolated T4 | `2853b29`; `0cf35980e79727793d88f2e0beef6012c93ca8b80b0d8b25ae458c8f73f4acf2` |
| Netloader | `5f827f450a4993804b6ac99345f8d2dc702ef0226d10e5e233550420692f7a8d` |
| T800/T805 runtime | `dbb6d39c4e13aec053eea8fbae2d550e1dbc64bc3cfb1b8b6fa5b006f3169e06` |
| T9000 runtime | `6750477284c50b500a90ec79e18f8918247d0dafd2c14849aca1ebb8cb72c818` |
| T425 runtime | `8b62fa1bcc539005c70b261cf2900d41a966bf37fe861b19231c732dee6a50af` |
| T414 runtime | `c1f955193dbb0a3ad0a9fa4e05c8210459714239a181439ed9993f5b0a7bc79d` |

Build logs, baseline and repaired UART/ELF/BTL captures, input/source hashes,
and the SPINE host frame are retained in
`logs/compiler-coordination/slot3-bounds/`. The shared T4 executable and
production toolchain binaries were not replaced.

Reproduce with a built compiler/runtime bundle and corrected T4:

```sh
TCC_DIR=/path/to/tools T4_BIN=/path/to/t4 WORK=/path/to/output \
  bash transputer/tests/bench/fbwm_blit.sh t800 move
# Set EXTRA_CFLAGS=-DTPOS_SPINE_CHIP for the alternate address profile.
bash transputer/fbwm/test-fbwm-host.sh
```

## Historical session checkpoint

The original four-file proposal is preserved as
[`fbwm-slot-bounds.pending.patch`](fbwm-slot-bounds.pending.patch), SHA-256
`65378e5da7b9a20aa5a1b84121541d6ca1c925072dd7243167111d84381fc60a`.
It reduces slot 3 to 400×284, adds a compile-time reservation check and extends
host/compositor canaries. Its source/test changes are now integrated in
`685532a4`, along with the benchmark-runner overflow check. The file keeps its
original name and contents for provenance; it is no longer pending and
must not be reapplied to the repaired tree.

The owner reports a failing-before/passing-after host canary on default and
SPINE profiles, both retaining canonical frame SHA-256
`17a5ccefc0dc76e6fc2ebc3240a0cdb0feb45784c0d3ad5cb587fe395b01e976`.
The chief has now completed the target acceptance above. The original
detached worktree and its host-test artifacts remain a historical snapshot;
their uncommitted patch is superseded by the integrated repair. The six
worker chats remain archived.
