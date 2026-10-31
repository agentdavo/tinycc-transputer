#!/bin/bash
# nm-wrapper: Extracts symbols from Transputer binaries
#
# PROBLEM: System 'nm' often rejects binaries with unknown Machine IDs (805).
# SOLUTION: 
# 1. Try 'llvm-nm' if available (it is promiscuous/tolerant).
# 2. Try 'nm'. 
# 3. If 'nm' fails, patch the ELF header of a temp copy to look like x86 (EM_386),
#    extract symbols, and pretend nothing happened.

# 1. Configuration
# ----------------
NM_BIN="nm"
# Prefer llvm-nm if installed, as it ignores unknown Machine IDs
if command -v llvm-nm >/dev/null 2>&1; then
    NM_BIN="llvm-nm"
fi

# 2. Parsing
# ----------
FLAGS=()
FILES=()

for arg in "$@"; do
    if [[ "$arg" == -* ]]; then
        FLAGS+=("$arg")
    else
        FILES+=("$arg")
    fi
done

# 3. Execution Helper
# -------------------
try_nm() {
    local bin="$1"
    local file="$2"
    
    # Run nm, capture stderr
    ERR=$("$bin" "${FLAGS[@]}" "$file" 2>&1 >/dev/null)
    local ret=$?
    
    # If successful, output stdout and return
    if [ $ret -eq 0 ]; then
        "$bin" "${FLAGS[@]}" "$file"
        return 0
    fi
    
    # If failed, return error code
    return $ret
}

emit_tccsym() {
    local file="$1"
    local map_file="${file}.tccsym"
    if [ -f "$map_file" ]; then
        cat "$map_file"
        return 0
    fi
    return 1
}

# 4. Main Logic
# -------------

# If no files provided (just flags or help), just run nm
if [ ${#FILES[@]} -eq 0 ]; then
    exec "$NM_BIN" "$@"
fi

# Process files
for file in "${FILES[@]}"; do
    if [ ! -f "$file" ]; then
        # Let nm handle file-not-found errors
        "$NM_BIN" "${FLAGS[@]}" "$file"
        continue
    fi

    # Attempt 1: Standard Execution
    # Use subshell to capture output, prevent partial writes on failure
    OUTPUT=$("$NM_BIN" "${FLAGS[@]}" "$file" 2>/dev/null)
    RET=$?

    if [ $RET -eq 0 ]; then
        if [ -z "$OUTPUT" ] || echo "$OUTPUT" | grep -qi "no symbols"; then
            if emit_tccsym "$file"; then
                continue
            fi
        fi
        echo "$OUTPUT"
    else
        # Attempt 2: Header Spoofing (The "Transputer Mask")
        # Copy file, patch Machine ID to 0x03 (i386), run nm
        
        TMP_FILE=$(mktemp)
        cp "$file" "$TMP_FILE"
        
        # Patch Byte 18 (e_machine) to 0x03 (EM_386)
        # 0x12 (18) is the offset for e_machine in Elf32_Ehdr
        printf '\x03' | dd of="$TMP_FILE" bs=1 seek=18 count=1 conv=notrunc >/dev/null 2>&1
        
        # Run nm on spoofed file
        OUTPUT=$("$NM_BIN" "${FLAGS[@]}" "$TMP_FILE" 2>/dev/null)
        RET=$?
        
        # Cleanup
        rm -f "$TMP_FILE"
        
        if [ $RET -eq 0 ]; then
            # Success!
            # If nm output includes filenames (e.g. print-file-name option), 
            # we must sed the temp filename back to the real filename.
            if [ -z "$OUTPUT" ] || echo "$OUTPUT" | grep -qi "no symbols"; then
                if emit_tccsym "$file"; then
                    continue
                fi
            fi
            echo "$OUTPUT" | sed "s|$(basename "$TMP_FILE")|$file|g"
        else
            # Total failure, print original error
            "$NM_BIN" "${FLAGS[@]}" "$file"
            exit $RET
        fi
    fi
done

exit 0
