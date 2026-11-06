#!/bin/bash
# build-fbwm.sh — Build TransputerOS framebuffer window manager (bFLT)
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TRANS_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
ROOT_DIR="$(cd "$TRANS_DIR/.." && pwd)"
TCC="${TCC:-$ROOT_DIR/transputer-tcc}"
ELF2FLT="${ELF2FLT:-$ROOT_DIR/transputer-elf2flt}"
UCLIBC="$TRANS_DIR/uClibc-ng"
KERNEL_TREE="${KERNEL_TREE:-linux-7}"
OUT="${1:-$TRANS_DIR/initramfs/fbwm.flt}"
TMP="${TMPDIR:-/tmp}/fbwm-build-$$"
mkdir -p "$TMP"

if [ ! -x "$TCC" ]; then echo "Need transputer-tcc"; exit 1; fi
if [ ! -x "$ELF2FLT" ]; then
  gcc -O2 -o "$ROOT_DIR/transputer-elf2flt" "$TRANS_DIR/tools/transputer-elf2flt.c"
  ELF2FLT="$ROOT_DIR/transputer-elf2flt"
fi

# Canonical TransputerOS userspace include path (TCC + uClibc-ng + kernel UAPI).
# Order matters: arch/transputer bits before generic uClibc, then staged linux headers.
INC="-nostdinc -I$SCRIPT_DIR -I$ROOT_DIR/include/transputer -I$ROOT_DIR/include \
  -I$UCLIBC/include \
  -I$UCLIBC/libc/sysdeps/linux/transputer \
  -I$UCLIBC/libc/sysdeps/linux/common \
  -I$TRANS_DIR/linux-7-headers/include \
  -I$TRANS_DIR/$KERNEL_TREE/arch/transputer/include/uapi \
  -I$TRANS_DIR/$KERNEL_TREE/arch/transputer/include \
  -I$TRANS_DIR/$KERNEL_TREE/include/uapi \
  -I$TRANS_DIR/$KERNEL_TREE/include"
# TCC defines __TINYC__; headers now treat that like a modern C compiler for
# long long / inline.  __GNUC__ pretence remains for any leftover GCC-only gates.
CFLAGS_UCLIBC="-mt9000 -w -DTPOS_HAVE_MOVE2D=1 -D__GNUC__=4 -D__GNUC_MINOR__=4 -D__GNUC_PATCHLEVEL__=0"
# SPINE=1 → SPINE-chip address profile (FB 0x81000000, compose 0x81200000,
# scratch 0x81400000) matching a CONFIG_TRANSPUTER_SPINE_CHIP kernel.
if [ "${SPINE:-0}" = "1" ]; then
  CFLAGS_UCLIBC="$CFLAGS_UCLIBC -DTPOS_SPINE_CHIP"
fi

echo "=== Compiling fbwm ==="
# FBWM_FORCE_AUTO_DEMO=1  — headless showreel (tman underlay → DOOM).
# FBWM_SHOWCASE=1         — mouse login + icon tour for show-off video.
FORCE_DEMO_FLAG=""
if [ "${FBWM_FORCE_AUTO_DEMO:-0}" != "0" ]; then
  FORCE_DEMO_FLAG="-DFBWM_FORCE_AUTO_DEMO=1"
  echo "  (auto-demo ENABLED)"
fi
if [ "${FBWM_SHOWCASE:-0}" != "0" ]; then
  FORCE_DEMO_FLAG="$FORCE_DEMO_FLAG -DFBWM_SHOWCASE=1"
  echo "  (showcase ENABLED)"
fi
$TCC $CFLAGS_UCLIBC $INC -c -o "$TMP/fbwm.o" "$SCRIPT_DIR/fbwm.c"
$TCC $CFLAGS_UCLIBC $INC -c -o "$TMP/fbwm_shm.o" "$SCRIPT_DIR/fbwm_shm.c"
$TCC $CFLAGS_UCLIBC $INC $FORCE_DEMO_FLAG -c -o "$TMP/fbwm_main.o" "$SCRIPT_DIR/fbwm_main.c"
$TCC -nostdlib -nostdinc -c -o "$TMP/crt1.o" "$TRANS_DIR/initramfs/crt1.S"
$TCC -nostdlib $INC -c -o "$TMP/crt_bridge.o" "$TRANS_DIR/initramfs/crt_bridge.c"
$TCC -nostdlib $INC -c -o "$TMP/syscall.o" "$TRANS_DIR/tests/syscall_stub.c"

echo "=== Linking fbwm.elf ==="
env T4_STRICT_UNRESOLVED_CALLS=1 \
$TCC -nostdlib -nostdinc -o "$TMP/fbwm.elf" \
  "$TMP/crt1.o" "$TMP/crt_bridge.o" \
  "$TMP/fbwm_main.o" "$TMP/fbwm.o" "$TMP/fbwm_shm.o" \
  "$TMP/syscall.o" \
  "$UCLIBC/lib/libc.a" \
  "$ROOT_DIR/transputer-libtcc1.a"

echo "=== elf2flt → $OUT ==="
$ELF2FLT "$TMP/fbwm.elf" "$OUT"
ls -lh "$OUT"
rm -rf "$TMP"
echo "OK"
