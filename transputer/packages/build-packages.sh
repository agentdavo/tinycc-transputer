#!/bin/bash
# build-packages.sh — Create TransputerOS installable packages (.tpz)
#
# .tpz = gzip'd tar with .tpz-info manifest (see TPZ-FORMAT.md)
# Install: tar xzf package.tpz -C /
# Manage:  tpkg install/remove/list/info
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TRANS_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
ROOT_DIR="$(cd "$TRANS_DIR/.." && pwd)"
PKG_OUT="$SCRIPT_DIR/output"

mkdir -p "$PKG_OUT"

# -----------------------------------------------------------------------
# Helper: create .tpz-info manifest and produce final .tpz
#   $1 = staging dir
#   $2 = package name
#   $3 = version
#   $4 = description
#   $5 = license (SPDX)
#   $6 = depends (comma-separated, empty string for none)
# -----------------------------------------------------------------------
make_tpz() {
    local staging="$1"
    local name="$2"
    local version="$3"
    local desc="$4"
    local license="$5"
    local depends="${6:-}"
    local outfile="$PKG_OUT/${name}-${version}.tpz"

    # Count files (exclude directories and .tpz-info itself)
    local nfiles
    nfiles=$(cd "$staging" && find . -type f ! -name '.tpz-info' | wc -l)

    # Calculate installed size
    local size
    size=$(du -sb "$staging" | cut -f1)

    # Write manifest (MUST be first entry in tar)
    cat > "$staging/.tpz-info" <<EOF
Package: ${name}
Version: ${version}
Description: ${desc}
Size: ${size}
Arch: transputer
Depends: ${depends}
License: ${license}
Maintainer: djs
Files: ${nfiles}
EOF

    # Create .tpz (gzip'd tar, .tpz-info first)
    cd "$staging"
    (echo .tpz-info; find . -type f ! -name '.tpz-info' | sort) | \
        tar czf "$outfile" -T -
    cd "$SCRIPT_DIR"
    rm -rf "$staging"

    local pkgsize
    pkgsize=$(du -sh "$outfile" | cut -f1)
    echo "  ${name}-${version}.tpz  (${pkgsize}, ${nfiles} files, installed ${size} bytes)"
}

# -----------------------------------------------------------------------
# Package: tcc-toolchain (native compiler + headers + libs)
# -----------------------------------------------------------------------
build_tcc_toolchain() {
    echo "=== Building tcc-toolchain.tpz ==="
    local staging="$PKG_OUT/staging-tcc"
    rm -rf "$staging"
    mkdir -p "$staging/usr/local/bin" \
             "$staging/usr/local/lib/tcc/include" \
             "$staging/usr/include" \
             "$staging/usr/lib"

    if [ -f /tmp/native-tcc.flt ]; then
        cp /tmp/native-tcc.flt "$staging/usr/local/bin/tcc"
        chmod 755 "$staging/usr/local/bin/tcc"
    else
        echo "  SKIP: /tmp/native-tcc.flt not found"
        rm -rf "$staging"
        return 0
    fi

    cp -r "$ROOT_DIR/include"/* "$staging/usr/local/lib/tcc/include/" 2>/dev/null || true
    cp -r "$TRANS_DIR/uClibc-ng/include"/* "$staging/usr/include/" 2>/dev/null || true
    cp "$TRANS_DIR/uClibc-ng/lib/libc.a" "$staging/usr/lib/" 2>/dev/null || true
    cp "$TRANS_DIR/uClibc-ng/lib/crt1.o" "$staging/usr/lib/" 2>/dev/null || true
    cp "$TRANS_DIR/uClibc-ng/lib/crti.o" "$staging/usr/lib/" 2>/dev/null || true
    cp "$TRANS_DIR/uClibc-ng/lib/crtn.o" "$staging/usr/lib/" 2>/dev/null || true
    [ -f /tmp/tpsh_syscall_stub.o ] && cp /tmp/tpsh_syscall_stub.o "$staging/usr/lib/syscall_stub.o"

    make_tpz "$staging" "tcc-toolchain" "0.9.28" \
        "TCC native compiler, uClibc-ng headers and libraries" \
        "LGPL-2.1" ""
}

# -----------------------------------------------------------------------
# Package: fbwm (framebuffer window manager)
# -----------------------------------------------------------------------
build_fbwm() {
    echo "=== Building fbwm.tpz ==="
    local staging="$PKG_OUT/staging-fbwm"
    local fbwm_bin=""
    rm -rf "$staging"
    mkdir -p "$staging/usr/bin"

    # Prefer built artifacts (initramfs copy, then /tmp, then build script)
    for cand in \
        "$TRANS_DIR/initramfs/fbwm.flt" \
        /tmp/fbwm.flt \
        "$TRANS_DIR/fbwm/fbwm.flt"; do
        if [ -f "$cand" ]; then fbwm_bin="$cand"; break; fi
    done

    if [ -z "$fbwm_bin" ] && [ -x "$TRANS_DIR/fbwm/build-fbwm.sh" ]; then
        echo "  Building fbwm via build-fbwm.sh..."
        bash "$TRANS_DIR/fbwm/build-fbwm.sh" "$TRANS_DIR/initramfs/fbwm.flt" || true
        [ -f "$TRANS_DIR/initramfs/fbwm.flt" ] && fbwm_bin="$TRANS_DIR/initramfs/fbwm.flt"
    fi

    if [ -z "$fbwm_bin" ] || [ ! -f "$fbwm_bin" ]; then
        echo "  SKIP: fbwm.flt not found (run: bash transputer/fbwm/build-fbwm.sh)"
        rm -rf "$staging"
        return 0
    fi

    cp "$fbwm_bin" "$staging/usr/bin/fbwm"
    chmod 755 "$staging/usr/bin/fbwm"

    # Install paths match tpkg + TPZ-FORMAT (relative, no leading /)
    mkdir -p "$staging/usr/share/doc/fbwm"
    cat > "$staging/usr/share/doc/fbwm/README" <<'EOF'
fbwm — TransputerOS framebuffer window manager

  Splash → login → desktop (icons) → terminal

  Keys: any key advances splash/login
        t = GUI terminal
        s = spawn /bin/tpsh or BusyBox sh (UART)
        ESC = leave desktop

  Paths after: tpkg install fbwm-1.0.0.tpz
    /usr/bin/fbwm
EOF

    make_tpz "$staging" "fbwm" "1.0.0" \
        "TransputerOS simple FB window manager (splash/login/desktop)" \
        "MIT" ""
}

# -----------------------------------------------------------------------
# Package: doom (DOOM for Transputer + Freedoom WAD)
# -----------------------------------------------------------------------
build_doom() {
    echo "=== Building doom.tpz ==="
    local staging="$PKG_OUT/staging-doom"
    local doom_build="$TRANS_DIR/doom/build-tp"
    local freedoom_dir="$TRANS_DIR/doom/freedoom"
    rm -rf "$staging"
    mkdir -p "$staging/usr/games" \
             "$staging/usr/share/doom" \
             "$staging/usr/share/doc/doom"

    if [ -f "$doom_build/doom.flt" ]; then
        cp "$doom_build/doom.flt" "$staging/usr/games/doom"
        chmod 755 "$staging/usr/games/doom"
    else
        echo "  SKIP: doom.flt not found"
        rm -rf "$staging"
        return 0
    fi

    # Include Freedoom WAD
    if [ -f "$freedoom_dir/freedoom1.wad" ]; then
        cp "$freedoom_dir/freedoom1.wad" "$staging/usr/share/doom/freedoom1.wad"
    fi
    [ -f "$freedoom_dir/COPYING.txt" ] && \
        cp "$freedoom_dir/COPYING.txt" "$staging/usr/share/doc/doom/FREEDOOM-LICENSE.txt"

    cat > "$staging/usr/share/doc/doom/README" <<'EOF'
DOOM for TransputerOS
Run: doom (auto-finds /usr/share/doom/freedoom1.wad)
EOF

    # No hard Depends: fbwm — DOOM uses direct MMIO framebuffer (i_video_tp).
    make_tpz "$staging" "doom" "1.0.0" \
        "DOOM engine with Freedoom WAD (simple FB play: doom -simplefb)" \
        "GPL-2.0" ""
}

# -----------------------------------------------------------------------
# Package: doom-engine (binary only — for slim rootfs / initramfs)
# -----------------------------------------------------------------------
build_doom_engine() {
    echo "=== Building doom-engine.tpz (binary only) ==="
    local staging="$PKG_OUT/staging-doom-engine"
    local doom_build="$TRANS_DIR/doom/build-tp"
    rm -rf "$staging"
    mkdir -p "$staging/usr/games" "$staging/usr/share/doom" \
             "$staging/usr/share/doc/doom"

    if [ -f "$doom_build/doom.flt" ]; then
        cp "$doom_build/doom.flt" "$staging/usr/games/doom"
        chmod 755 "$staging/usr/games/doom"
    elif [ -f /tmp/doom.flt ]; then
        cp /tmp/doom.flt "$staging/usr/games/doom"
        chmod 755 "$staging/usr/games/doom"
    else
        echo "  SKIP: doom.flt not found"
        rm -rf "$staging"
        return 0
    fi

    cat > "$staging/usr/games/doom-simple" <<'EOF'
#!/bin/sh
export DOOMWADDIR=/usr/share/doom
export HOME=/
cd /usr/share/doom 2>/dev/null || true
exec /usr/games/doom -simplefb "$@"
EOF
    chmod 755 "$staging/usr/games/doom-simple"

    cat > "$staging/usr/share/doc/doom/README" <<'EOF'
DOOM engine for TransputerOS (simple framebuffer)

  doom-simple              # 2x scale, full-screen style
  doom -windowed           # window chrome mode
  doom -iwad /path/to.wad

Place freedoom1.wad in /usr/share/doom/ (install doom-1.0.0.tpz data
or copy from packages/output after extracting the full doom package).
EOF

    make_tpz "$staging" "doom-engine" "1.0.0" \
        "DOOM binary only (no WAD) — simple framebuffer play" \
        "GPL-2.0" ""
}

# -----------------------------------------------------------------------
# Package: coremark (spawnable TransputerOS bFLT)
# -----------------------------------------------------------------------
build_coremark() {
    echo "=== Building coremark.tpz ==="
    local staging="$PKG_OUT/staging-coremark"
    local coremark_bin="$TRANS_DIR/initramfs/coremark.flt"
    rm -rf "$staging"
    mkdir -p "$staging/usr/bin" "$staging/usr/share/doc/coremark"

    if [ ! -f "$coremark_bin" ]; then
        echo "  SKIP: coremark.flt not found — run transputer/coremark/transputer/build-flt.sh"
        rm -rf "$staging"
        return 0
    fi

    cp "$coremark_bin" "$staging/usr/bin/coremark"
    chmod 755 "$staging/usr/bin/coremark"
    cp "$TRANS_DIR/coremark/LICENSE.md" \
        "$staging/usr/share/doc/coremark/LICENSE.md"

    make_tpz "$staging" "coremark" "1.0.2" \
        "CoreMark CPU benchmark for the T800 transputer" \
        "Apache-2.0" ""
}

# -----------------------------------------------------------------------
# Main
# -----------------------------------------------------------------------

echo "TransputerOS Package Builder (.tpz)"
echo "===================================="

# -----------------------------------------------------------------------
# Package: tpkg (package manager CLI + GUI)
# -----------------------------------------------------------------------
build_tpkg() {
    echo "=== Building tpkg.tpz ==="
    local staging="$PKG_OUT/staging-tpkg"
    rm -rf "$staging"
    mkdir -p "$staging/usr/bin"

    if [ -f /tmp/tpkg.flt ]; then
        cp /tmp/tpkg.flt "$staging/usr/bin/tpkg"
        chmod 755 "$staging/usr/bin/tpkg"
    else
        echo "  SKIP: /tmp/tpkg.flt not found — run transputer/tpkg/build.sh"
        rm -rf "$staging"
        return 0
    fi

    if [ -f /tmp/tpkg_ui.flt ]; then
        cp /tmp/tpkg_ui.flt "$staging/usr/bin/tpkg-ui"
        chmod 755 "$staging/usr/bin/tpkg-ui"
    fi

    make_tpz "$staging" "tpkg" "1.2.0" \
        "TransputerOS package manager (CLI and GUI)" \
        "MIT" ""
}

# -----------------------------------------------------------------------
# Package: ttop (system monitor)
# -----------------------------------------------------------------------
build_ttop() {
    echo "=== Building ttop.tpz ==="
    local staging="$PKG_OUT/staging-ttop"
    rm -rf "$staging"
    mkdir -p "$staging/usr/bin"

    if [ -f /tmp/ttop.flt ]; then
        cp /tmp/ttop.flt "$staging/usr/bin/ttop"
        chmod 755 "$staging/usr/bin/ttop"
    else
        echo "  SKIP: /tmp/ttop.flt not found — run transputer/tpkg/build.sh"
        rm -rf "$staging"
        return 0
    fi

    make_tpz "$staging" "ttop" "1.0.0" \
        "T800 system monitor with CPU/MEM bars and process list" \
        "MIT" ""
}

# -----------------------------------------------------------------------
# Package: tman (task manager)
# Source of truth: transputer/tpkg/tman.c → build.sh → /tmp/tman.flt
# Ship as tman-*.tpz; stage/rootfs should install from this package.
# -----------------------------------------------------------------------
build_tman() {
    echo "=== Building tman.tpz ==="
    local staging="$PKG_OUT/staging-tman"
    local bin=""
    rm -rf "$staging"
    mkdir -p "$staging/usr/bin"

    # Prefer freshly built binary; fall back to staged package extract.
    if [ -f /tmp/tman.flt ]; then
        bin=/tmp/tman.flt
    elif [ -f "$TRANS_DIR/tpkg/tman.flt" ]; then
        bin="$TRANS_DIR/tpkg/tman.flt"
    fi

    if [ -z "$bin" ]; then
        echo "  Building tman from source (transputer/tpkg/build.sh)..."
        bash "$TRANS_DIR/tpkg/build.sh" || true
        if [ -f /tmp/tman.flt ]; then
            bin=/tmp/tman.flt
        fi
    fi

    if [ -z "$bin" ] || [ ! -f "$bin" ]; then
        echo "  SKIP: tman.flt not found — run: bash transputer/tpkg/build.sh"
        rm -rf "$staging"
        return 0
    fi

    cp "$bin" "$staging/usr/bin/tman"
    chmod 755 "$staging/usr/bin/tman"

    # 1.0.1: 500ms /proc refresh, thread column, getdents-based process list
    make_tpz "$staging" "tman" "1.0.1" \
        "Task manager: live /proc (~500ms), processes+threads, CPU/MEM graphs" \
        "MIT" ""
}

# -----------------------------------------------------------------------
# Package: tfiles (file manager)
# -----------------------------------------------------------------------
build_tfiles() {
    echo "=== Building tfiles.tpz ==="
    local staging="$PKG_OUT/staging-tfiles"
    rm -rf "$staging"
    mkdir -p "$staging/usr/bin"

    if [ -f /tmp/tfiles.flt ]; then
        cp /tmp/tfiles.flt "$staging/usr/bin/tfiles"
        chmod 755 "$staging/usr/bin/tfiles"
    else
        echo "  SKIP: /tmp/tfiles.flt not found — run transputer/tpkg/build.sh"
        rm -rf "$staging"
        return 0
    fi

    make_tpz "$staging" "tfiles" "1.0.0" \
        "TransputerOS file manager with directory browsing and file viewer" \
        "MIT" ""
}

case "${1:-all}" in
    tcc|tcc-toolchain) build_tcc_toolchain ;;
    fbwm)              build_fbwm ;;
    doom)              build_doom ;;
    doom-engine)       build_doom_engine ;;
    coremark)          build_coremark ;;
    tpkg)              build_tpkg ;;
    ttop)              build_ttop ;;
    tman)              build_tman ;;
    tfiles)            build_tfiles ;;
    all)
        build_tcc_toolchain
        build_fbwm
        build_doom
        build_doom_engine
        build_coremark
        build_tpkg
        build_ttop
        build_tman
        build_tfiles
        echo ""
        echo "All packages in: $PKG_OUT/"
        ls -lh "$PKG_OUT"/*.tpz 2>/dev/null || echo "(none built)"
        ;;
    *)
        echo "Usage: $0 [tcc|fbwm|doom|doom-engine|coremark|tpkg|ttop|tman|tfiles|all]"
        ;;
esac
