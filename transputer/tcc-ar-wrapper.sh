#!/bin/bash
# ar-wrapper: Bridges Linux Kbuild ar calls to BSD ar / TCC requirements
#
# 1. Strips unsupported GNU flags (D, P, T).
# 2. "Inflates" Thin Archives (T) into full archives.
# 3. Flattens nested archives (archives inside archives) which TCC's linker hates.

# Exit on error
set -e

# 1. Parse Flags
# --------------
# The kernel usually calls: ar rcST output.a input.o ...
raw_flags="$1"
shift

# Archive inspection and member ordering do not create a new archive.  Kbuild
# uses these while constructing built-in-fixup.a:
#
#   ar t archive
#   ar mPi <position> archive <members...>
#
# Treating the first argument of either command as an output archive corrupts
# the archive (and used to reduce built-in-fixup.a to an empty ARMAG header).
# The host ar is already required below for extraction and archive creation, so
# let it implement these operations directly.
case "$raw_flags" in
    *t*|*m*)
        exec ar "$raw_flags" "$@"
        ;;
esac

# Sanitize flags for BSD ar (macOS/generic)
# Remove:
#   D: Deterministic mode (BSD ar default usually, or unsupported)
#   P: Full path matching (Unsupported by BSD)
#   T: Thin archive (We MUST remove this and do manual flattening)
flags=$(echo "$raw_flags" | tr -d 'DPT')

# Ensure we have at least create/replace/save-index
if [[ "$flags" != *c* ]]; then flags="c$flags"; fi
if [[ "$flags" != *r* ]]; then flags="r$flags"; fi
if [[ "$flags" != *s* ]]; then flags="s$flags"; fi

# 2. Parse Arguments
# ------------------
archive_out="$1"
shift

# Expand @file response files: replace @path with contents of that file
expanded_args=()
for arg in "$@"; do
    if [[ "$arg" == @* ]]; then
        resp_file="${arg#@}"
        # Read the response file, handling whitespace-separated entries
        while IFS= read -r line; do
            for word in $line; do
                expanded_args+=("$word")
            done
        done < "$resp_file"
    else
        expanded_args+=("$arg")
    fi
done
set -- "${expanded_args[@]}"

# If no inputs, create empty archive and exit
if [ $# -eq 0 ]; then
    if ! ar "$flags" "$archive_out" 2>/dev/null; then
        # A zero-byte placeholder is not a valid archive and confuses TCC's
        # archive loader; emit the canonical ARMAG header instead.
        printf '!<arch>\n' > "$archive_out"
    fi
    exit 0
fi

# Get absolute path of output because we will be changing directories
if [[ "$archive_out" != /* ]]; then
    archive_out="$(pwd)/$archive_out"
fi

# 3. Processing
# -------------
# We create a staging area.
# - Object files are copied here.
# - Archives are extracted here (recursively).

tmpdir=$(mktemp -d)
trap "rm -rf '$tmpdir'" EXIT

# Counter to ensure every extracted archive gets a unique directory
# to prevent filename collisions (e.g. built-in.a containing init.o vs main.a containing init.o)
extract_count=0

# Kbuild relies on archive member order for same-level initcalls and other
# linker-set sections. Keep an explicit ordered list; filesystem traversal
# and shell globs do not preserve the order in which inputs were supplied.
ordered_objects=()

# Helper: Recursive extraction
process_input() {
    local file="$1"
    
    # Resolve absolute path for the input file
    local abs_path=""
    if [[ "$file" == /* ]]; then
        abs_path="$file"
    else
        abs_path="$PWD/$file"
    fi

    if [[ "$file" == *.a ]]; then
        # It's an archive (Thin or Static). We must explode it.
        # Create a unique subfolder for this archive's contents
        local sub_dir="$tmpdir/ex_${extract_count}"
        mkdir -p "$sub_dir"
        extract_count=$((extract_count + 1))

        # Record the archive directory before extraction. `ar x` writes in
        # member order, but directory enumeration later is unspecified.
        local archive_members=()
        mapfile -t archive_members < <(ar t "$abs_path")

        # Pushd, extract, process members in archive-table order, popd.
        pushd "$sub_dir" > /dev/null
        ar x "$abs_path"

        local extracted
        for extracted in "${archive_members[@]}"; do
            [ -f "$extracted" ] || continue
            if [[ "$extracted" == *.a ]]; then
                process_input "$extracted"
            elif [[ "$extracted" == *.o ]]; then
                ordered_objects+=("$sub_dir/$extracted")
            fi
        done
        popd > /dev/null
    elif [[ "$file" == *.o ]]; then
        # It's an object file.
        # To prevent collisions (e.g. drivers/net/main.o vs drivers/gpu/main.o),
        # we generate a flattened name based on the path.
        local flat_name
        # Replace / with _
        flat_name=$(echo "$file" | sed 's|^/||; s|^\./||; s|/|_|g')
        
        # A private staging directory avoids overwriting equal basenames while
        # retaining the member name expected by Kbuild's later `ar mPi` step.
        local obj_dir="$tmpdir/obj_${extract_count}"
        mkdir -p "$obj_dir"
        extract_count=$((extract_count + 1))
        cp "$abs_path" "$obj_dir/$flat_name"
        ordered_objects+=("$obj_dir/$flat_name")
    fi
}

# Iterate over all command line inputs
for arg in "$@"; do
    process_input "$arg"
done

# 4. Creation
# -----------
# Delete the output archive if it exists (we are rebuilding it flat)
rm -f "$archive_out"

# Add members in the exact recursive input/archive order. This is essential
# for Linux linker sets: initcalls at the same level retain object link order.
if [ "${#ordered_objects[@]}" -eq 0 ]; then
    printf '!<arch>\n' > "$archive_out"
    exit 0
fi

ar "$flags" "$archive_out" "${ordered_objects[@]}"

exit 0
