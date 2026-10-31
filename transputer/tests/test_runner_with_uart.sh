#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
TEST_DIR="$ROOT_DIR/transputer/tests"
BOOT_STUB="$TEST_DIR/test_boot.S"
TCC_BIN="$ROOT_DIR/transputer-tcc"
NETLOAD_BIN="$ROOT_DIR/transputer-netload"
T4_BIN="${T4_BIN:-$ROOT_DIR/transputer/t4/t4}"
T4_MEM_SHIFT="${T4_MEM_SHIFT:-26}"
# The processor t4 models (-s425 / -s414 stop on an instruction that part lacks)
T4_CPU="${T4_CPU:--s806}"
T4_TIMEOUT="${T4_TIMEOUT:-8s}"
RESULTS_MD="$ROOT_DIR/transputer/tests/test_results.md"
RUNNER_TMPDIR=""
UCLIBC_DIR="$ROOT_DIR/transputer/uClibc-ng"
ROOT_INCLUDE_DIR="$ROOT_DIR/include"
# Another target: extra compile flags and link inputs, e.g. for the T425
#   TP_EXTRA_CFLAGS=-mt425
TP_EXTRA_CFLAGS="${TP_EXTRA_CFLAGS:-}"
# The part those flags select (the last -mt option wins, as in tcc).  A
# link for a part refuses objects compiled for a bigger one, so the helper
# library linked by default is the one built for it.
TP_TARGET="$(printf '%s\n' $TP_EXTRA_CFLAGS | grep '^-mt' | tail -n 1 || true)"
case "$TP_TARGET" in
-mt800|-mt805) TP_RUNTIME=transputer-libtcc1-t800.a ;;
-mt425|-mt414) TP_RUNTIME=transputer-libtcc1-${TP_TARGET#-m}.a ;;
*)             TP_RUNTIME=transputer-libtcc1.a ;;
esac
TP_EXTRA_LIBS="${TP_EXTRA_LIBS:-$ROOT_DIR/$TP_RUNTIME}"
# A test's own `Target: -mXXX` comment (TVX tests: -mt9000tvx) is added to
# these when they select no part; with another part selected it is skipped.
TEST_CFLAGS="$TP_EXTRA_CFLAGS"

# Associative array for collecting cycle counts
declare -A TEST_CYCLES=()
declare -a TEST_ORDER=()

usage() {
    cat <<'EOF'
Usage:
  bash transputer/tests/test_runner_with_uart.sh [--list] [--all] [test_name ...]

Behavior:
  - Discovers runnable tests under transputer/tests
  - Uses transputer/tests/test_boot.S for files exposing test_main()
  - Auto-wraps files exposing main() so they can run under test_boot.S
  - Handles known multi-file tests such as xcall/xfile/wsubdb semantics
  - Runs on transputer/t4/t4 via transputer-netload and captures UART output

Environment:
  T4_MEM_SHIFT   Emulator memory shift (default: 26)
  T4_TIMEOUT     Per-test timeout, passed to timeout(1) (default: 8s)
EOF
}

require_tools() {
    [[ -x "$TCC_BIN" ]] || { echo "missing $TCC_BIN"; exit 1; }
    [[ -x "$NETLOAD_BIN" ]] || { echo "missing $NETLOAD_BIN"; exit 1; }
    [[ -x "$T4_BIN" ]] || { echo "missing $T4_BIN"; exit 1; }
    [[ -f "$BOOT_STUB" ]] || { echo "missing $BOOT_STUB"; exit 1; }
}

declare -A EXTRA_SOURCES=(
    [main]="support.c"
    [simple_main]="support.c"
    [test_main]="test_functions.c"
    [test_wsubdb_semantics]="test_wsubdb_semantics_fn.S"
    [test_t9000_ops]="test_t9000_ops_fn.S"
    [test_regtest_ops]="test_regtest_ops_fn.S"
    [test_xcall_main]="test_xcall_helper1.c test_xcall_helper2.c"
    [test_xfile_a]="test_xfile_b.c"
    [test_longlong]="lib64_runtime.c"
    [test_llong_call_arg_pair]="lib64_runtime.c"
    [test_conc_alt]="../../lib/transputer-conc.c"
    [test_return_timeslice]="../../lib/transputer-conc.c"
    [test_vla]="../../lib/transputer-vla.c ../../lib/transputer-conc.c"
    [test_xlink]="../../lib/transputer-conc.c"
    [test_reloc_iprel_fwd]="test_reloc_iprel_fwd_helper.c"
    [test_reloc_iprel_bwd]="test_reloc_iprel_bwd_helper.c"
    [test_reloc_duplocal]="test_reloc_duplocal_helper.c"
    [test_reloc_cross_multi]="test_reloc_cross_multi_helper.c"
    [test_reloc_extern_static]="test_reloc_extern_static_helper.c"
    [test_near_call]="test_near_call_helper.c"
    [test_link_relax]="test_link_relax_fn.S test_link_relax_helper.c"
    [test_tinycore_abi_mixed]="test_tinycore_abi_mixed_helper.S"
    [test_tinycore_abi_syscall_stub]="syscall_stub.c"
    [test_tinycore_abi_asm_syscall]="test_tinycore_abi_asm_syscall_helper.S syscall_stub.c"
)

# Per-test compile flags.  The GGUF decoders are t4's host reference
# headers; built for the target they get the libc-free include set software
# TVX is built with.
declare -A EXTRA_CFLAGS=(
    [test_gguf_decode_parts]="-nostdinc -I$ROOT_DIR/lib/transputer-tvx/include"
)

declare -A FORCE_SKIP=(
    [minimal]="uses its own _start and writes memory, not UART-test oriented"
    [minimal_fp_test]="uses its own _start and debug memory only"
    [simple]="empty host-style stub"
    [support]="helper file for main/simple_main"
    [test]="host-style stub"
    [test_boot]="boot stub, not a standalone test"
    [test_debug_objects]="library-style helper only"
    [test_functions]="helper file for test_main"
    [test_instructions]="uses debug memory breadcrumbs rather than UART"
    [test_kernel_headers]="library-style helper only"
    [lib64_runtime]="helper file for test_longlong"
    [test_linker_symbols]="requires synthetic linker symbols"
    [test_linker_unresolved_call]="negative final-link regression"
    [test_linker_extern_static]="negative final-link regression"
    [test_logical_simple]="library-style helper only"
    [test_minimal_static]="library-style helper only"
    [test_program]="own _start with no UART output"
    [test_trace_helper]="manual inspection trace, no fixed oracle"
    [test_fpu_temps]="needs the T9 FPU RTL's Temps pool, which t4 does not model"
    [test_xcall_helper1]="helper file for test_xcall_main"
    [test_xcall_helper2]="helper file for test_xcall_main"
    [test_xfile_b]="helper file for test_xfile_a"
    [test_reloc_iprel_fwd_helper]="helper file for test_reloc_iprel_fwd"
    [test_reloc_iprel_bwd_helper]="helper file for test_reloc_iprel_bwd"
    [test_reloc_duplocal_helper]="helper file for test_reloc_duplocal"
    [test_reloc_cross_multi_helper]="helper file for test_reloc_cross_multi"
    [test_reloc_extern_static_helper]="helper file for test_reloc_extern_static"
    [test_near_call_helper]="helper file for test_near_call"
    [test_link_relax_helper]="helper file for test_link_relax"
    [transputer-fpu]="binary blob source, not standalone"
    [tvx_conformance]="host program: cc -O2 tvx_conformance.c -lm"
    [tvx_genvec]="host program: generates test_tvx.c's vectors"
    [tvx_genvec_vec]="host program: generates test_tvx_vec.c's vectors"
)

declare -A EXPECTED_OUTPUT=(
    [test_arrinit]="SSBCADONE\nH"
    [test_arrinit2]="SSBCADONE\nH"
    [test_deepcall]="SSa1b2c3d4e5f6DONE\nH"
    [test_initcall]="SSCXDONE\nH"
    [test_initcall_levels]="SSIE01AB67DONE\nH"
    [test_initramfs_bounds]="SIRFOK\nH"
    [test_looparr]="SSL0123DONE\nH"
    [test_loopcall]="SSL0123DONE\nH"
    [test_manycall]="SS123456789ABCXDONE\nH"
    [test_strlen]="STL5!OK\nH"
    [test_reloc_data32]="SD32OK\nH"
    [test_reloc_iprel_fwd]="SFWDOK\nH"
    [test_reloc_iprel_bwd]="SBWDOK\nH"
    [test_reloc_duplocal]="SDUPOK\nH"
    [test_reloc_cross_multi]="SXMOK\nH"
    [test_conc_alt]="S\nALL-PASS\nH"
    [test_return_timeslice]="Sbad=00000000\nRET-OK\nH"
    [test_regtest_ops]="Spop:P ldsts:P hchan:P \nALL-PASS\nH"
    [test_reloc_static_data]="SSDOK\nH"
    [test_dowhile0_loops]="SSSIAJNZOPEDONE\nH"
)

list_all_candidates() {
    find "$TEST_DIR" -maxdepth 1 -type f \( -name '*.c' -o -name '*.S' \) | sort
}

test_name_from_path() {
    basename "$1" | sed 's/\.[^.]*$//'
}

has_symbol() {
    local pattern="$1"
    local file="$2"
    grep -qE "$pattern" "$file"
}

has_entrypoint() {
    local file="$1"
    if has_symbol '(^|[[:space:]\*])(void|int)[[:space:]]+test_main[[:space:]]*\(' "$file"; then
        echo "test_main"
        return 0
    fi
    if has_symbol '(^|[[:space:]\*])(void|int)[[:space:]]+_start[[:space:]]*\(' "$file"; then
        echo "_start"
        return 0
    fi
    if has_symbol '(^|[[:space:]\*])(void|int)[[:space:]]+main[[:space:]]*\(' "$file"; then
        echo "main"
        return 0
    fi
    return 1
}

# The -m option a test needs, from a `Target: -mXXX` line in its first 24.
target_from_comment() {
    sed -n '1,24p' "$1" | sed -n 's/.*Target:[[:space:]]*\(-m[a-z0-9]*\).*/\1/p' | head -n1 || true
}

# A `Needs: fpu` line: any part with an FPU (not -mt425/-mt414).
# `Needs: t805ops`: the T805/T425 additions such as lddevid (not -mt800/-mt414).
# `Needs: softfp`: the soft-float runtime, so only -mt425/-mt414.
needs_from_comment() {
    sed -n '1,24p' "$1" | sed -n 's/.*Needs:[[:space:]]*\([a-z0-9]*\).*/\1/p' | head -n1 || true
}

expected_from_comment() {
    local file="$1"
    local line
    # A one-line comment (/* Expected: ... */) must not drag its */ along.
    line="$(sed -n '1,24p' "$file" | sed -n 's/.*Expected:[[:space:]]*//p' | head -n1 |
        sed 's/[[:space:]]*\*\/[[:space:]]*$//')"
    if [[ -n "$line" ]]; then
        printf '%b' "$line"
        return 0
    fi
    return 1
}

expected_for_test() {
    local name="$1"
    local file="$2"
    if [[ -n "${EXPECTED_OUTPUT[$name]:-}" ]]; then
        printf '%b' "${EXPECTED_OUTPUT[$name]}"
        return 0
    fi
    expected_from_comment "$file" && return 0
    return 1
}

resolve_sources() {
    local name="$1"
    local file="$2"
    local entry="$3"
    local wrapper="$4"
    local -a srcs=()
    local extra

    if [[ "$entry" != "_start" ]]; then
        srcs+=("$BOOT_STUB")
    fi
    if [[ -n "$wrapper" ]]; then
        srcs+=("$wrapper")
    fi
    srcs+=("$file")

    extra="${EXTRA_SOURCES[$name]:-}"
    if [[ -n "$extra" ]]; then
        local rel
        for rel in $extra; do
            srcs+=("$TEST_DIR/$rel")
        done
    fi

    printf '%s\n' "${srcs[@]}"
}

make_main_wrapper() {
    local tmpdir="$1"
    local name="$2"
    local wrapper="$tmpdir/${name}_main_wrapper.c"
    cat >"$wrapper" <<'EOF'
extern int main(void);
void test_main(void) { (void)main(); }
EOF
    printf '%s\n' "$wrapper"
}

compile_test() {
    local name="$1"
    local elf="$2"
    shift
    shift

    if [[ "$name" == "test_uclibc_malloc" ]]; then
        "$TCC_BIN" -static -nostdlib -nostdinc \
            -I"$ROOT_INCLUDE_DIR/transputer" -I"$ROOT_INCLUDE_DIR" -I"$UCLIBC_DIR/include" \
            -o "$elf" "$@" \
            "$TEST_DIR/syscall_stub.c" "$UCLIBC_DIR/lib/libc.a" \
            >/dev/null 2>&1
        return
    fi

    "$TCC_BIN" $TEST_CFLAGS -static -nostdlib -I"$ROOT_INCLUDE_DIR/transputer" -I"$ROOT_INCLUDE_DIR" \
        -o "$elf" "$@" $TP_EXTRA_LIBS >/dev/null 2>&1
}

run_uart_test() {
    local btl="$1"
    local uart="$2"
    local profile_dir="$3"
    # Run with -su to enable instruction profiling; profile written to cwd.
    # -k: t4 exits on SIGTERM only between instructions, so one that never
    # finishes would otherwise outlive the timeout.
    ( cd "$profile_dir" && \
      env -u DISPLAY T4_UART_CONSOLE=0 T4_UART_LOG="$uart" \
        timeout -k 5 "$T4_TIMEOUT" "$T4_BIN" $T4_CPU -sm "$T4_MEM_SHIFT" -su -ss -sb "$btl" \
        >/dev/null 2>&1 ) || true
}

# Extract instruction count from T4 profile file
get_instruction_count() {
    local profile_dir="$1"
    local pf="$profile_dir/profile"
    if [[ -f "$pf" ]]; then
        sed -n 's/^Instructions[[:space:]]*\([0-9]*\)/\1/p' "$pf"
    fi
}

print_list() {
    local file name reason entry
    while IFS= read -r file; do
        name="$(test_name_from_path "$file")"
        if [[ -n "${FORCE_SKIP[$name]:-}" ]]; then
            printf "SKIP  %s - %s\n" "$name" "${FORCE_SKIP[$name]}"
            continue
        fi
        if entry="$(has_entrypoint "$file")"; then
            printf "RUN   %s - entry=%s" "$name" "$entry"
            if [[ -n "${EXTRA_SOURCES[$name]:-}" ]]; then
                printf " extras=%s" "${EXTRA_SOURCES[$name]}"
            fi
            if expected_for_test "$name" "$file" >/dev/null 2>&1; then
                printf " expected=yes"
            else
                printf " expected=no"
            fi
            printf "\n"
        else
            printf "SKIP  %s - no _start/main/test_main entrypoint\n" "$name"
        fi
    done < <(list_all_candidates)
}

run_one_test() {
    local tmpdir="$1"
    local name="$2"
    local file="$3"
    local reason entry wrapper="" expected="" actual="" display_actual="" display_expected=""
    local elf="$tmpdir/${name}.elf"
    local btl="$tmpdir/${name}.btl"
    local uart="$tmpdir/${name}.uart"
    local profile_dir="$tmpdir/${name}_profile"
    local instr_count=""
    local -a sources=()
    mkdir -p "$profile_dir"

    if [[ -n "${FORCE_SKIP[$name]:-}" ]]; then
        printf "SKIP %s: %s\n" "$name" "${FORCE_SKIP[$name]}"
        return 0
    fi

    local want
    want="$(target_from_comment "$file")"
    TEST_CFLAGS="$TP_EXTRA_CFLAGS"
    if [[ -n "$want" ]]; then
        # -mt9000tvx is -mt9000 plus TVX, so it runs -mt9000 tests too.
        if [[ -n "$TP_TARGET" && "$TP_TARGET" != "$want" &&
              "$want$TP_TARGET" != "-mt9000-mt9000tvx" ]]; then
            printf "SKIP %s: needs %s\n" "$name" "$want"
            return 0
        fi
        TEST_CFLAGS="$TP_EXTRA_CFLAGS $want"
    fi
    case "$(needs_from_comment "$file"):$TP_TARGET" in
    fpu:-mt425|fpu:-mt414|t805ops:-mt800|t805ops:-mt414|softfp:|softfp:-mt[89]*)
        printf "SKIP %s: needs %s\n" "$name" "$(needs_from_comment "$file")"
        return 0 ;;
    esac
    TEST_CFLAGS="$TEST_CFLAGS ${EXTRA_CFLAGS[$name]:-}"

    if ! entry="$(has_entrypoint "$file")"; then
        printf "SKIP %s: no runnable entrypoint\n" "$name"
        return 0
    fi

    if [[ "$entry" == "main" ]]; then
        wrapper="$(make_main_wrapper "$tmpdir" "$name")"
    fi

    mapfile -t sources < <(resolve_sources "$name" "$file" "$entry" "$wrapper")

    if ! compile_test "$name" "$elf" "${sources[@]}"; then
        printf "FAIL %s: compile failed\n" "$name"
        return 1
    fi
    if ! "$NETLOAD_BIN" "$elf" "$btl" >/dev/null 2>&1; then
        printf "FAIL %s: netload failed\n" "$name"
        return 1
    fi

    run_uart_test "$btl" "$uart" "$profile_dir"
    if [[ -f "$uart" ]]; then
        actual="$(cat "$uart")"
    else
        actual=""
    fi

    instr_count="$(get_instruction_count "$profile_dir")"
    if [[ -n "$instr_count" ]]; then
        TEST_CYCLES["$name"]="$instr_count"
        TEST_ORDER+=("$name")
    fi

    if expected="$(expected_for_test "$name" "$file" 2>/dev/null)"; then
        if [[ "$actual" != "$expected" ]]; then
            display_actual="$(printf '%s' "$actual" | perl -0777 -pe 's/\n/\\n/g')"
            display_expected="$(printf '%s' "$expected" | perl -0777 -pe 's/\n/\\n/g')"
            printf "FAIL %s: UART mismatch\n" "$name"
            printf "  expected: %q\n" "$display_expected"
            printf "  actual:   %q\n" "$display_actual"
            return 1
        fi
        printf "PASS %s [%s instructions]\n" "$name" "${instr_count:-?}"
    else
        display_actual="$(printf '%s' "$actual" | perl -0777 -pe 's/\n/\\n/g')"
        printf "PASS %s (no oracle) [%s instructions] uart=%q\n" "$name" "${instr_count:-?}" "$display_actual"
    fi
}

main() {
    local mode="selected"
    local arg file name
    local -a requested=()
    local -a run_names=()
    local failures=0

    require_tools

    if [[ $# -eq 0 ]]; then
        mode="all"
    fi

    for arg in "$@"; do
        case "$arg" in
            --help|-h) usage; exit 0 ;;
            --list) print_list; exit 0 ;;
            --all) mode="all" ;;
            *) requested+=("$arg") ;;
        esac
    done

    if [[ "$mode" == "all" && ${#requested[@]} -eq 0 ]]; then
        while IFS= read -r file; do
            name="$(test_name_from_path "$file")"
            run_names+=("$name")
        done < <(list_all_candidates)
    else
        run_names=("${requested[@]}")
    fi

    if [[ ${#run_names[@]} -eq 0 ]]; then
        usage
        exit 1
    fi

    RUNNER_TMPDIR="$(mktemp -d)"
    trap 'rm -rf "$RUNNER_TMPDIR"' EXIT

    for name in "${run_names[@]}"; do
        file=""
        for arg in "$TEST_DIR/$name.c" "$TEST_DIR/$name.S"; do
            if [[ -f "$arg" ]]; then
                file="$arg"
                break
            fi
        done
        if [[ -z "$file" ]]; then
            printf "FAIL %s: not found in %s\n" "$name" "$TEST_DIR"
            failures=$((failures + 1))
            continue
        fi
        if ! run_one_test "$RUNNER_TMPDIR" "$name" "$file"; then
            failures=$((failures + 1))
        fi
    done

    # Write test_results.md
    if [[ ${#TEST_ORDER[@]} -gt 0 ]]; then
        {
            printf "# Test Results\n\n"
            printf "Generated: %s\n\n" "$(date -u '+%Y-%m-%d %H:%M:%S UTC')"
            printf "| Test | Instructions | Status |\n"
            printf "|------|-------------|--------|\n"
            for name in "${TEST_ORDER[@]}"; do
                local ic="${TEST_CYCLES[$name]:-?}"
                # Format with thousand separators
                local ic_fmt
                ic_fmt="$(printf "%'d" "$ic" 2>/dev/null || printf "%s" "$ic")"
                printf "| %-40s | %14s | PASS |\n" "$name" "$ic_fmt"
            done
            printf "\n**Total tests**: %d pass" "${#TEST_ORDER[@]}"
            if [[ $failures -ne 0 ]]; then
                printf ", %d fail" "$failures"
            fi
            printf "\n"
        } > "$RESULTS_MD"
        printf "\nResults written to %s\n" "$RESULTS_MD"
    fi

    if [[ $failures -ne 0 ]]; then
        printf "FAILURES: %d\n" "$failures"
        exit 1
    fi
    printf "All requested UART tests completed.\n"
}

main "$@"
