#!/bin/bash
# capture-showcase.sh — Fresh reboot → console → mouse login → apps → 30fps video
#
# Produces: showcase-capture/showcase-30fps.mp4 (800×600, 30 fps)
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TRANS_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
ROOT_DIR="$(cd "$TRANS_DIR/.." && pwd)"
CAP="${SHOWCASE_OUT:-$ROOT_DIR/showcase-capture}"
SD="${SHOWCASE_SD:-/tmp/tpos-showcase.sd}"
T4="$TRANS_DIR/t4/t4"
UART="${SHOWCASE_UART:-/tmp/fbwm-showcase.uart}"

echo "=== Build showcase fbwm ==="
FBWM_SHOWCASE=1 FBWM_FORCE_AUTO_DEMO=0 bash "$SCRIPT_DIR/build-fbwm.sh"

echo "=== Stage rootfs + WAD ==="
bash "$TRANS_DIR/rootfs/stage.sh"
mkdir -p "$TRANS_DIR/rootfs/output/stage/usr/share/doom"
if [ -f "$TRANS_DIR/doom/freedoom/freedoom1.wad" ]; then
  cp -f "$TRANS_DIR/doom/freedoom/freedoom1.wad" \
        "$TRANS_DIR/rootfs/output/stage/usr/share/doom/"
fi
cp -f "$TRANS_DIR/initramfs/fbwm.flt" "$TRANS_DIR/rootfs/output/stage/usr/bin/fbwm"
if [ -f /tmp/tman.flt ]; then
  cp -f /tmp/tman.flt "$TRANS_DIR/rootfs/output/stage/usr/bin/tman"
elif [ -f "$TRANS_DIR/tpkg/tman.flt" ]; then
  cp -f "$TRANS_DIR/tpkg/tman.flt" "$TRANS_DIR/rootfs/output/stage/usr/bin/tman"
fi
chmod 755 "$TRANS_DIR/rootfs/output/stage/usr/bin/fbwm"
[ -f "$TRANS_DIR/rootfs/output/stage/usr/bin/tman" ] && \
  chmod 755 "$TRANS_DIR/rootfs/output/stage/usr/bin/tman"

echo "=== SD image ==="
TPOS_ROOTFS_MIB=48 bash "$TRANS_DIR/make-sd.sh" \
  "$TRANS_DIR/linux-7/vmlinux" "$SD"

echo "=== Boot + record (console + flips) ==="
rm -rf "$CAP"
mkdir -p "$CAP"
cd "$CAP"

# Kill any previous t4
ps -eo pid=,args= | awk '/\/transputer\/t4\/t4 / && !/awk/ {print $1}' \
  | while read -r p; do kill -9 "$p" 2>/dev/null || true; done
sleep 1

# T4_FB_CONSOLE=1  — kernel printk on the 800×600 FB during boot
# T4_FB_RECORD_ON_FLIP=1 — every guest present
# T4_FB_RECORD=N — also sample during long compute (boot / doom)
# FLIP_LIMIT caps total recorded frames so the run ends cleanly
timeout "${SHOWCASE_TIMEOUT:-2400}" env \
  T4_SD_IMAGE="$SD" \
  T4_SD_BOOT=1 \
  T4_FB_CONSOLE=1 \
  T4_FB_RECORD_ON_FLIP=1 \
  T4_FB_RECORD="${T4_FB_RECORD:-80000}" \
  T4_FB_RECORD_FLIP_LIMIT="${T4_FB_RECORD_FLIP_LIMIT:-900}" \
  "$T4" -s806 -sm 27 -ss \
  >"$UART" 2>&1 || true

N=$(ls fbrec_*.bmp 2>/dev/null | wc -l | tr -d ' ')
echo "Recorded frames: $N"
if [ "$N" -lt 10 ]; then
  echo "Too few frames — see $UART" >&2
  tail -40 "$UART" >&2 || true
  exit 1
fi

echo "=== Encode 30 fps 800×600 H.264 ==="
# Constant 30 fps playback; each BMP is one full FB update (800×600).
ffmpeg -y -framerate 30 -i fbrec_%05d.bmp \
  -c:v libx264 -pix_fmt yuv420p -crf 18 -r 30 \
  showcase-30fps.mp4

ls -lh showcase-30fps.mp4
echo "Markers:"
rg -a 'SHOWCASE|DESKTOP_READY|LOGIN|LAUNCH|SPLASH|FBWM_' "$UART" | head -40 || true
echo "OK: $CAP/showcase-30fps.mp4 ($N frames @ 30fps)"
