#!/bin/bash
# Build a native TCC that runs ON the transputer (targeting transputer).
#
# This compiles TCC's own source code using the transputer cross-compiler,
# links against uClibc-ng, and produces a transputer ELF binary.
#
# Prerequisites:
#   bash transputer/build.sh --transputer-tcc   # build cross-compiler first
#
# Usage:
#   bash transputer/build-native-tcc.sh
#
# Output:
#   /tmp/native-tcc.elf     — Transputer ELF of TCC
#   /tmp/native-tcc.btl     — Bootable BTL (if transputer-netload available)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

TCC_BIN="$ROOT_DIR/transputer-tcc"
NETLOAD_BIN="$ROOT_DIR/transputer-netload"
UCLIBC_DIR="$ROOT_DIR/transputer/uClibc-ng"
TESTS_DIR="$ROOT_DIR/transputer/tests"
TCC_INCLUDE="$ROOT_DIR/include"
KERNEL_TREE="${KERNEL_TREE:-linux-7}"
LINUX_INCLUDE="$ROOT_DIR/transputer/$KERNEL_TREE/include"
LINUX_UAPI_INCLUDE="$ROOT_DIR/transputer/$KERNEL_TREE/include/uapi"
LINUX_ARCH_INCLUDE="$ROOT_DIR/transputer/$KERNEL_TREE/arch/transputer/include"
LINUX_ARCH_GENERATED_INCLUDE="$ROOT_DIR/transputer/$KERNEL_TREE/arch/transputer/include/generated/uapi"
OUTDIR="/tmp/native-tcc-build"

# Verify cross-compiler exists
if [[ ! -x "$TCC_BIN" ]]; then
    echo "ERROR: transputer-tcc not found. Run: bash transputer/build.sh --transputer-tcc"
    exit 1
fi

# Verify uClibc-ng is built
if [[ ! -f "$UCLIBC_DIR/lib/libc.a" ]]; then
    echo "ERROR: uClibc-ng libc.a not found. Build it first:"
    echo "  cd transputer/uClibc-ng && make ARCH=transputer defconfig && LD_LIBRARY_PATH='' make"
    exit 1
fi

# Clean output directory
rm -rf "$OUTDIR"
mkdir -p "$OUTDIR"

echo "=== Building Native Transputer TCC ==="
echo "Cross-compiler: $TCC_BIN"
echo "uClibc-ng:      $UCLIBC_DIR"
echo "Output dir:     $OUTDIR"
echo ""

# ---------------------------------------------------------------------------
# Step 1: Create a config.h for the native transputer build
# ---------------------------------------------------------------------------
cat > "$OUTDIR/config.h" << 'CONFIGEOF'
/* config.h for native transputer TCC (runs ON transputer, targets transputer) */
#define TCC_VERSION "0.9.28rc"

/* We are compiling TCC with TCC — identify compiler */
#define CC_NAME CC_tinyc
#define GCC_MAJOR 0
#define GCC_MINOR 0

/* Target: transputer */
#define TCC_TARGET_TRANSPUTER 1
#define CONFIG_TRIPLET "transputer-linux-gnu"

/* Static build — no dynamic linking on transputer */
#define CONFIG_TCC_STATIC 1

/* Embed predefs so we don't need to find tccdefs.h at runtime */
#define CONFIG_TCC_PREDEFS 1

/* No backtrace support (requires native execution support) */
#define CONFIG_TCC_BACKTRACE 0

/* No bounds checking */
#define CONFIG_TCC_BCHECK 0

#ifndef CONFIG_TCCDIR
#define CONFIG_TCCDIR "/usr/local/lib/tcc"
#endif
CONFIGEOF

echo "[1/5] Created config.h"

# Create a shim override directory for headers that need TCC-compatible versions.
# uClibc-ng's math.h uses complex macro patterns (__MATHCALLI) that TCC can't parse.
SHIM_DIR="$OUTDIR/shim"
mkdir -p "$SHIM_DIR"

# semaphore.h — uClibc-ng doesn't have pthreads/semaphores; provide stub
cat > "$SHIM_DIR/semaphore.h" << 'SEMEOF'
/* Stub semaphore.h for native transputer TCC (no threads) */
#ifndef _SEMAPHORE_H
#define _SEMAPHORE_H 1
typedef int sem_t;
static inline int sem_init(sem_t *s, int p, unsigned v) { (void)s;(void)p;(void)v; return 0; }
static inline int sem_wait(sem_t *s) { (void)s; return 0; }
static inline int sem_post(sem_t *s) { (void)s; return 0; }
#endif
SEMEOF

cat > "$SHIM_DIR/math.h" << 'MATHEOF'
/* Minimal math.h for native transputer TCC build.
 * TCC only needs ldexp/ldexpl from math.h.
 * The full uClibc-ng math.h has complex macros that TCC cannot parse. */
#ifndef _MATH_H
#define _MATH_H 1
#define HUGE_VAL  (__builtin_huge_val())
#define HUGE_VALF (__builtin_huge_valf())
#define INFINITY  (__builtin_inff())
#define NAN       (__builtin_nanf(""))
extern double ldexp(double __x, int __exp);
extern long double ldexpl(long double __x, int __exp);
extern double frexp(double __x, int *__exp);
#endif
MATHEOF

# ---------------------------------------------------------------------------
# Step 2: Build crt0.o from uClibc-ng (if not already built)
# ---------------------------------------------------------------------------
CRT0_OBJ="$OUTDIR/crt0.o"
CRT0_SRC="$UCLIBC_DIR/libc/sysdeps/linux/transputer/crt0.S"

if [[ -f "$CRT0_SRC" ]]; then
    "$TCC_BIN" -mt9000 -w -nostdlib -nostdinc \
        -I"$TCC_INCLUDE/transputer" -I"$TCC_INCLUDE" \
        -c -o "$CRT0_OBJ" "$CRT0_SRC"
    echo "[2/5] Built crt0.o"
else
    echo "[2/5] SKIP: crt0.S not found (will use test_boot.S fallback)"
    CRT0_OBJ=""
fi

# ---------------------------------------------------------------------------
# Step 3: Compile TCC in ONE_SOURCE mode for transputer
# ---------------------------------------------------------------------------
#
# ONE_SOURCE mode: tcc.c includes libtcc.c which includes all backend files.
# This is TCC's standard way of building itself as a single compilation unit.
#
# Key defines:
#   -DONE_SOURCE=1              — single-file compilation mode
#   -DTCC_TARGET_TRANSPUTER     — target the transputer backend
#   -DCONFIG_TCC_STATIC         — no dynamic loading (no dlfcn.h)
#   -DCONFIG_TCC_PREDEFS=1      — embed predefs (tccdefs_.h)
#   -DCONFIG_TCC_BACKTRACE=0    — disable backtrace
#   -DCONFIG_TCC_BCHECK=0       — disable bounds checking
#   -D__TINYC__                 — already defined by transputer-tcc
#
# Include paths:
#   -I$OUTDIR               — our custom config.h (takes priority)
#   -I$ROOT_DIR              — TCC source dir (tcc.h, libtcc.h, elf.h, etc.)
#   -I$TCC_INCLUDE           — TCC's include/ (stdarg.h, stddef.h, etc.)
#   -I$UCLIBC_DIR/include    — uClibc-ng headers (stdio.h, stdlib.h, etc.)
#   -I$LINUX_INCLUDE         — Linux kernel headers (for asm/types.h etc.)

echo "[3/5] Compiling tcc.c (ONE_SOURCE mode) — this may take a while..."

# Common compile flags
CFLAGS=(
    -mt9000
    -w                          # suppress warnings (TCC source generates many)
    -nostdlib
    -nostdinc
    -DONE_SOURCE=1
    -DTCC_TARGET_TRANSPUTER=1
    -DCONFIG_TCC_STATIC=1
    -DCONFIG_TCC_PREDEFS=1
    -DCONFIG_TCC_BACKTRACE=0
    -DCONFIG_TCC_BCHECK=0
    -D__GNUC__=4
    -D__GNUC_MINOR__=4
    -D__GNUC_PATCHLEVEL__=0
    -D_GNU_SOURCE
    -I"$OUTDIR"
    -I"$SHIM_DIR"
    -I"$ROOT_DIR"
    -I"$TCC_INCLUDE/transputer" -I"$TCC_INCLUDE"
    -I"$UCLIBC_DIR/include"
    -I"$LINUX_ARCH_INCLUDE"
    -I"$LINUX_ARCH_GENERATED_INCLUDE"
    -I"$LINUX_UAPI_INCLUDE"
    -I"$LINUX_INCLUDE"
)

# Compile tcc.c → tcc.o
"$TCC_BIN" "${CFLAGS[@]}" \
    -c -o "$OUTDIR/tcc.o" \
    "$ROOT_DIR/tcc.c" \
    2>&1 | tee "$OUTDIR/compile.log"

COMPILE_STATUS=${PIPESTATUS[0]}
if [[ $COMPILE_STATUS -ne 0 ]]; then
    echo ""
    echo "COMPILE FAILED (exit $COMPILE_STATUS)"
    echo "See $OUTDIR/compile.log for details"
    exit 1
fi

# Show object size
OBJ_SIZE=$(stat -c%s "$OUTDIR/tcc.o" 2>/dev/null || stat -f%z "$OUTDIR/tcc.o" 2>/dev/null || echo "?")
echo "    tcc.o size: $OBJ_SIZE bytes"

# ---------------------------------------------------------------------------
# Step 4: Link native TCC
# ---------------------------------------------------------------------------
echo "[4/5] Linking native-tcc.elf..."

# Build syscall stub if needed
SYSCALL_OBJ="$TESTS_DIR/syscall_stub.o"
if [[ ! -f "$SYSCALL_OBJ" ]]; then
    "$TCC_BIN" -mt9000 -w -nostdlib -nostdinc \
        -I"$TCC_INCLUDE/transputer" -I"$TCC_INCLUDE" -I"$UCLIBC_DIR/include" \
        -I"$LINUX_ARCH_INCLUDE" -I"$LINUX_ARCH_GENERATED_INCLUDE" \
        -I"$LINUX_UAPI_INCLUDE" \
        -I"$LINUX_INCLUDE" \
        -c -o "$SYSCALL_OBJ" "$TESTS_DIR/syscall_stub.c"
    echo "    Built syscall_stub.o"
fi

# Link everything together
# Order: crt0.o crt1.o tcc.o syscall_stub.o libc.a
LINK_OBJS=()
if [[ -n "$CRT0_OBJ" && -f "$CRT0_OBJ" ]]; then
    LINK_OBJS+=("$CRT0_OBJ")
fi
LINK_OBJS+=("$UCLIBC_DIR/lib/crt1.o")
LINK_OBJS+=("$OUTDIR/tcc.o")
LINK_OBJS+=("$SYSCALL_OBJ")
LINK_OBJS+=("$UCLIBC_DIR/lib/libc.a")

"$TCC_BIN" -mt9000 -w -nostdlib -static \
    -o "$OUTDIR/native-tcc.elf" \
    "${LINK_OBJS[@]}" \
    2>&1 | tee "$OUTDIR/link.log"

LINK_STATUS=${PIPESTATUS[0]}
if [[ $LINK_STATUS -ne 0 ]]; then
    echo ""
    echo "LINK FAILED (exit $LINK_STATUS)"
    echo "See $OUTDIR/link.log for details"
    exit 1
fi

ELF_SIZE=$(stat -c%s "$OUTDIR/native-tcc.elf" 2>/dev/null || stat -f%z "$OUTDIR/native-tcc.elf" 2>/dev/null || echo "?")
echo "    native-tcc.elf size: $ELF_SIZE bytes"

# Copy to /tmp for easy access
cp "$OUTDIR/native-tcc.elf" /tmp/native-tcc.elf

# Native TCC is installed as a normal TransputerOS executable, so also make
# the bFLT consumed by packages/build-packages.sh.
ELF2FLT_BIN="$ROOT_DIR/transputer-elf2flt"
if [[ -x "$ELF2FLT_BIN" ]]; then
    "$ELF2FLT_BIN" "$OUTDIR/native-tcc.elf" /tmp/native-tcc.flt
else
    echo "ERROR: transputer-elf2flt not found: $ELF2FLT_BIN"
    exit 1
fi

# ---------------------------------------------------------------------------
# Step 5: Create BTL (optional)
# ---------------------------------------------------------------------------
if [[ -x "$NETLOAD_BIN" ]]; then
    echo "[5/5] Creating native-tcc.btl..."
    "$NETLOAD_BIN" "$OUTDIR/native-tcc.elf" "$OUTDIR/native-tcc.btl" 2>/dev/null || true
    if [[ -f "$OUTDIR/native-tcc.btl" ]]; then
        BTL_SIZE=$(stat -c%s "$OUTDIR/native-tcc.btl" 2>/dev/null || stat -f%z "$OUTDIR/native-tcc.btl" 2>/dev/null || echo "?")
        echo "    native-tcc.btl size: $BTL_SIZE bytes"
        cp "$OUTDIR/native-tcc.btl" /tmp/native-tcc.btl
    fi
else
    echo "[5/5] SKIP: transputer-netload not found"
fi

echo ""
echo "=== Native Transputer TCC Build Complete ==="
echo "  ELF: /tmp/native-tcc.elf ($ELF_SIZE bytes)"
echo "  bFLT: /tmp/native-tcc.flt"
echo "  Build dir: $OUTDIR/"
echo ""
echo "To test (once transputer OS is running):"
echo "  # Copy native-tcc.elf to transputer filesystem"
echo "  # Run: ./native-tcc -c -o hello.o hello.c"
