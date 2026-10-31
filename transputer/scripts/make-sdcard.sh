#!/bin/bash
# make-sdcard.sh — Create and populate the TransputerOS SD card image
#
# Generates a 128MB ext2 image with all packages pre-installed.
# The image is used by the T4 emulator via T4_SD_IMAGE=sdcard.img
#
# Usage: bash transputer/scripts/make-sdcard.sh [output-path]
#
# Prerequisites: mkfs.ext2, debugfs (e2fsprogs package)
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TRANS_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
ROOT_DIR="$(cd "$TRANS_DIR/.." && pwd)"

SD_IMG="${1:-$ROOT_DIR/sdcard.img}"
SD_SIZE_MB=128

echo "=== Creating TransputerOS SD Card Image ==="
echo "Output: $SD_IMG"
echo "Size:   ${SD_SIZE_MB}MB ext2"

# Create empty image
dd if=/dev/zero of="$SD_IMG" bs=1M count=$SD_SIZE_MB status=none
mkfs.ext2 -F -q -L "TransputerSD" "$SD_IMG"

# Create directory structure
for dir in usr usr/bin usr/games usr/share usr/share/doom usr/share/doc \
           var var/lib var/lib/tpkg home home/tc tmp; do
    debugfs -w -R "mkdir $dir" "$SD_IMG" 2>/dev/null || true
done

# Install binaries (if they exist)
install_file() {
    local src="$1" dst="$2"
    if [ -f "$src" ]; then
        debugfs -w -R "write $src $dst" "$SD_IMG" 2>/dev/null
        echo "  + /$dst ($(du -sh "$src" | cut -f1))"
    else
        echo "  - /$dst (not found: $src)"
    fi
}

echo ""
echo "Installing packages..."
install_file "/tmp/tpkg.flt"     "usr/bin/tpkg"
install_file "/tmp/tpkg_ui.flt"  "usr/bin/tpkg-ui"
install_file "/tmp/ttop.flt"     "usr/bin/ttop"
install_file "/tmp/fbwm.flt"     "usr/bin/fbwm"

install_file "$TRANS_DIR/doom/build-tp/doom.flt" "usr/games/doom"
install_file "$TRANS_DIR/doom/freedoom/freedoom1.wad" "usr/share/doom/freedoom1.wad"

# Create package database entries
for pkg in tpkg doom fbwm; do
    debugfs -w -R "mkdir var/lib/tpkg/$pkg" "$SD_IMG" 2>/dev/null || true
done

# Write tpkg info files via temp files
tmpinfo=$(mktemp)

cat > "$tmpinfo" <<'EOF'
Package: tpkg
Version: 1.1.0
Description: TransputerOS package manager and system tools
Size: 548896
Arch: transputer
Depends:
License: MIT
Files: 3
EOF
debugfs -w -R "write $tmpinfo var/lib/tpkg/tpkg/info" "$SD_IMG" 2>/dev/null

cat > "$tmpinfo" <<'EOF'
Package: doom
Version: 1.0.0
Description: DOOM engine with Freedoom WAD data
Size: 29793607
Arch: transputer
Depends: fbwm
License: GPL-2.0
Files: 2
EOF
debugfs -w -R "write $tmpinfo var/lib/tpkg/doom/info" "$SD_IMG" 2>/dev/null

cat > "$tmpinfo" <<'EOF'
Package: fbwm
Version: 1.0.0
Description: Transputer framebuffer window manager
Size: 173372
Arch: transputer
Depends:
License: MIT
Files: 1
EOF
debugfs -w -R "write $tmpinfo var/lib/tpkg/fbwm/info" "$SD_IMG" 2>/dev/null

rm -f "$tmpinfo"

echo ""
echo "=== SD Card Contents ==="
debugfs -R "ls -l usr/bin" "$SD_IMG" 2>/dev/null
debugfs -R "ls -l usr/games" "$SD_IMG" 2>/dev/null
debugfs -R "ls -l usr/share/doom" "$SD_IMG" 2>/dev/null
echo ""
echo "SD card image ready: $SD_IMG ($(du -sh "$SD_IMG" | cut -f1))"
echo ""
echo "To boot: T4_SD_IMAGE=$SD_IMG bash transputer/build.sh --boot"
