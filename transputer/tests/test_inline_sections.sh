#!/usr/bin/env bash
set -euo pipefail

INLINE_TEST_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
INLINE_TEST_ROOT="$(cd "$INLINE_TEST_DIR/../.." && pwd)"
INLINE_TEST_TMP="$(mktemp -d /tmp/tcc-inline-sections.XXXXXX)"
INLINE_TEST_CCS="${INLINE_TEST_CCS:-$INLINE_TEST_ROOT/transputer-tcc $INLINE_TEST_ROOT/tcc}"

cleanup_inline_test() {
    rm -rf "$INLINE_TEST_TMP"
}
trap cleanup_inline_test EXIT

assert_symbol_section() {
    local object="$1"
    local symbol="$2"
    local expected="$3"
    local actual

    actual="$(objdump -t "$object" | \
        awk -v wanted="$symbol" '$NF == wanted { print $(NF - 2); exit }')"
    if [[ "$actual" != "$expected" ]]; then
        echo "FAIL: $symbol is in ${actual:-<missing>}, expected $expected" >&2
        exit 1
    fi
    echo "ok: $symbol -> $expected"
}

for compiler in $INLINE_TEST_CCS; do
    compiler_name="$(basename "$compiler")"
    object="$INLINE_TEST_TMP/$compiler_name.o"
    reverse_object="$INLINE_TEST_TMP/$compiler_name-reverse.o"

    TCC_TRANSPUTER_INLINE=0 "$compiler" -nostdlib -nostdinc -c -o "$object" \
        "$INLINE_TEST_DIR/test_inline_sections.c"
    TCC_TRANSPUTER_INLINE=0 "$compiler" -nostdlib -nostdinc -c -o "$reverse_object" \
        "$INLINE_TEST_DIR/test_inline_sections_reverse.c"

    echo "compiler: $compiler_name"
    assert_symbol_section "$object" inferred_inline .init.text
    assert_symbol_section "$object" inferred_plain .init.text
    assert_symbol_section "$object" nested_inner .init.text
    assert_symbol_section "$object" nested_outer .init.text
    assert_symbol_section "$object" explicit_inline .early.text
    assert_symbol_section "$object" conflicted_inline .text
    assert_symbol_section "$object" mixed_init_path .init.text
    assert_symbol_section "$object" mixed_live_path .live.text
    assert_symbol_section "$object" mixed_shared .text
    assert_symbol_section "$object" init_address_path .init.text
    assert_symbol_section "$object" address_target .text
    if objdump -t "$object" | awk '$NF == "unused_inline_path" { found = 1 } END { exit !found }'; then
        echo "FAIL: unused_inline_path was emitted" >&2
        exit 1
    fi
    assert_symbol_section "$reverse_object" reverse_init_path .init.text
    assert_symbol_section "$reverse_object" reverse_live_path .live.text
    assert_symbol_section "$reverse_object" reverse_shared .text
done
