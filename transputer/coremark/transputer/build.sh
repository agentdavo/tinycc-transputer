#!/bin/bash
# Build CoreMark for Transputer T4 emulator
set -e

TOP="$(cd "$(dirname "$0")/../../.." && pwd)"
# TCC, NETLOAD and T4 may name another toolchain or emulator build.
TCC="${TCC:-$TOP/transputer-tcc}"
NETLOAD="${NETLOAD:-$TOP/transputer-netload}"
T4="${T4:-$TOP/transputer/t4/t4}"
COREMARK="$TOP/transputer/coremark"
PORT="$COREMARK/transputer"
# CPU picks the processor: t9000 (the default, see below), t805, t800 (the
# original T800, without what the T805 added), or one of the FPU-less t425
# and t414, whose floating point (CoreMark's timing report) calls the
# soft-float helpers in transputer-libtcc1-$CPU.a.
# Another CPU than t9000 builds coremark-$CPU.btl.
CPU="${CPU:-t9000}"
SUFFIX=""
[ "$CPU" = t9000 ] || SUFFIX="-$CPU"
LIBS=""
case "$CPU" in
t425|t414) LIBS="$(dirname "$TCC")/transputer-libtcc1-$CPU.a" ;;
t800|t805) LIBS="$(dirname "$TCC")/transputer-libtcc1-t800.a" ;;
esac
OUT="/tmp/coremark_transputer$SUFFIX"
BTL="${BTL:-$PORT/coremark$SUFFIX.btl}"
RUN_COREMARK="${RUN_COREMARK:-1}"
# Enough iterations for the 10 s a CoreMark run must last, with margin, on
# t4 as of 2026-11-03: -mt9000 does about 173 a second (13.9 s for 2400),
# -mt800, -mt425 and -mt414 about 80 (15 s for 1200; 30 s on the strict
# -s425/-s414, which are half as fast).  CoreMark counts
# a shorter run as an error, so ITERATIONS=N for a quick smoke-test image
# ends "Errors detected" even when every CRC is right.
case "$CPU" in
t9000) ITERATIONS="${ITERATIONS:-2400}" ;;
*)     ITERATIONS="${ITERATIONS:-1200}" ;;
esac

# Beside the BTL go the ELF it was made from, the linker's symbol map
# (ELF.tccsym: address, nm-style type, name) and every command that built
# it, so its functions can be extracted and measured later.
ELF="${BTL%.btl}.elf"
CMDS="${BTL%.btl}.build.txt"

mkdir -p "$OUT"
{
    echo "# CoreMark for the $CPU, $(date -u '+%Y-%m-%d %H:%M UTC')"
    echo "# compiler $(git -C "$TOP" describe --always --dirty 2>/dev/null): $TCC"
} > "$CMDS"
# Record a command in $CMDS, then run it.
run() {
    printf '%q ' "$@" >> "$CMDS"
    echo >> "$CMDS"
    "$@"
}

# Common flags
# The netload target is the T9000-class core (ip/cpu in the gf180mcu project),
# so build for it: -mt9000 turns on the ls/lsx/ss/lbx/xsword/ssub lowering that
# transputer-gen.c already implements but that -mt800 has to synthesise from
# byte pairs.  Measured on the RTL at TOTAL_DATA_SIZE=2000/ITERATIONS=1:
# 16,198,260 -> 8,919,582 cycles (-45.0%), 3,635,271 -> 1,754,658 instructions,
# 39,200 -> 16,904 bytes of text+data, all five CRCs unchanged.
# -mt9000 is now the compiler default too; CPU=t805 builds the T805 subset.
CFLAGS="-m$CPU ${EXTRA_CFLAGS:-} -nostdlib -nostdinc -w -I$PORT -I$COREMARK -I$TOP/include"
DEFS="-DPERFORMANCE_RUN=1 -DITERATIONS=$ITERATIONS -DFLAGS_STR=\"\\\"transputer-tcc\\\"\""

echo "=== Compiling CoreMark for Transputer (ITERATIONS=$ITERATIONS) ==="

# Compile all source files
for src in core_main.c core_list_join.c core_matrix.c core_state.c core_util.c; do
    echo "  CC $src"
    run "$TCC" $CFLAGS $DEFS -c -o "$OUT/${src%.c}.o" "$COREMARK/$src"
done

# Port-specific files
echo "  CC core_portme.c"
run "$TCC" $CFLAGS $DEFS -c -o "$OUT/core_portme.o" "$PORT/core_portme.c"

echo "  CC ee_printf.c"
run "$TCC" $CFLAGS $DEFS -c -o "$OUT/ee_printf.o" "$PORT/ee_printf.c"

echo "  CC cvt.c"
run "$TCC" $CFLAGS $DEFS -c -o "$OUT/cvt.o" "$PORT/cvt.c"

echo "  CC math_stub.c"
run "$TCC" $CFLAGS $DEFS -c -o "$OUT/math_stub.o" "$PORT/math_stub.c"

echo "  AS transputer_ldtimer.S"
run "$TCC" $CFLAGS -c -o "$OUT/transputer_ldtimer.o" "$PORT/transputer_ldtimer.S"

echo "  CC main_wrapper.c"
run "$TCC" $CFLAGS $DEFS -c -o "$OUT/main_wrapper.o" "$PORT/main_wrapper.c"

echo "  AS test_boot.S"
run "$TCC" $CFLAGS -c -o "$OUT/test_boot.o" "$TOP/transputer/tests/test_boot.S"

# Link (-m$CPU too: -mt414 links at the T414's MemStart, #80000048)
echo "=== Linking ==="
run "$TCC" -m$CPU -nostdlib -nostdinc -o "$OUT/coremark.elf" \
    "$OUT/test_boot.o" \
    "$OUT/main_wrapper.o" \
    "$OUT/core_main.o" \
    "$OUT/core_list_join.o" \
    "$OUT/core_matrix.o" \
    "$OUT/core_state.o" \
    "$OUT/core_util.o" \
    "$OUT/core_portme.o" \
    "$OUT/ee_printf.o" \
    "$OUT/cvt.o" \
    "$OUT/math_stub.o" \
    "$OUT/transputer_ldtimer.o" $LIBS
cp "$OUT/coremark.elf" "$ELF"
cp "$OUT/coremark.elf.tccsym" "$ELF.tccsym"

echo "=== Creating BTL ==="
run "$NETLOAD" "$OUT/coremark.elf" "$BTL"

# A T4xx or T8xx build must use only that part's instructions: check, and
# keep the instruction histogram beside the BTL.
CENSUS=""
case "$CPU" in
t425) CENSUS="--hist" ;;
t414|t800|t805) CENSUS="--hist --$CPU" ;;
esac
if [ -n "$CENSUS" ]; then
    python3 "$TOP/transputer/scripts/t425-census.py" $CENSUS "$ELF" \
        > "${BTL%.btl}.census.txt" || { cat "${BTL%.btl}.census.txt"; exit 1; }
    tail -n 1 "${BTL%.btl}.census.txt"
fi

echo "=== CoreMark BTL ready ==="
ls -lh "$BTL" "$ELF" "$ELF.tccsym" "$CMDS"

if [ "$RUN_COREMARK" = "0" ]; then
	exit 0
fi

# A t4 with -s425/-s414 runs a T425 or T414 build as that part, stopping on
# any instruction it lacks; an older t4 runs it as a T800.
if [ -z "${T4_CPU:-}" ]; then
    T4_CPU=-s8
    case "$CPU" in
    t425|t414) grep -qa -- '-s414, -s425' "$T4" && T4_CPU="-s${CPU#t}" ;;
    esac
fi

echo "=== Running on T4 ($T4_CPU) ==="
UART="$OUT/coremark.uart"
rm -f "$UART"
DISPLAY= WAYLAND_DISPLAY= T4_UART_CONSOLE=0 T4_UART_LOG="$UART" \
    timeout 300 "$T4" $T4_CPU -si -se -sb "$BTL" 2>/dev/null || true

echo ""
echo "=== UART Output ==="
tr -d '\000' < "$UART"
