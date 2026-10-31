#!/bin/bash
# Build tpkg CLI package manager for TransputerOS
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TRANS_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
ROOT_DIR="$(cd "$TRANS_DIR/.." && pwd)"
UCLIBC="$TRANS_DIR/uClibc-ng"
INITRAMFS="$TRANS_DIR/initramfs"
TCC="$ROOT_DIR/transputer-tcc"
ELF2FLT="$ROOT_DIR/transputer-elf2flt"
KERNEL_TREE="${KERNEL_TREE:-linux-7}"
KERNEL_INCLUDE="$TRANS_DIR/$KERNEL_TREE/include"

echo "=== Building tpkg ==="

# Compile
$TCC -mt9000 -w \
    -D__GNUC__=4 -D__GNUC_MINOR__=4 -D__GNUC_PATCHLEVEL__=0 \
    -nostdinc \
    -I"$ROOT_DIR/include/transputer" -I"$ROOT_DIR/include" \
    -I"$UCLIBC/include" \
    -I"$UCLIBC/libc/sysdeps/linux/transputer" \
    -I"$UCLIBC/libc/sysdeps/linux/common" \
    -I"$KERNEL_INCLUDE" \
    -c -o /tmp/tpkg.o "$SCRIPT_DIR/tpkg.c"

# Link
local_crt1=""
if [ -f "$INITRAMFS/crt1.S" ]; then
    $TCC -nostdlib -nostdinc -c -o /tmp/tpkg_crt1.o "$INITRAMFS/crt1.S"
    local_crt1="/tmp/tpkg_crt1.o"
elif [ -f "$UCLIBC/lib/crt1.o" ]; then
    local_crt1="$UCLIBC/lib/crt1.o"
fi

local_crt_bridge=""
if [ -f "$INITRAMFS/crt_bridge.c" ]; then
    $TCC -nostdlib -nostdinc \
        -I"$ROOT_DIR/include/transputer" -I"$ROOT_DIR/include" \
        -I"$UCLIBC/include" \
        -I"$UCLIBC/libc/sysdeps/linux/transputer" \
        -I"$UCLIBC/libc/sysdeps/linux/common" \
        -I"$KERNEL_INCLUDE" \
        -c -o /tmp/tpkg_crt_bridge.o "$INITRAMFS/crt_bridge.c"
    local_crt_bridge="/tmp/tpkg_crt_bridge.o"
fi

local_syscall=""
if [ -f "$TRANS_DIR/tests/syscall_stub.c" ]; then
    $TCC -nostdlib -nostdinc \
        -I"$ROOT_DIR/include/transputer" -I"$ROOT_DIR/include" \
        -I"$UCLIBC/include" \
        -I"$UCLIBC/libc/sysdeps/linux/transputer" \
        -I"$UCLIBC/libc/sysdeps/linux/common" \
        -I"$KERNEL_INCLUDE" \
        -c -o /tmp/tpkg_syscall.o "$TRANS_DIR/tests/syscall_stub.c"
    local_syscall="/tmp/tpkg_syscall.o"
fi

$TCC -nostdlib -nostdinc \
    -o /tmp/tpkg.elf \
    $local_crt1 \
    $local_crt_bridge \
    /tmp/tpkg.o \
    $local_syscall \
    "$UCLIBC/lib/libc.a"

# Convert to bFLT
$ELF2FLT /tmp/tpkg.elf /tmp/tpkg.flt

echo "  tpkg.flt: $(du -sh /tmp/tpkg.flt | cut -f1)"

echo "=== Building tpkg-ui ==="

# Compile UI
$TCC -mt9000 -w \
    -D__GNUC__=4 -D__GNUC_MINOR__=4 -D__GNUC_PATCHLEVEL__=0 \
    -nostdinc \
    -I"$ROOT_DIR/include/transputer" -I"$ROOT_DIR/include" \
    -I"$UCLIBC/include" \
    -I"$UCLIBC/libc/sysdeps/linux/transputer" \
    -I"$UCLIBC/libc/sysdeps/linux/common" \
    -I"$KERNEL_INCLUDE" \
    -c -o /tmp/tpkg_ui.o "$SCRIPT_DIR/tpkg_ui.c"

# Link UI
$TCC -nostdlib -nostdinc \
    -o /tmp/tpkg_ui.elf \
    $local_crt1 \
    $local_crt_bridge \
    /tmp/tpkg_ui.o \
    $local_syscall \
    "$UCLIBC/lib/libc.a"

# Convert to bFLT
$ELF2FLT /tmp/tpkg_ui.elf /tmp/tpkg_ui.flt

echo "  tpkg-ui.flt: $(du -sh /tmp/tpkg_ui.flt | cut -f1)"

# Build ttop if source exists
if [ -f "$SCRIPT_DIR/ttop.c" ]; then
    echo "=== Building ttop ==="

    $TCC -mt9000 -w \
        -D__GNUC__=4 -D__GNUC_MINOR__=4 -D__GNUC_PATCHLEVEL__=0 \
        -nostdinc \
        -I"$ROOT_DIR/include/transputer" -I"$ROOT_DIR/include" \
        -I"$UCLIBC/include" \
        -I"$UCLIBC/libc/sysdeps/linux/transputer" \
        -I"$UCLIBC/libc/sysdeps/linux/common" \
        -I"$KERNEL_INCLUDE" \
        -c -o /tmp/ttop.o "$SCRIPT_DIR/ttop.c"

    $TCC -nostdlib -nostdinc \
        -o /tmp/ttop.elf \
        $local_crt1 \
        $local_crt_bridge \
        /tmp/ttop.o \
        $local_syscall \
        "$UCLIBC/lib/libc.a"

    $ELF2FLT /tmp/ttop.elf /tmp/ttop.flt

    echo "  ttop.flt: $(du -sh /tmp/ttop.flt | cut -f1)"
fi

# Build tfiles if source exists
if [ -f "$SCRIPT_DIR/tfiles.c" ]; then
    echo "=== Building tfiles ==="

    $TCC -mt9000 -w \
        -D__GNUC__=4 -D__GNUC_MINOR__=4 -D__GNUC_PATCHLEVEL__=0 \
        -nostdinc \
        -I"$ROOT_DIR/include/transputer" -I"$ROOT_DIR/include" \
        -I"$UCLIBC/include" \
        -I"$UCLIBC/libc/sysdeps/linux/transputer" \
        -I"$UCLIBC/libc/sysdeps/linux/common" \
        -I"$KERNEL_INCLUDE" \
        -c -o /tmp/tfiles.o "$SCRIPT_DIR/tfiles.c"

    $TCC -nostdlib -nostdinc \
        -o /tmp/tfiles.elf \
        $local_crt1 \
        $local_crt_bridge \
        /tmp/tfiles.o \
        $local_syscall \
        "$UCLIBC/lib/libc.a"

    $ELF2FLT /tmp/tfiles.elf /tmp/tfiles.flt

    echo "  tfiles.flt: $(du -sh /tmp/tfiles.flt | cut -f1)"
fi

# Build tman if source exists (Task Manager — ships as tman-*.tpz package)
if [ -f "$SCRIPT_DIR/tman.c" ]; then
    echo "=== Building tman (package source: tpkg/tman.c → tman.tpz) ==="

    $TCC -mt9000 -w \
        -D__GNUC__=4 -D__GNUC_MINOR__=4 -D__GNUC_PATCHLEVEL__=0 \
        -nostdinc \
        -I"$ROOT_DIR/include/transputer" -I"$ROOT_DIR/include" \
        -I"$UCLIBC/include" \
        -I"$UCLIBC/libc/sysdeps/linux/transputer" \
        -I"$UCLIBC/libc/sysdeps/linux/common" \
        -I"$TRANS_DIR/linux-7-headers/include" \
        -I"$KERNEL_INCLUDE" \
        -c -o /tmp/tman.o "$SCRIPT_DIR/tman.c"

    $TCC -nostdlib -nostdinc \
        -o /tmp/tman.elf \
        $local_crt1 \
        $local_crt_bridge \
        /tmp/tman.o \
        $local_syscall \
        "$UCLIBC/lib/libc.a"

    $ELF2FLT /tmp/tman.elf /tmp/tman.flt
    # Keep a copy next to sources for package builds
    cp /tmp/tman.flt "$SCRIPT_DIR/tman.flt"

    echo "  tman.flt: $(du -sh /tmp/tman.flt | cut -f1)"
    echo "  Next: bash transputer/packages/build-packages.sh tman"
fi
