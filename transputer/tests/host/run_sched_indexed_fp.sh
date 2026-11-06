#!/usr/bin/env bash
set -eu
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
"${CC:-cc}" -O2 -Wno-unused-result -DTCC_TARGET_TRANSPUTER -I"$root" \
    "$root/transputer/tests/host/test_sched_indexed_fp.c" \
    -o "$work/test" -lm -ldl -lpthread
"$work/test"
