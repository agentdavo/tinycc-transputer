#!/bin/sh
# Exact host-side geometry and canonical-frame regression gate.
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT_DIR=$(CDPATH= cd -- "$SCRIPT_DIR/../.." && pwd)
TEST_BIN=$(mktemp /tmp/fbwm-host-test.XXXXXX)
FRAME=$(mktemp /tmp/fbwm-canonical.XXXXXX.ppm)
EXPECTED=17a5ccefc0dc76e6fc2ebc3240a0cdb0feb45784c0d3ad5cb587fe395b01e976

trap 'rm -f "$TEST_BIN" "$FRAME"' EXIT HUP INT TERM

cc -std=c99 -D_GNU_SOURCE -DFBWM_HOST -Wall -Wextra \
  -Wno-int-to-pointer-cast -Wno-unused-function \
  -I"$SCRIPT_DIR" \
  "$SCRIPT_DIR/test_fbwm_host.c" "$SCRIPT_DIR/fbwm.c" \
  "$SCRIPT_DIR/fbwm_shm.c" \
  -o "$TEST_BIN"

FBWM_GOLDEN_OUT="$FRAME" "$TEST_BIN"
ACTUAL=$(sha256sum "$FRAME" | cut -d ' ' -f 1)
if [ "$ACTUAL" != "$EXPECTED" ]; then
    echo "FBWM canonical frame mismatch" >&2
    echo "expected: $EXPECTED" >&2
    echo "actual:   $ACTUAL" >&2
    echo "frame:    $FRAME" >&2
    trap - EXIT HUP INT TERM
    exit 1
fi

echo "fbwm canonical frame sha256: PASS ($ACTUAL)"
