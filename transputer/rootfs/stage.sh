#!/bin/bash
# stage.sh — Build the TransputerOS root filesystem
#
# Creates the TransputerOS rootfs from our own components.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TRANS_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
ROOT_DIR="$(cd "$TRANS_DIR/.." && pwd)"
STAGE_DIR="${1:-$SCRIPT_DIR/output/stage}"

# Required binaries
INIT_BIN="$TRANS_DIR/initramfs/init"
BUSYBOX_BIN="$TRANS_DIR/initramfs/busybox.flt"
TPSH_BIN="$TRANS_DIR/initramfs/tpsh.flt"
FBWM_BIN="${FBWM_BIN:-$TRANS_DIR/initramfs/fbwm.flt}"
if [ ! -f "$FBWM_BIN" ] && [ -f /tmp/fbwm.flt ]; then
    FBWM_BIN=/tmp/fbwm.flt
fi

echo "TransputerOS rootfs staging → $STAGE_DIR"

# Verify required binaries
for f in "$INIT_BIN" "$BUSYBOX_BIN" "$TPSH_BIN"; do
    if [ ! -f "$f" ]; then
        echo "Missing: $f" >&2
        echo "Build with: bash transputer/build.sh --initramfs --busybox --tpsh" >&2
        exit 1
    fi
done

# Clean and create directory structure
rm -rf "$STAGE_DIR"
mkdir -p \
    "$STAGE_DIR/bin" \
    "$STAGE_DIR/sbin" \
    "$STAGE_DIR/dev" \
    "$STAGE_DIR/etc" \
    "$STAGE_DIR/tmp" \
    "$STAGE_DIR/opt" \
    "$STAGE_DIR/usr/bin" \
    "$STAGE_DIR/usr/games"

# --- Boot-critical binaries (bFLT) ---
cp "$INIT_BIN" "$STAGE_DIR/init"
cp "$BUSYBOX_BIN" "$STAGE_DIR/bin/busybox"
cp "$TPSH_BIN" "$STAGE_DIR/bin/tpsh"
chmod 0755 "$STAGE_DIR/init" "$STAGE_DIR/bin/busybox" "$STAGE_DIR/bin/tpsh"

# fbwm (optional)
if [ -f "$FBWM_BIN" ]; then
    cp "$FBWM_BIN" "$STAGE_DIR/usr/bin/fbwm"
    chmod 0755 "$STAGE_DIR/usr/bin/fbwm"
    echo "  fbwm: $(du -h "$STAGE_DIR/usr/bin/fbwm" | cut -f1)"
fi

# coremark (optional)
COREMARK_BIN="$TRANS_DIR/initramfs/coremark.flt"
if [ -f "$COREMARK_BIN" ]; then
    cp "$COREMARK_BIN" "$STAGE_DIR/usr/bin/coremark"
    chmod 0755 "$STAGE_DIR/usr/bin/coremark"
    echo "  coremark: $(du -h "$STAGE_DIR/usr/bin/coremark" | cut -f1)"
fi

# DOOM engine binary (optional — ~1MB bFLT). WAD is NOT in initramfs
# (freedoom1.wad is ~28MB). Install data via: tpkg install doom-1.0.0.tpz
# or place freedoom1.wad at /usr/share/doom/ on SD.
DOOM_BIN="${DOOM_BIN:-$TRANS_DIR/doom/build-tp/doom.flt}"
if [ -f "$DOOM_BIN" ]; then
    mkdir -p "$STAGE_DIR/usr/games" "$STAGE_DIR/usr/share/doom"
    cp "$DOOM_BIN" "$STAGE_DIR/usr/games/doom"
    chmod 0755 "$STAGE_DIR/usr/games/doom"
    # launcher: simple framebuffer mode by default
    cat > "$STAGE_DIR/usr/games/doom-simple" << 'EOF'
#!/bin/sh
# Simple framebuffer DOOM (2x scale, no window chrome)
# Place freedoom1.wad in /usr/share/doom/ (tpkg install doom, or copy)
export DOOMWADDIR=/usr/share/doom
export HOME=/
cd /usr/share/doom 2>/dev/null || true
exec /usr/games/doom -simplefb "$@"
EOF
    chmod 0755 "$STAGE_DIR/usr/games/doom-simple"
    # Optional: include WAD only if TPOS_INCLUDE_WAD=1 (huge rootfs)
    if [ "${TPOS_INCLUDE_WAD:-0}" = "1" ] && \
       [ -f "$TRANS_DIR/doom/freedoom/freedoom1.wad" ]; then
        cp "$TRANS_DIR/doom/freedoom/freedoom1.wad" \
           "$STAGE_DIR/usr/share/doom/freedoom1.wad"
        echo "  doom WAD: $(du -h "$STAGE_DIR/usr/share/doom/freedoom1.wad" | cut -f1)"
    else
        echo "  doom: engine only (set TPOS_INCLUDE_WAD=1 to embed freedoom1.wad)"
    fi
    echo "  doom: $(du -h "$STAGE_DIR/usr/games/doom" | cut -f1)"
fi

# tpkg CLI (optional)
TPKG_BIN="${TPKG_BIN:-/tmp/tpkg.flt}"
if [ -f "$TPKG_BIN" ]; then
    cp "$TPKG_BIN" "$STAGE_DIR/usr/bin/tpkg"
    chmod 0755 "$STAGE_DIR/usr/bin/tpkg"
    echo "  tpkg: $(du -h "$STAGE_DIR/usr/bin/tpkg" | cut -f1)"
fi

# tman task manager — TPZ package (source: transputer/tpkg/tman.c).
# Prefer installing from packages/output/tman-*.tpz so rootfs matches
# on-target tpkg installs; fall back to TMAN_BIN /tmp/tman.flt.
PKG_DIR="${PKG_DIR:-$TRANS_DIR/packages/output}"
TMAN_TPZ=$(ls -1 "$PKG_DIR"/tman-*.tpz 2>/dev/null | sort -V | tail -1 || true)
if [ -n "$TMAN_TPZ" ] && [ -f "$TMAN_TPZ" ]; then
    tar xzf "$TMAN_TPZ" -C "$STAGE_DIR" ./usr/bin/tman 2>/dev/null \
        || tar xzf "$TMAN_TPZ" -C "$STAGE_DIR" usr/bin/tman 2>/dev/null \
        || true
    if [ -f "$STAGE_DIR/usr/bin/tman" ]; then
        chmod 0755 "$STAGE_DIR/usr/bin/tman"
        echo "  tman: $(du -h "$STAGE_DIR/usr/bin/tman" | cut -f1) (from $(basename "$TMAN_TPZ"))"
    fi
fi
if [ ! -f "$STAGE_DIR/usr/bin/tman" ]; then
    TMAN_BIN="${TMAN_BIN:-/tmp/tman.flt}"
    if [ -f "$TMAN_BIN" ]; then
        cp "$TMAN_BIN" "$STAGE_DIR/usr/bin/tman"
        chmod 0755 "$STAGE_DIR/usr/bin/tman"
        echo "  tman: $(du -h "$STAGE_DIR/usr/bin/tman" | cut -f1) (from $TMAN_BIN)"
    fi
fi

# --- BusyBox applet symlinks ---
ln -sf /bin/busybox "$STAGE_DIR/bin/sh"
for applet in \
    cat cp cut date df dirname dmesg echo env expr false find free grep \
    gunzip gzip head hostname id kill ln ls mkdir mount mv pidof ping \
    printf ps pwd rm sed sleep sort stat sync tail tar test touch true \
    umount uname uptime wc wget which; do
    ln -sf /bin/busybox "$STAGE_DIR/bin/$applet"
done

for applet in ifconfig route; do
    ln -sf /bin/busybox "$STAGE_DIR/sbin/$applet"
done

# --- Configuration files ---
cat > "$STAGE_DIR/etc/os-release" << 'EOF'
NAME="TransputerOS"
VERSION="0.1.0"
ID=transputeros
PRETTY_NAME="TransputerOS 0.1.0 (INMOS T800)"
HOME_URL="https://github.com/agentdavo/tinycc-transputer"
EOF

cat > "$STAGE_DIR/etc/hostname" << 'EOF'
transputer
EOF

cat > "$STAGE_DIR/etc/hosts" << 'EOF'
127.0.0.1	localhost transputer
EOF

cat > "$STAGE_DIR/etc/fstab" << 'EOF'
proc	/proc	proc	defaults	0 0
sysfs	/sys	sysfs	defaults	0 0
tmpfs	/tmp	tmpfs	defaults	0 0
EOF

cat > "$STAGE_DIR/etc/profile" << 'EOF'
export PATH=/bin:/sbin:/usr/bin:/usr/sbin:/usr/games
export HOME=/
export TERM=vt100
export PS1='tc@transputer:\w\$ '
EOF

cat > "$STAGE_DIR/etc/services" << 'EOF'
echo	7/tcp
echo	7/udp
http	80/tcp
ssh	22/tcp
EOF

# --- Optional .tpz packages for on-target tpkg install ---
# Layout matches TPZ-FORMAT.md: install with
#   tar xzf /opt/packages/NAME.tpz -C /
# or tpkg install /opt/packages/NAME.tpz
PKG_DIR="${PKG_DIR:-$TRANS_DIR/packages/output}"
mkdir -p "$STAGE_DIR/opt/packages" "$STAGE_DIR/var/lib/tpkg"
if [ -d "$PKG_DIR" ]; then
    # Prefer small packages only (skip huge doom full WAD in initramfs).
    # tman is a first-class TPZ app (System icon / live task manager).
    for pkg in fbwm-1.0.0.tpz doom-engine-1.0.0.tpz tpkg-1.2.0.tpz \
               ttop-1.0.0.tpz tfiles-1.0.0.tpz coremark-1.0.2.tpz; do
        if [ -f "$PKG_DIR/$pkg" ]; then
            # Skip if package larger than ~1.5MB (initramfs budget)
            psz=$(stat -c%s "$PKG_DIR/$pkg" 2>/dev/null || echo 99999999)
            if [ "$psz" -lt 1500000 ]; then
                cp "$PKG_DIR/$pkg" "$STAGE_DIR/opt/packages/"
                echo "  package: $pkg ($(du -h "$PKG_DIR/$pkg" | cut -f1))"
            else
                echo "  skip large package: $pkg ($(du -h "$PKG_DIR/$pkg" | cut -f1))"
            fi
        fi
    done
    # Latest tman TPZ only (app is package-managed)
    tman_pkg=$(ls -1 "$PKG_DIR"/tman-*.tpz 2>/dev/null | sort -V | tail -1 || true)
    if [ -n "$tman_pkg" ] && [ -f "$tman_pkg" ]; then
        cp "$tman_pkg" "$STAGE_DIR/opt/packages/"
        echo "  package: $(basename "$tman_pkg") ($(du -h "$tman_pkg" | cut -f1))"
    fi
fi

# --- Permissions ---
chmod 1777 "$STAGE_DIR/tmp"

# --- Summary ---
echo "TransputerOS rootfs staged:"
echo "  $(find "$STAGE_DIR" -type f | wc -l) files, $(find "$STAGE_DIR" -type l | wc -l) symlinks"
echo "  $(du -sh "$STAGE_DIR" | cut -f1) total"
