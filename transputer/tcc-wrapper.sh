#!/bin/bash
# tcc-wrapper: Bridges Linux Kbuild calls to Transputer TCC
# No GCC dependency — all compilation uses transputer-tcc.

# Linux uses GCC's predefined version macros both to reject obsolete host
# toolchains and to select compiler features.  Keep the 2.6.39-compatible
# default, but allow the modern-kernel build to request a newer facade.
TCC_GCC_VERSION="${TCC_GCC_VERSION:-4.4.0}"
IFS=. read -r TCC_GCC_MAJOR TCC_GCC_MINOR TCC_GCC_PATCH <<< "$TCC_GCC_VERSION"
TCC_GCC_MINOR="${TCC_GCC_MINOR:-0}"
TCC_GCC_PATCH="${TCC_GCC_PATCH:-0}"

if [ -n "${TCC_WRAPPER_DEBUG:-}" ]; then
    echo "tcc-wrapper called with: $*" >&2
fi

tool_name="$(basename "$0")"

if [[ "$tool_name" == "tcc-strip" ]]; then
    if [[ "$1" == "-v" || "$1" == "--version" || "$1" == "-V" ]]; then
        echo "GNU strip (GNU Binutils) 2.38"
        exit 0
    fi

    # BusyBox and Kbuild use strip as a post-link size reduction step.
    # The transputer TCC backend does not implement ELF section rewriting,
    # but these builds only need a successful output artifact. Treat the
    # requested strip/remove-section operations as a no-op copy.
    strip_input=""
    strip_output=""
    while [ $# -gt 0 ]; do
        case "$1" in
            -o)
                strip_output="$2"
                shift 2
                ;;
            -s|--strip-all|--strip-debug|--remove-section=*)
                shift
                ;;
            -R)
                shift 2
                ;;
            --)
                shift
                break
                ;;
            -*)
                shift
                ;;
            *)
                strip_input="$1"
                shift
                ;;
        esac
    done

    if [ -z "$strip_input" ]; then
        exit 0
    fi

    if [ -z "$strip_output" ]; then
        strip_output="$strip_input"
    fi
    if [ "$strip_input" != "$strip_output" ]; then
        cp "$strip_input" "$strip_output"
    fi
    chmod a+x "$strip_output"
    exit 0
fi

# 1. Configuration
# ----------------
LOGfile="/tmp/tcc-wrapper.log"
# Resolve path to the actual transputer-tcc binary
self_path="${BASH_SOURCE[0]:-$0}"
SCRIPT_DIR="$(cd "$(dirname "$self_path")" && pwd)"
if [ -n "${TCC_BIN_OVERRIDE:-}" ] && [ -x "$TCC_BIN_OVERRIDE" ]; then
    # Escape hatch for A/B codegen measurement: point the wrapper at an
    # alternate transputer-tcc build without disturbing the tree.
    TCC_BIN="$TCC_BIN_OVERRIDE"
elif [ -x "$SCRIPT_DIR/../transputer-tcc" ]; then
    TCC_BIN="$SCRIPT_DIR/../transputer-tcc"
elif [ -x "$SCRIPT_DIR/transputer-tcc" ]; then
    TCC_BIN="$SCRIPT_DIR/transputer-tcc"
else
    echo "CRITICAL ERROR: transputer-tcc not found relative to wrapper." >&2
    exit 1
fi

# 2. Argument Parsing & Filtering
# -------------------------------
args=()
dep_file=""
dep_mode=""
dep_phony=false
do_S=false
output_file=""
source_file=""
link_tmpdir=""

echo "[$PWD] tcc-wrapper $@" >> "$LOGfile"

# Modern Kbuild probes the compiler and integrated assembler before it has a
# configuration.  Answer these probes directly: TCC emits line markers even
# with -P, and it has no GNU-style `-Wa,--version` output of its own.
raw_args=" $* "
if [[ "$raw_args" == *" -E "* && "$raw_args" == *" -P "* &&
      "$raw_args" == *" -x c "* && "$raw_args" == *" - "* ]]; then
    printf 'GCC\t%s  %s  %s\n' "$TCC_GCC_MAJOR" "$TCC_GCC_MINOR" "$TCC_GCC_PATCH"
    exit 0
fi
if [[ "$raw_args" == *" -Wa,--version "* ]]; then
    echo "GNU assembler (GNU Binutils) 2.38"
    exit 0
fi

while [ $# -gt 0 ]; do
    case "$1" in
        # --- Masquerade as the selected GCC version ---
        -v)
            if [ $# -eq 1 ]; then
                echo "gcc version $TCC_GCC_VERSION (Transputer TCC)"
                exit 0
            fi
            shift
            ;;
        --version)
            echo "gcc (GCC) $TCC_GCC_VERSION"
            exit 0
            ;;
        -dumpversion)
            echo "$TCC_GCC_VERSION"
            exit 0
            ;;
        -dumpmachine)
            echo "transputer-linux-gnu"
            exit 0
            ;;
        -print-file-name=*)
            # Return empty — TCC has no internal include dir in the GCC sense
            echo ""
            exit 0
            ;;
        -print-libgcc-file-name)
            # TCC has no libgcc; return empty path
            echo ""
            exit 0
            ;;

        -isystem)
            if [ $# -ge 2 ] && [ -n "$2" ] && [[ "$2" != -* ]]; then
                args+=("-isystem" "$2")
                shift 2
            else
                shift
            fi
            ;;

        # --- Filter Unsupported GCC Flags ---
        -fstack-protector*|-fconserve-stack|-fno-var-tracking-assignments) shift ;;
        -fno-strict-overflow|-fverbose-asm|-fno-delete-null-pointer-checks) shift ;;
        -funsigned-char) args+=("-funsigned-char"); shift ;;
        -fno-builtin|-fcommon|-fno-common) shift ;; # TCC defaults
        -Wold-style-definition) shift ;; # TCC default behavior
        -Wa,*) shift ;; # Assembler passthrough — TCC doesn't support
        -Wl,--start-group|-Wl,--end-group) shift ;; # TCC links all objects as group
        -Wl,--whole-archive|-Wl,--no-whole-archive) shift ;; # TCC includes all archive members
        -Wl,*) shift ;; # Drop other linker passthrough flags TCC doesn't understand
        -Wno-*|-Werror=*|-Wframe-larger-than=*|-Wdeclaration-after-statement) shift ;;
        -mno-*|-m64|-m32|-mcmodel=*|-G*) shift ;;
        -O*) shift ;;
        -std=gnu*) shift ;;
        -DCC_HAVE_ASM_GOTO) shift ;; # TCC doesn't support asm goto
        -P) shift ;; # TCC doesn't support -P (inhibit line markers)
        -x) shift; shift ;; # TCC doesn't need -x language specifier
        -dN) args+=("-dM"); shift ;; # TCC -dN doesn't emit defines; use -dM instead
        -dM|-dD) args+=("$1"); shift ;; # Preprocessor define listing modes

        # --- Handle Dependencies ---
        -Wp,-MMD,*)
            dep_file="${1#-Wp,-MMD,}"
            dep_mode="-MMD"
            shift
            ;;
        -Wp,-MD,*)
            dep_file="${1#-Wp,-MD,}"
            dep_mode="-MD"
            shift
            ;;
        -MMD|-MD)
            dep_mode="$1"
            shift
            ;;
        -MP)
            dep_phony=true
            shift
            ;;
        -MT) shift; shift ;; # skip -MT <target>
        -MF)
            dep_file="$2"
            shift 2
            ;;

        # --- Core Compilation Flags ---
        -S)
            do_S=true
            shift
            ;;
        -c)
            args+=("-c")
            shift
            ;;
        -E)
            args+=("-E")
            shift
            ;;
        -o)
            output_file="$2"
            args+=("-o" "$2")
            shift 2
            ;;

        # --- Handle -include (force-include a header) ---
        -include)
            args+=("-include" "$2")
            shift 2
            ;;

        # --- Capture source file ---
        *.c|*.S)
            source_file="$1"
            args+=("$1")
            shift
            ;;
        # --- Handle stdin as source ---
        -)
            args+=("-")
            shift
            ;;

        # --- Pass everything else ---
        *)
            args+=("$1")
            shift
            ;;
    esac
done

is_archive_file() {
    local path="$1"

    [ -f "$path" ] || return 1
    printf '!<arch>\n' | cmp -s -n 8 - "$path"
}

# 3. Special case: cc-version.sh detection
if [[ "${args[*]}" == *"-E -P -x c"* ]]; then
    echo "TCC	0 9 28"
    exit 0
fi

# 3b. Special case: as-version.sh detection
if [[ "${args[*]}" == *"-Wa,--version"* ]]; then
    exec "$TCC_BIN" "$@"
fi

# 4. Handle Kbuild's generated-offset assembly
# ---------------------------------------------
# TCC has no -S mode.  These files are never linked into the target: Kbuild
# parses their .ascii markers to make C headers.  Calculate them with an ILP32
# host compiler, whose data model and aggregate alignment match Transputer.
if [ "$do_S" = true ]; then
    echo "Handling -S for: $source_file -> $output_file" >> "$LOGfile"

    if [[ "$source_file" == *"bounds.c" || "$source_file" == *"offsets.c" ]]; then
        offset_cc="${TCC_OFFSETS_CC:-/usr/bin/clang}"
        offset_args=()
        for ((i = 0; i < ${#args[@]}; i++)); do
            case "${args[$i]}" in
                -include|-o)
                    offset_args+=("${args[$i]}" "${args[$((i + 1))]}")
                    i=$((i + 1))
                    ;;
                -I*|-D*|-U*|-nostdinc|*.c)
                    offset_args+=("${args[$i]}")
                    ;;
            esac
        done
        "$offset_cc" -m32 -std=gnu11 -fms-extensions "${offset_args[@]}" -S
        ret=$?
        if [ $ret -eq 0 ] && [ -n "$dep_file" ]; then
            echo "$output_file: $source_file" > "$dep_file"
        fi
        exit $ret

    else
        echo "ERROR: -S requested for unknown file: $source_file" >&2
        echo "tcc-wrapper only supports -S for Kbuild offset generators." >&2
        exit 1
    fi
fi

# 5. Base defines for kernel headers
# -----------------------------------
TCC_ROOT="$(cd "$(dirname "$TCC_BIN")" && pwd)"
tcc_defines=(
    "-mt9000"
    "-w"
    "-Wno-implicit-function-declaration"
    "-D__GNUC__=$TCC_GCC_MAJOR"
    "-D__GNUC_MINOR__=$TCC_GCC_MINOR"
    "-D__GNUC_PATCHLEVEL__=$TCC_GCC_PATCH"
    "-D__TCC_KERNEL_BUILD__=1"
    # TCC has no optimizer pragma implementation.  Treat GCC's _Pragma
    # operator as an intentional no-op instead of making common headers
    # carry compiler-specific branches.
    "-D_Pragma(x)="
    "-D__alias(x)=__attribute__((alias(x)))"
    # uClibc uses __GI_* hidden aliases via attribute((alias));
    # TCC's alias support can't handle the __hidden_ver1 pattern.
    # __BCC__ disables the hidden_proto/hidden_def path in libc-symbols.h
    "-D__BCC__=1"
    # Ensure libc_hidden_proto/def are defined (empty) even without libc-symbols.h
    "-Dlibc_hidden_proto(name,...)="
    "-Dlibc_hidden_def(name)="
    "-Dlibc_hidden_weak(name)="
    "-Dlibm_hidden_proto(name,...)="
    "-Dlibm_hidden_def(name)="
    "-DTCC_TARGET_TRANSPUTER"
    "-I${TCC_ROOT}/include/transputer" "-I${TCC_ROOT}/include"
    # Disable section annotations (__init, __meminit, __ref, etc.)
    # TCC linker resolves cross-section function calls to wrong addresses,
    # so all code must stay in .text
    "-D__section(S)="
)

# 6. Execute TCC
# --------------

# Filter -nostdinc from args; we'll re-add it at the right position
# TCC with -nostdinc ignores ALL include paths set before it, so we need:
# -nostdinc first, then TCC's own include dir, then user includes
filtered_args=()
has_nostdinc=false
for arg in "${args[@]}"; do
    if [ "$arg" = "-nostdinc" ]; then
        has_nostdinc=true
    else
        abs_arg="$arg"
        if [[ "$abs_arg" != /* ]]; then
            abs_arg="$PWD/$abs_arg"
        fi

        # BusyBox/Kbuild can emit empty archives named built-in.o.
        # Skip canonical empty archives and expand non-empty archive-shaped
        # placeholders so the linker never sees them as malformed .o files.
        if is_archive_file "$abs_arg" && [[ "$arg" != *.a ]]; then
            if [ ! -s "$abs_arg" ] || [ "$(stat -c %s "$abs_arg" 2>/dev/null || echo 0)" -le 8 ]; then
                continue
            fi

            if [ -z "$link_tmpdir" ]; then
                link_tmpdir="$(mktemp -d)"
                trap "rm -rf '$link_tmpdir'" EXIT
            fi

            extract_dir="$link_tmpdir/$(basename "$arg").$$.${#filtered_args[@]}"
            mkdir -p "$extract_dir"
            (cd "$extract_dir" && ar x "$abs_arg")

            for obj in "$extract_dir"/*.o; do
                if [ -f "$obj" ]; then
                    filtered_args+=("$obj")
                fi
            done
        else
            filtered_args+=("$arg")
        fi
    fi
done

echo "TCC args: ${filtered_args[*]}" >> "$LOGfile"

# TCC supports native dependency generation, but its -Wp argument splitter
# treats the depfile as another input.  Kbuild uses -Wp,-MMD,<depfile>, so
# normalize that spelling to TCC's reliable -MMD -MF <depfile> form.
dep_args=()
if [ -n "$dep_mode" ]; then
    dep_args+=("$dep_mode")
    if [ -n "$dep_file" ]; then
        dep_args+=("-MF" "$dep_file")
    fi
    if [ "$dep_phony" = true ]; then
        dep_args+=("-MP")
    fi
fi

if [ "$has_nostdinc" = true ]; then
    "$TCC_BIN" -nostdinc "-I${TCC_ROOT}/include/transputer" "-I${TCC_ROOT}/include" "${tcc_defines[@]}" "${dep_args[@]}" "${filtered_args[@]}"
else
    "$TCC_BIN" "${tcc_defines[@]}" "${dep_args[@]}" "${filtered_args[@]}"
fi
ret=$?

# 7. Post-Processing: Dependency Generation
# -----------------------------------------
if [ $ret -eq 0 ] && [ -n "$dep_file" ]; then
    if [ ! -f "$dep_file" ]; then
        if [ -n "$output_file" ] && [ -n "$source_file" ]; then
            echo "$output_file: $source_file" > "$dep_file"
        else
            echo "# dummy dependency" > "$dep_file"
        fi
    fi
fi

exit $ret
