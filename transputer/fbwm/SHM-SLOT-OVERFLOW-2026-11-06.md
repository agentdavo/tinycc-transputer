# Retained-surface slot 3 exceeds its bank

The default T4 profile reserves the shared bank at
`[0x81c00000, 0x81e00000)`, including a 4096-byte header. The simplefb
framebuffer begins at `0x81e00000`. The fixed slot layout in `fbwm_shm.c`
currently advertises these pixel extents (end exclusive):

| Slot | Capacity | Extent |
|---|---:|---|
| 0 | 320×240 | `[0x81c01000, 0x81c4c000)` |
| 1 | 640×400 | `[0x81c4c000, 0x81d46000)` |
| 2 | 320×240 | `[0x81d46000, 0x81d91000)` |
| 3 | 400×300 | `[0x81d91000, 0x81e06300)` |

Slot 3 extends **25,344 bytes (6,336 pixels)** beyond the bank, into the
first 6,336 framebuffer pixels. Its zero-based pixel `[284][64]` is the
first word at the bank end. In the SPINE profile the same relative overflow
is `[0x81400000, 0x81406300)`, overlapping the present-scratch carveout.

This is reachable without malformed metadata. `tpos_shm_claim_slot` returns
the first free slot whose advertised dimensions fit. When slot 3 is selected,
`wm_shm_prepare_guest` clears its entire advertised 400×300 capacity, even
for a smaller guest, and therefore writes the adjacent bank. FBWM's surface
publisher can also copy a full 400×300 surface into it. Doom requests 320×240;
its own rows fit within the bank at the slot-3 stride, but the window-manager
full-capacity clear still reaches the overflow if that path selected slot 3.

## Canary reproducer

`transputer/tests/bench/fbwm_blit.sh t800 row` runs the real shared-memory
layout in t4. The benchmark writes a canary at `TPOS_SHM_BASE + TPOS_SHM_SIZE`,
writes slot 3 at the corresponding pixel index only if it is within the
advertised capacity, and restores the adjacent word. With the current layout
it reports `slot3_overflow 1`. This is a diagnostic of the existing layout,
not a failure of the compositor's row-copy or MOVE2D comparison. Run only in
the emulator: the probe intentionally touches the word after the shared bank.

The smallest non-widening safety repair is to reduce slot 3's advertised
height from 300 to 284, and add a compile-time or test assertion that the
header plus all advertised slot capacities fit in `TPOS_SHM_SIZE`. That
leaves 256 bytes unused and prevents the full-capacity clear from crossing
the bank. It changes availability: a 400×300 request can still use slot 1
when available, but can no longer use slot 3. Preserving every advertised
capacity concurrently requires a different, coordinated layout or allocation
scheme and an assessment of the cross-process shared-memory contract. This
finding does not change the layout or silently enlarge a mapping.
