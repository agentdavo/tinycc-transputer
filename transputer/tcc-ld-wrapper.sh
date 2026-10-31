#!/bin/bash
# ld-wrapper: Bridges Linux Kbuild ld calls to Transputer TCC
#
# 1. Spoofs GNU ld version.
# 2. Filters unsupported flags (-z, -T, --build-id).
# 3. Explodes archives (.a) into object files (.o) because TCC's linker 
#    prefers direct objects for symbol resolution.

# Exit on error
set -e

# 1. Version Spoofing
# -------------------
if [[ "$1" == "-v" || "$1" == "--version" || "$1" == "-V" ]]; then
    echo "GNU ld (GNU Binutils) 2.38"
    exit 0
fi

# 2. Configuration
# ----------------
# Resolve tcc path
self_path="${BASH_SOURCE[0]:-$0}"
SCRIPT_DIR="$(cd "$(dirname "$self_path")" && pwd)"
if [ -x "$SCRIPT_DIR/transputer-tcc" ]; then
    LD_BIN="$SCRIPT_DIR/transputer-tcc"
elif [ -x "$SCRIPT_DIR/../transputer-tcc" ]; then
    LD_BIN="$SCRIPT_DIR/../transputer-tcc"
else
    echo "CRITICAL: transputer-tcc not found." >&2
    exit 1
fi

# Temp dir for archive extraction
TMPDIR=$(mktemp -d)
# Clean up on exit (Comment out to debug)
trap "rm -rf '$TMPDIR'" EXIT

is_archive_file() {
    local path="$1"

    [ -f "$path" ] || return 1
    printf '!<arch>\n' | cmp -s -n 8 - "$path"
}

# 3. Argument Parsing
# -------------------
ARGS=()
ARCHIVE_COUNT=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        # --- Pass-through Flags ---
        -o)
            ARGS+=("-o" "$2")
            shift 2
            ;;
        -r|-Ur)
            # Relocatable link (partial linking) - TCC supports this
            ARGS+=("-r")
            shift
            ;;
        -L*|-l*)
            # Library paths and libs
            ARGS+=("$1")
            shift
            ;;
        -m)
            # Emulation mode (e.g. elf_i386) - Ignore, we force Transputer
            shift 2
            ;;
        
        # --- Drop Unsupported Flags ---
        -z|-T|--script)
            # Drop flag AND argument
            # We ignore -T (linker script) because transputer-link.c
            # handles the specific memory layout (0x80000070).
            shift 2
            ;;
        --script=*|-T=*)
            # Drop flag with embedded argument (--script=file.lds)
            shift
            ;;
        --build-id*|--orphan-handling*|--strip-debug|--sort-section*|--no-warn-rwx-segments)
            # Drop flag
            shift
            ;;
        --whole-archive|--no-whole-archive|--start-group|--end-group)
            # TCC treats everything as whole-archive/grouped by default 
            # when passed object files directly.
            shift
            ;;

        # --- Handle Archives ---
        *.a)
            # TCC often misses symbols in archives if not ordered perfectly.
            # We extract them to force inclusion (mimics --whole-archive).
            
            # 1. Create unique dir
            extract_dir="$TMPDIR/$ARCHIVE_COUNT"
            mkdir -p "$extract_dir"
            
            # 2. Get absolute path of archive
            arch_path="$1"
            if [[ "$arch_path" != /* ]]; then arch_path="$PWD/$arch_path"; fi
            
            # 3. Extract (Fast method)
            pushd "$extract_dir" > /dev/null
            ar x "$arch_path"
            popd > /dev/null
            
            # 4. Add extracted objects in archive-table order. A glob sorts
            # names alphabetically and silently breaks Linux's same-level
            # initcall ordering (for example socket before netlink).
            while IFS= read -r member; do
                obj="$extract_dir/$member"
                if [[ "$member" == *.o && -f "$obj" ]]; then
                    ARGS+=("$obj")
                fi
            done < <(ar t "$arch_path")
            
            ARCHIVE_COUNT=$((ARCHIVE_COUNT + 1))
            shift
            ;;
            
        # --- Pass Objects ---
        *)
            arg_path="$1"
            if [[ "$arg_path" != /* ]]; then
                arg_path="$PWD/$arg_path"
            fi

            # BusyBox/Kbuild may emit empty archives named built-in.o.
            # Detect archives by magic, not suffix, so these become no-ops
            # instead of being passed to the linker as malformed objects.
            if is_archive_file "$arg_path"; then
                extract_dir="$TMPDIR/$ARCHIVE_COUNT"
                mkdir -p "$extract_dir"

                pushd "$extract_dir" > /dev/null
                ar x "$arg_path" 2>/dev/null || true
                popd > /dev/null

                while IFS= read -r member; do
                    obj="$extract_dir/$member"
                    if [[ "$member" == *.o && -f "$obj" ]]; then
                        ARGS+=("$obj")
                    fi
                done < <(ar t "$arg_path")

                ARCHIVE_COUNT=$((ARCHIVE_COUNT + 1))
            else
                ARGS+=("$1")
            fi
            shift
            ;;
    esac
done

# 4. Execution
# ------------
# -nostdlib is crucial for kernel: prevents linking crt0.o / libc

# Handle empty link (no input .o files) — produce a minimal empty ELF.
# This happens for unconfigured subsystems (e.g. drivers/gpu/drm/).
has_input=0
output_file=""
for a in "${ARGS[@]}"; do
    case "$a" in
        -o) ;;
        -r|-nostdlib) ;;
        *.o|*.a)
            if [[ "$a" != "$output_file" ]]; then has_input=1; fi
            ;;
    esac
    if [[ "$a" == "-o" ]]; then
        # Next arg is the output file
        :
    fi
done
# Simpler check: count non-flag args that look like inputs
input_count=0
skip_next=0
for a in "${ARGS[@]}"; do
    if [[ $skip_next -eq 1 ]]; then skip_next=0; continue; fi
    case "$a" in
        -o) skip_next=1 ;;
        -*) ;;
        *) input_count=$((input_count + 1)) ;;
    esac
done
if [[ $input_count -eq 0 ]]; then
    # No input files — produce empty object via a trivial .c
    empty_c="$TMPDIR/empty.c"
    echo "/* empty */" > "$empty_c"
    # Find the -o argument
    out=""
    skip_next=0
    for a in "${ARGS[@]}"; do
        if [[ $skip_next -eq 1 ]]; then out="$a"; break; fi
        if [[ "$a" == "-o" ]]; then skip_next=1; fi
    done
    if [[ -n "$out" ]]; then
        "$LD_BIN" -nostdlib -r -c -o "$out" "$empty_c" 2>/dev/null || true
    fi
    exit 0
fi

STDERR_LOG="$TMPDIR/ld.stderr"
if "$LD_BIN" -nostdlib "${ARGS[@]}" 2>"$STDERR_LOG"; then
    if [ -s "$STDERR_LOG" ]; then
        if [ -n "${TCC_LD_WRAPPER_DEBUG:-}" ]; then
            cat "$STDERR_LOG" >&2
        else
            # TCC may emit non-fatal duplicate-definition diagnostics while still
            # producing a valid output for this build flow; suppress those only.
            grep -Ev "defined twice$" "$STDERR_LOG" >&2 || true
        fi
    fi
    exit 0
else
    cat "$STDERR_LOG" >&2
    exit 1
fi
