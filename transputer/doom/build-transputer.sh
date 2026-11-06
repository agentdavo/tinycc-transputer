#!/bin/bash
# build-transputer.sh — Build DOOM for Transputer (TransputerOS / T4 emulator)
#
# Compiles linuxdoom-1.10 with transputer-tcc, links against uClibc-ng,
# and optionally creates a bFLT binary and .tpk package.
#
# Usage:
#   bash transputer/doom/build-transputer.sh [--compile|--link|--bflt|--package|--all]
#
# Prerequisites:
#   - transputer-tcc must be built (bash transputer/build.sh --transputer-tcc)
#   - uClibc-ng must be built (cd transputer/uClibc-ng && make)
#   - transputer-elf2flt must be built (for bFLT conversion)
#
# CPU=t805 (or t800, t425, t414) builds for that part into build-tp-$CPU
# and links its libtcc1 (the soft-float helpers on the T425/T414).  The
# default is the T9000, into build-tp.  uClibc-ng's lib/libc.a is built
# for the T9000 (it has gtu, ls, ss), so another part links
# lib/libc-$CPU.a, which transputer/scripts/build-uclibc-part.sh makes;
# without it the link falls back to libc.a with a warning.
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DOOM_SRC="$SCRIPT_DIR/linuxdoom-1.10"
TRANS_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
ROOT_DIR="$(cd "$TRANS_DIR/.." && pwd)"
CPU="${CPU:-t9000}"
case "$CPU" in
    t9000) BUILD_DIR="$SCRIPT_DIR/build-tp"; LIBTCC1=transputer-libtcc1.a ;;
    t805|t800) BUILD_DIR="$SCRIPT_DIR/build-tp-$CPU"; LIBTCC1=transputer-libtcc1-t800.a ;;
    t425|t414) BUILD_DIR="$SCRIPT_DIR/build-tp-$CPU"; LIBTCC1=transputer-libtcc1-$CPU.a ;;
    *) echo "ERROR: unknown CPU=$CPU (t9000, t805, t800, t425, t414)"; exit 1 ;;
esac
UCLIBC="$TRANS_DIR/uClibc-ng"
LIBC="$UCLIBC/lib/libc.a"
if [ "$CPU" != t9000 ]; then
    if [ -f "$UCLIBC/lib/libc-$CPU.a" ]; then
        LIBC="$UCLIBC/lib/libc-$CPU.a"
    else
        echo "WARNING: no $UCLIBC/lib/libc-$CPU.a: linking the T9000 libc.a," \
             "which has instructions the $CPU lacks" \
             "(transputer/scripts/build-uclibc-part.sh $CPU builds one)"
    fi
fi
INITRAMFS="$TRANS_DIR/initramfs"

TCC="$ROOT_DIR/transputer-tcc"
ELF2FLT="$ROOT_DIR/transputer-elf2flt"

# Compiler flags
CFLAGS="-m$CPU -w"
if [ "$CPU" != t414 ]; then
    CFLAGS="$CFLAGS -DTPOS_HAVE_MOVE2D=1"
fi
CFLAGS="$CFLAGS -D__GNUC__=4 -D__GNUC_MINOR__=4 -D__GNUC_PATCHLEVEL__=0"
CFLAGS="$CFLAGS -DNORMALUNIX -DTRANSPUTER"
CFLAGS="$CFLAGS -include $DOOM_SRC/doom_tp_config.h"
CFLAGS="$CFLAGS -nostdinc"
CFLAGS="$CFLAGS -I$ROOT_DIR/include/transputer -I$ROOT_DIR/include"
CFLAGS="$CFLAGS -I$UCLIBC/include"
CFLAGS="$CFLAGS -I$UCLIBC/libc/sysdeps/linux/transputer"
CFLAGS="$CFLAGS -I$UCLIBC/libc/sysdeps/linux/common"
CFLAGS="$CFLAGS -I$TRANS_DIR/linux-2.6.39/include"
CFLAGS="$CFLAGS -I$DOOM_SRC"
CFLAGS="$CFLAGS -I$TRANS_DIR/fbwm"

# All DOOM source files (engine + game logic)
# Platform files are the transputer-specific _tp.c variants
DOOM_SRCS="
    doomdef.c doomstat.c dstrings.c
    tables.c info.c sounds.c
    f_finale.c f_wipe.c
    d_main.c d_net.c d_items.c
    g_game.c
    m_menu.c m_misc.c m_argv.c m_bbox.c m_fixed.c m_swap.c m_cheat.c m_random.c
    am_map.c
    p_ceilng.c p_doors.c p_enemy.c p_floor.c p_inter.c p_lights.c
    p_map.c p_maputl.c p_plats.c p_pspr.c p_setup.c p_sight.c
    p_spec.c p_switch.c p_mobj.c p_telept.c p_tick.c p_saveg.c p_user.c
    r_bsp.c r_data.c r_draw.c r_main.c r_plane.c r_segs.c r_sky.c r_things.c
    w_wad.c wi_stuff.c v_video.c
    st_lib.c st_stuff.c hu_stuff.c hu_lib.c
    s_sound.c z_zone.c
    i_main.c
"

# Transputer platform layer (replaces i_video.c, i_sound.c, i_system.c, i_net.c)
TP_SRCS="
    i_video_tp.c i_sound_tp.c i_system_tp.c i_net_tp.c
"

# --------------------------------------------------------------------------

check_prereqs() {
    if [ ! -x "$TCC" ]; then
        echo "ERROR: transputer-tcc not found at $TCC"
        echo "Run: bash transputer/build.sh --transputer-tcc"
        exit 1
    fi
    if [ ! -f "$UCLIBC/lib/libc.a" ]; then
        echo "ERROR: uClibc-ng libc.a not found"
        echo "Run: cd transputer/uClibc-ng && make"
        exit 1
    fi
}

do_compile() {
    echo "=== Compiling DOOM for Transputer ==="
    mkdir -p "$BUILD_DIR"

    local count=0
    local total=0
    local failed=0

    # Count total files
    for src in $DOOM_SRCS $TP_SRCS; do
        total=$((total + 1))
    done

    # Compile engine sources
    for src in $DOOM_SRCS; do
        count=$((count + 1))
        local obj="$BUILD_DIR/$(basename "$src" .c).o"
        printf "  [%2d/%2d] %s\n" "$count" "$total" "$src"
        if ! $TCC $CFLAGS -c -o "$obj" "$DOOM_SRC/$src" 2>>"$BUILD_DIR/compile.log"; then
            echo "    FAILED: $src (see $BUILD_DIR/compile.log)"
            failed=$((failed + 1))
        fi
    done

    # Compile transputer platform layer
    for src in $TP_SRCS; do
        count=$((count + 1))
        local obj="$BUILD_DIR/$(basename "$src" .c).o"
        printf "  [%2d/%2d] %s\n" "$count" "$total" "$src"
        if ! $TCC $CFLAGS -c -o "$obj" "$DOOM_SRC/$src" 2>>"$BUILD_DIR/compile.log"; then
            echo "    FAILED: $src (see $BUILD_DIR/compile.log)"
            failed=$((failed + 1))
        fi
    done

    # Shared compositor client used by -windowed mode
    printf "  [++] fbwm_shm.c\n"
    if ! $TCC $CFLAGS -c -o "$BUILD_DIR/fbwm_shm.o" \
            "$TRANS_DIR/fbwm/fbwm_shm.c" 2>>"$BUILD_DIR/compile.log"; then
        echo "    FAILED: fbwm_shm.c (see $BUILD_DIR/compile.log)"
        failed=$((failed + 1))
    fi

    # FixedMul/FixedDiv (m_fixed.c leaves them to this under TRANSPUTER)
    printf "  [++] m_fixed_tp.S\n"
    if ! $TCC -m$CPU -c -o "$BUILD_DIR/m_fixed_tp.o" \
            "$DOOM_SRC/m_fixed_tp.S" 2>>"$BUILD_DIR/compile.log"; then
        echo "    FAILED: m_fixed_tp.S (see $BUILD_DIR/compile.log)"
        failed=$((failed + 1))
    fi
    if [ "$failed" -gt 0 ]; then
        echo "=== Compilation: $failed files FAILED ==="
        return 1
    else
        echo "=== Compilation: $total files + fbwm_shm + m_fixed_tp OK ==="
    fi
}

do_link() {
    echo "=== Linking DOOM ==="

    # Build CRT objects
    local crt1="$BUILD_DIR/crt1.o"
    if [ -f "$INITRAMFS/crt1.S" ]; then
        $TCC -m$CPU -nostdlib -nostdinc -c -o "$crt1" "$INITRAMFS/crt1.S"
    elif [ -f "$UCLIBC/lib/crt1.o" ]; then
        cp "$UCLIBC/lib/crt1.o" "$crt1"
    else
        echo "ERROR: No crt1 found"
        exit 1
    fi

    # Build syscall stub
    local syscall_stub="$BUILD_DIR/syscall_stub.o"
    if [ -f "$TRANS_DIR/tests/syscall_stub.c" ]; then
        $TCC -m$CPU -nostdlib -nostdinc \
            -I"$ROOT_DIR/include/transputer" -I"$ROOT_DIR/include" \
            -I"$UCLIBC/include" \
            -I"$UCLIBC/libc/sysdeps/linux/transputer" \
            -I"$UCLIBC/libc/sysdeps/linux/common" \
            -I"$TRANS_DIR/linux-2.6.39/include" \
            -c -o "$syscall_stub" "$TRANS_DIR/tests/syscall_stub.c"
    elif [ -f /tmp/tpsh_syscall_stub.o ]; then
        cp /tmp/tpsh_syscall_stub.o "$syscall_stub"
    fi

    # CRT bridge (if available)
    local crt_bridge=""
    if [ -f "$INITRAMFS/crt_bridge.c" ]; then
        $TCC -m$CPU -nostdlib -nostdinc \
            -I"$ROOT_DIR/include/transputer" -I"$ROOT_DIR/include" \
            -I"$UCLIBC/include" \
            -I"$UCLIBC/libc/sysdeps/linux/transputer" \
            -I"$UCLIBC/libc/sysdeps/linux/common" \
            -I"$TRANS_DIR/linux-2.6.39/include" \
            -c -o "$BUILD_DIR/crt_bridge.o" "$INITRAMFS/crt_bridge.c"
        crt_bridge="$BUILD_DIR/crt_bridge.o"
    fi

    # Collect all DOOM .o files
    local objs=""
    for src in $DOOM_SRCS $TP_SRCS; do
        local obj="$BUILD_DIR/$(basename "$src" .c).o"
        if [ -f "$obj" ]; then
            objs="$objs $obj"
        fi
    done

    # Link: CRT → CRT_BRIDGE → DOOM objects → SHM compositor → SYSCALL → LIBC
    $TCC -m$CPU -nostdlib -nostdinc \
        -o "$BUILD_DIR/doom.elf" \
        "$crt1" \
        $crt_bridge \
        $objs \
        "$BUILD_DIR/m_fixed_tp.o" \
        "$BUILD_DIR/fbwm_shm.o" \
        "$syscall_stub" \
        "$LIBC" \
        "$ROOT_DIR/$LIBTCC1"

    local size=$(stat -c%s "$BUILD_DIR/doom.elf" 2>/dev/null || echo "?")
    echo "=== Linked: doom.elf ($size bytes) ==="
}

do_bflt() {
    echo "=== Converting to bFLT ==="

    if [ ! -x "$ELF2FLT" ]; then
        echo "ERROR: transputer-elf2flt not found at $ELF2FLT"
        exit 1
    fi

    if [ ! -f "$BUILD_DIR/doom.elf" ]; then
        echo "ERROR: doom.elf not found — run --link first"
        exit 1
    fi

    $ELF2FLT "$BUILD_DIR/doom.elf" "$BUILD_DIR/doom.flt"

    local size=$(stat -c%s "$BUILD_DIR/doom.flt" 2>/dev/null || echo "?")
    echo "=== Created: doom.flt ($size bytes) ==="
}

do_package() {
    echo "=== Creating doom-1.0.0.tpz package ==="

    local pkg_out="$TRANS_DIR/packages/output"
    local staging="$pkg_out/staging-doom"
    local freedoom_dir="$SCRIPT_DIR/freedoom"

    mkdir -p "$pkg_out"
    rm -rf "$staging"
    mkdir -p "$staging/usr/games" "$staging/usr/share/doom" "$staging/usr/share/doc/doom"

    if [ -f "$BUILD_DIR/doom.flt" ]; then
        cp "$BUILD_DIR/doom.flt" "$staging/usr/games/doom"
        chmod 755 "$staging/usr/games/doom"
    else
        echo "ERROR: doom.flt not found — run --bflt first"
        exit 1
    fi

    # Include Freedoom WAD (freedoom1.wad = 4 episodes, registered-compatible)
    if [ -f "$freedoom_dir/freedoom1.wad" ]; then
        cp "$freedoom_dir/freedoom1.wad" "$staging/usr/share/doom/freedoom1.wad"
        echo "  Included freedoom1.wad ($(du -sh "$freedoom_dir/freedoom1.wad" | cut -f1))"
    else
        echo "WARNING: freedoom1.wad not found in $freedoom_dir"
    fi

    # Include Freedoom license
    if [ -f "$freedoom_dir/COPYING.txt" ]; then
        cp "$freedoom_dir/COPYING.txt" "$staging/usr/share/doc/doom/FREEDOOM-LICENSE.txt"
    fi

    # Include a README
    cat > "$staging/usr/share/doc/doom/README" <<'DOCEOF'
DOOM for TransputerOS
=====================

Port of id Software's DOOM (linuxdoom-1.10) to the INMOS Transputer.

Renders at 320x200, scaled 2x to 640x400, centered on 800x600 ARGB
framebuffer. No sound (stubbed). Single-player only.

USAGE:
  Place a DOOM WAD file (doom1.wad, doom.wad, or doom2.wad) in the
  current directory or set DOOMWADDIR to the WAD file location, then:

    doom [-file wad] [-warp e m] [-skill s] [-nomonsters] [-devparm]

  Examples:
    doom                        # Auto-detect WAD in current dir
    doom -file /data/doom1.wad  # Specify WAD path
    doom -warp 1 1 -skill 4     # Start at E1M1, Ultra-Violence

WAD FILES:
  This package does NOT include WAD data files. You must provide your own:
  - doom1.wad   (shareware, free to distribute)
  - doom.wad    (registered)
  - doom2.wad   (commercial)

CONTROLS:
  Arrow keys    Move/turn
  Ctrl          Fire
  Space         Use/Open
  Alt           Strafe
  Shift         Run
  1-7           Weapon select
  Tab           Automap
  Escape        Menu
  F1            Help

LICENSE:
  DOOM source: GNU General Public License v2 (id Software / ZeniMax Media)
  Transputer port: Same license as the original source.
DOCEOF

    # Create .tpz-info manifest
    local nfiles
    nfiles=$(cd "$staging" && find . -type f ! -name '.tpz-info' | wc -l)
    local inst_size
    inst_size=$(du -sb "$staging" | cut -f1)

    cat > "$staging/.tpz-info" <<INFOEOF
Package: doom
Version: 1.0.0
Description: DOOM engine with Freedoom WAD (doom -simplefb for FB play)
Size: ${inst_size}
Arch: transputer
Depends:
License: GPL-2.0
Maintainer: djs
Files: ${nfiles}
INFOEOF

    # Create .tpz (gzip'd tar, manifest first)
    cd "$staging"
    (echo .tpz-info; find . -type f ! -name '.tpz-info' | sort) | \
        tar czf "$pkg_out/doom-1.0.0.tpz" -T -
    cd "$SCRIPT_DIR"
    rm -rf "$staging"

    echo "Package: doom-1.0.0.tpz ($(du -sh "$pkg_out/doom-1.0.0.tpz" | cut -f1))"
}

do_all() {
    do_compile
    do_link
    do_bflt
    do_package
}

# --------------------------------------------------------------------------
# Main
# --------------------------------------------------------------------------

check_prereqs

case "${1:-all}" in
    --compile)  do_compile ;;
    --link)     do_link ;;
    --bflt)     do_bflt ;;
    --package)  do_package ;;
    --all|all)  do_all ;;
    --clean)
        echo "Cleaning build directory..."
        rm -rf "$BUILD_DIR"
        ;;
    --help|-h)
        echo "Usage: $0 [--compile|--link|--bflt|--package|--all|--clean]"
        echo ""
        echo "  --compile   Compile all DOOM .c files to .o"
        echo "  --link      Link .o files into doom.elf"
        echo "  --bflt      Convert doom.elf to doom.flt (bFLT)"
        echo "  --package   Create doom.tpk for TransputerOS"
        echo "  --all       Do everything (default)"
        echo "  --clean     Remove build directory"
        ;;
    *)
        echo "Unknown option: $1"
        echo "Run: $0 --help"
        exit 1
        ;;
esac
