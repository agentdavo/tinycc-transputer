# TransputerOS Window Manager Review and Roadmap

Review baseline: 2026-08-07, 800x600 XRGB8888 simplefb.
Status: **Phases 0–4 complete** (host-verified; boot visual gate optional).

## Executive finding

FBWM is a retained, multi-window compositor with cross-process SHM clients,
size-constrained window management, widget chrome states, and atomic present.

| Layer | Mechanism |
|---|---|
| In-process clients | `wm_surface_*` + `wm_compose` / `fbwm_present_scene` |
| Cross-process guests | SHM bank `0x81C00000` + `tpos_shm_compose_present` (DOOM `-windowed`) |
| Atomic present | Scratch `0x81A00000` → full-frame copy → FLIP (default) |
| Window mgmt | Move clamp, 8-edge resize, min/max/restore, cascade, Alt-Tab overlay |
| Shell | Icon selection, login field model, live clock, SHM DOOM launch |

## Memory map (memblock-reserved)

```
0x81A00000  2 MiB   present scratch (atomic scanout)
0x81C00000  2 MiB   compose SHM (surfaces + window table)
0x81E00000  2 MiB   simplefb aperture
```

## Pixel contract

Half-open rects `[x,x+w)×[y,y+h)`. Chrome: 3 px border, 22 px titlebar,
18×16 close, 32 px taskbar, 800×568 work area. DOOM client: exactly 320×240.

## Phase checklist

### Phase 0 — visual specification — DONE
- [x] Theme constants, geometry helpers, golden frame, DOOM shared palette

### Phase 1 — retained compositor + surfaces — DONE
- [x] Pool surfaces, damage, compose blit
- [x] Cross-process SHM publish/claim/compose
- [x] DOOM `-windowed` surface present
- [x] Shell publish peers before spawn

### Phase 2 — window management — DONE
- [x] Constrained move, 8 resize zones, interactive resize
- [x] Minimize / restore / maximize + restore geometry
- [x] Size hints (`wm_set_size_hints`) applied on resize
- [x] Placement cascade (`wm_place_cascade`)
- [x] Taskbar hit + activation / toggle-minimize
- [x] Focus cycle + Alt-Tab overlay (Tab / Enter)
- [x] Dirty-region tracking (`wm_dirty_*`, compose skips when clean)
- [x] Events: FOCUS / MOVE / RESIZE / MIN / MAX types

### Phase 3 — widgets + shell convergence — DONE
- [x] Widget client clip, z-order (`wg_raise`), hover/pressed/disabled/focus
- [x] Textbox caret model + `wg_key` editing
- [x] Login field model + focus chrome; desktop icon selection
- [x] Model-backed taskbar clock (`wm_set_clock_text`)
- [x] `fbwm_demo.c` interactive conformance app
- [x] Shell uses library present + cascade + SHM DOOM path

### Phase 4 — presentation + performance — DONE
- [x] Atomic present mode (scratch → FB → FLIP); direct mode for host goldens
- [x] Bench counters: composes / presents / pixels copied
- [x] `visual-gate.sh` host suite + optional `--bench` / `--boot`

## Running checks

```sh
transputer/fbwm/test-fbwm-host.sh
transputer/fbwm/visual-gate.sh --bench
transputer/fbwm/build-fbwm.sh /tmp/fbwm.flt
bash transputer/doom/build-transputer.sh --all
```

Optional full-system gate:

```sh
T4_SD_IMAGE=tpos.sd T4_FB_RECORD_ON_FLIP=1 \
  transputer/fbwm/visual-gate.sh --boot
```

## Desktop keys

| Key | Action |
|---|---|
| `t` | Terminal (retained) |
| `D` | Real DOOM via SHM compositor |
| `g` | In-process DOOM-sized surface |
| `Tab` / `Enter` | Alt-Tab overlay step / commit |
| `m` / `r` | Minimize / restore terminal |
| `s` | Spawn UART shell |
| `Esc` | Exit FBWM |

## Residual polish (non-blocking)

- Dirty compose still redraws the full scene when dirty (tracking avoids no-ops)
- SHM title font is compact 8×8 vs library 8×16
- Login password is decorative (no auth backend)
- Cursor still uses save/draw/restore on the visible aperture after atomic copy
