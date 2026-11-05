#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
RUNNER="$ROOT_DIR/transputer/tests/test_runner_with_uart.sh"

TMPDIR_LINKER="$(mktemp -d)"
trap 'rm -rf "$TMPDIR_LINKER"' EXIT

run_case() {
    local label="$1"
    shift
    echo "== $label =="
    "$@"
}

run_case "strict-relocs data32" \
    env T4_STRICT_RELOCS=1 \
    bash "$RUNNER" test_reloc_data32

run_case "strict-relocs iprel-fwd/bwd" \
    env T4_STRICT_RELOCS=1 \
    bash "$RUNNER" test_reloc_iprel_fwd test_reloc_iprel_bwd

run_case "strict-relocs duplocal" \
    env T4_STRICT_RELOCS=1 \
    bash "$RUNNER" test_reloc_duplocal

run_case "strict-iprel fwd/bwd" \
    env T4_STRICT_IPREL=1 \
    bash "$RUNNER" test_reloc_iprel_fwd test_reloc_iprel_bwd

run_case "strict-ldc-abs boot stub" \
    env T4_STRICT_LDC_ABS=1 \
    bash "$RUNNER" test_reloc_data32

echo "== strict unresolved-call audit =="
if env T4_STRICT_UNRESOLVED_CALLS=1 \
    "$ROOT_DIR/transputer-tcc" -static -nostdlib \
    -o "$TMPDIR_LINKER/unresolved.elf" \
    "$SCRIPT_DIR/test_boot.S" \
    "$SCRIPT_DIR/test_linker_unresolved_call.c" \
    >"$TMPDIR_LINKER/unresolved.stdout" \
    2>"$TMPDIR_LINKER/unresolved.stderr"; then
    echo "FAIL: final link accepted an unresolved direct call" >&2
    exit 1
fi
if ! grep -q "undefined symbol 'missing_link_target'" \
    "$TMPDIR_LINKER/unresolved.stderr"; then
    echo "FAIL: final link did not diagnose the unresolved call" >&2
    cat "$TMPDIR_LINKER/unresolved.stderr" >&2
    exit 1
fi

echo "== extern bound only by another file's static =="
if env T4_STRICT_UNRESOLVED_CALLS=1 \
    "$ROOT_DIR/transputer-tcc" -static -nostdlib \
    -o "$TMPDIR_LINKER/extern_static.elf" \
    "$SCRIPT_DIR/test_boot.S" \
    "$SCRIPT_DIR/test_linker_extern_static.c" \
    "$SCRIPT_DIR/test_reloc_extern_static_helper.c" \
    >"$TMPDIR_LINKER/extern_static.stdout" \
    2>"$TMPDIR_LINKER/extern_static.stderr"; then
    echo "FAIL: final link bound an extern to another file's static" >&2
    exit 1
fi
if ! grep -q "undefined symbol 'only_static'" \
    "$TMPDIR_LINKER/extern_static.stderr"; then
    echo "FAIL: final link did not diagnose the undefined extern" >&2
    cat "$TMPDIR_LINKER/extern_static.stderr" >&2
    exit 1
fi

echo "== malformed bootstrap LDC slots =="
for point in __return_label_addr __test_main_load_point; do
    cat > "$TMPDIR_LINKER/load.S" <<EOF
.text
.global _start, test_main, halt, $point
_start:
test_main:
halt:
    ret
$point:
    .long 0, 0
EOF
    if "$ROOT_DIR/transputer-tcc" -nostdlib -o "$TMPDIR_LINKER/load.elf" \
        "$TMPDIR_LINKER/load.S" 2>"$TMPDIR_LINKER/load.stderr"; then
        echo "FAIL: linker accepted malformed $point" >&2
        exit 1
    fi
    grep -F "$point at " "$TMPDIR_LINKER/load.stderr" | grep -F 'eight-byte LDC'
done

echo "All strict linker mode tests passed."
