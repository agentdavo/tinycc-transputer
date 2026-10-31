#!/bin/bash
# Build a partitioned T9000 SD/XiP image with a persistent ext2 rootfs.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
KERNEL="${1:-$SCRIPT_DIR/linux-7/vmlinux}"
OUTPUT="${2:-$ROOT_DIR/tpos.sd}"
STAGE="${TPOS_ROOTFS_STAGE:-$SCRIPT_DIR/rootfs/output/stage}"
ROOTFS_IMAGE="${TPOS_ROOTFS_IMAGE:-$ROOT_DIR/tpos-rootfs.ext2}"
ROOTFS_MIB="${TPOS_ROOTFS_MIB:-64}"

if [ ! -f "$KERNEL" ]; then
    echo "Missing kernel: $KERNEL" >&2
    exit 1
fi
if [ ! -d "$STAGE" ]; then
    echo "Missing staged rootfs: $STAGE" >&2
    echo "Run: bash transputer/rootfs/stage.sh" >&2
    exit 1
fi

if [ ! -x "$ROOT_DIR/transputer-mksd" ] ||
   [ "$ROOT_DIR/transputer-mksd.c" -nt "$ROOT_DIR/transputer-mksd" ]; then
    cc -O2 -Wall -Wextra -o "$ROOT_DIR/transputer-mksd" \
       "$ROOT_DIR/transputer-mksd.c"
fi

# A full-size filesystem image makes replacement deterministic and ensures
# stale blocks from an older card image cannot survive outside ext2 metadata.
truncate -s "${ROOTFS_MIB}M" "$ROOTFS_IMAGE"
mke2fs -q -F -t ext2 -b 1024 -I 256 -L TPOS_ROOT -d "$STAGE" \
       "$ROOTFS_IMAGE"

"$ROOT_DIR/transputer-mksd" --rootfs "$ROOTFS_IMAGE" "$KERNEL" "$OUTPUT"
echo "SD image ready: $OUTPUT"
echo "Boot with:"
echo "  T4_SD_IMAGE=$OUTPUT T4_SD_BOOT=1 $SCRIPT_DIR/t4/t4 -s806 -sm 27 -ss"
