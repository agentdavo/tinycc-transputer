#!/bin/sh
# Phase 4 visual gate helpers: host geometry + optional SD boot capture.
#
# Usage:
#   transputer/fbwm/visual-gate.sh              # host tests only
#   transputer/fbwm/visual-gate.sh --bench      # host tests + bench counters
#   T4_SD_IMAGE=tpos.sd transputer/fbwm/visual-gate.sh --boot  # if T4 present
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT_DIR=$(CDPATH= cd -- "$SCRIPT_DIR/../.." && pwd)

echo "=== FBWM host geometry / golden / phase suite ==="
"$SCRIPT_DIR/test-fbwm-host.sh"

if [ "${1:-}" = "--bench" ] || [ "${1:-}" = "--all" ]; then
    echo "=== Host present-mode bench (via test binary counters) ==="
    # Re-run a tiny program that prints bench stats after atomic presents.
    BIN=$(mktemp /tmp/fbwm-bench.XXXXXX)
    trap 'rm -f "$BIN"' EXIT
    cc -std=c99 -D_GNU_SOURCE -DFBWM_HOST -Wall -Wno-int-to-pointer-cast \
      -I"$SCRIPT_DIR" -o "$BIN" -x c - "$SCRIPT_DIR/fbwm.c" "$SCRIPT_DIR/fbwm_shm.c" <<'EOF'
#include <stdio.h>
#include <sys/mman.h>
#include "fbwm.h"
#include "fbwm_shm.h"
int fb_kbd_available(void){return 0;} int fb_kbd_read(void){return 0;}
int fb_mouse_available(void){return 0;}
int fb_mouse_read_packet(int*a,int*b,int*c){(void)a;(void)b;(void)c;return 0;}
int main(void){
  int i,w;
  if(mmap((void*)FBWM_FB_BASE,FBWM_FB_SIZE,PROT_READ|PROT_WRITE,
          MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0)==MAP_FAILED) return 1;
  if(mmap((void*)(unsigned long)TPOS_SHM_BASE,TPOS_SHM_SIZE,PROT_READ|PROT_WRITE,
          MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0)==MAP_FAILED) return 1;
  if(mmap((void*)(unsigned long)TPOS_SCRATCH_BASE,TPOS_SCRATCH_SIZE,PROT_READ|PROT_WRITE,
          MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0)==MAP_FAILED) return 1;
  wm_init();
  fbwm_set_present_mode(FBWM_PRESENT_ATOMIC);
  fbwm_bench_reset();
  w=wm_create(40,40,300,200,"Bench",FBWM_WIN_DEFAULT);
  wm_surface_create(w);
  for(i=0;i<30;i++){
    wm_move(w,40+i,40+i);
    wm_dirty_full();
    fbwm_present_scene();
  }
  printf("bench composes=%u presents=%u pixels=%u\n",
         fbwm_bench_composes(), fbwm_bench_presents(),
         fbwm_bench_pixels_copied());
  return 0;
}
EOF
    "$BIN"
fi

if [ "${1:-}" = "--boot" ] || [ "${1:-}" = "--all" ]; then
    if [ -z "${T4_SD_IMAGE:-}" ]; then
        echo "Skip boot gate: set T4_SD_IMAGE to an SD image path" >&2
        exit 0
    fi
    T4="$ROOT_DIR/transputer/t4/t4"
    if [ ! -x "$T4" ]; then
        echo "Skip boot gate: t4 emulator not built" >&2
        exit 0
    fi
    echo "=== Boot visual gate (DESKTOP_READY + FLIP frames) ==="
    OUT=$(mktemp /tmp/fbwm-boot.XXXXXX)
    # Headless short boot; look for shell readiness markers.
    set +e
    T4_SD_IMAGE="$T4_SD_IMAGE" T4_SD_BOOT=1 T4_FB_RECORD_ON_FLIP=1 \
      timeout 120 "$T4" -s806 -sm 27 -ss >"$OUT" 2>&1
    set -e
    if grep -q 'DESKTOP_READY' "$OUT"; then
        echo "boot gate: DESKTOP_READY OK"
    else
        echo "boot gate: DESKTOP_READY missing" >&2
        tail -40 "$OUT" >&2
        exit 1
    fi
    FRAMES=$(ls fbrec_*.bmp 2>/dev/null | wc -l | tr -d ' ')
    echo "boot gate: recorded_frames=$FRAMES"
    if [ "$FRAMES" -lt 1 ]; then
        echo "boot gate: expected FLIP-recorded frames" >&2
        exit 1
    fi
fi

echo "visual-gate: PASS"
