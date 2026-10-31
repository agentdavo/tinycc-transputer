#!/bin/bash
# objcopy-wrapper: Handling Transputer (EM 805) binaries
#
# 1. Locates best available objcopy (LLVM > GNU).
# 2. Handles "Unknown Architecture" errors by temporarily spoofing
#    the ELF header to look like i386 (EM_386), performing the copy,
#    and restoring the header in the output if necessary.

# 1. Tool Discovery
# -----------------
# Search order: User override -> Specific LLVM versions -> Generic LLVM -> GNU
if [ -n "$OBJCOPY_OVERRIDE" ]; then
    OBJCOPY="$OBJCOPY_OVERRIDE"
elif command -v llvm-objcopy-mp-19 >/dev/null 2>&1; then OBJCOPY="llvm-objcopy-mp-19"
elif command -v llvm-objcopy-mp-18 >/dev/null 2>&1; then OBJCOPY="llvm-objcopy-mp-18"
elif command -v llvm-objcopy >/dev/null 2>&1; then       OBJCOPY="llvm-objcopy"
elif command -v objcopy >/dev/null 2>&1; then            OBJCOPY="objcopy"
else
    echo "CRITICAL: No objcopy found. Install llvm or binutils." >&2
    exit 1
fi

# 2. Argument Parsing
# -------------------
# We need to identify Input and Output files to handle spoofing.
# objcopy usage is typically: objcopy [flags] input [output]
ARGS=()
FILES=()

for arg in "$@"; do
    if [[ "$arg" == -* ]]; then
        ARGS+=("$arg")
    else
        FILES+=("$arg")
    fi
done

# Standard objcopy logic:
# If 1 file arg: Input modifies in-place (or output generated based on flags)
# If 2 file args: Input -> Output
INPUT_FILE=""
OUTPUT_FILE=""

if [ ${#FILES[@]} -ge 1 ]; then
    INPUT_FILE="${FILES[0]}"
fi
if [ ${#FILES[@]} -ge 2 ]; then
    OUTPUT_FILE="${FILES[1]}"
fi

# 3. Execution Helper
# -------------------
# Constants for ELF Header modification
# Offset 18 (0x12) is e_machine (uint16_t)
OFFSET=18
# 805 = 0x0325 (Little Endian: 25 03)
HEX_TP="\x25\x03"
# 3 = 0x0003 (Little Endian: 03 00) - i386
HEX_X86="\x03\x00"

# Attempt 1: Direct Execution
# Capture stderr to suppress "unknown architecture" noise on failure
ERR_LOG=$(mktemp)
"$OBJCOPY" "$@" 2>"$ERR_LOG"
RET=$?

if [ $RET -eq 0 ]; then
    rm -f "$ERR_LOG"
    exit 0
fi

# Check if failure was likely architecture related
ERR_MSG=$(cat "$ERR_LOG")
rm -f "$ERR_LOG"

# The linked vmlinux is a flat ELF with no section headers, so dumping any
# section from it fails with "section ... not found".  Linux 7.4 extracts
# .modinfo with --dump-section; 7.2's "-j .modinfo -O binary" gave an empty
# file instead.  For an input with no section headers, write each dump empty
# and run the rest of the command.
if [[ "$ERR_MSG" == *"section '"*"' not found"* ]]; then
    DUMPS=()
    REST=()
    SECTIONLESS=0
    prev=""
    for arg in "$@"; do
        if [[ "$prev" == "--dump-section" ]]; then
            DUMPS+=("${arg#*=}")
        elif [[ "$arg" == --dump-section=* ]]; then
            arg="${arg#--dump-section=}"
            DUMPS+=("${arg#*=}")
        elif [[ "$arg" != "--dump-section" ]]; then
            REST+=("$arg")
            # e_shnum is the 16-bit word at offset 48 of an ELF32 header.
            if [[ -f "$arg" && "$(head -c 4 "$arg")" == $'\x7fELF' &&
                  "$(od -An -tu2 -j48 -N2 "$arg" | tr -d ' ')" == "0" ]]; then
                SECTIONLESS=1
            fi
        fi
        prev="$arg"
    done
    if [[ $SECTIONLESS -eq 1 && ${#DUMPS[@]} -gt 0 ]]; then
        for f in "${DUMPS[@]}"; do
            : > "$f" || exit 1
        done
        exec "$0" "${REST[@]}"
    fi
fi

if [[ "$ERR_MSG" != *"arch"* && "$ERR_MSG" != *"format"* ]]; then
    # It failed for some other reason (file not found, bad flag)
    # Output the error and exit
    echo "$ERR_MSG" >&2
    exit $RET
fi

# 4. Fallback: The Spoof Maneuver
# -------------------------------
# If we are here, objcopy rejected the Transputer ID.
# We will create a temp input, patch it to i386, run objcopy, 
# and if the output is an ELF, patch it back to Transputer.

if [ ! -f "$INPUT_FILE" ]; then
    echo "$ERR_MSG" >&2
    exit $RET
fi

# Create temp input
TMP_IN=$(mktemp)
cp "$INPUT_FILE" "$TMP_IN"

# Patch TMP_IN to i386 (0x03)
printf "$HEX_X86" | dd of="$TMP_IN" bs=1 seek=$OFFSET count=2 conv=notrunc >/dev/null 2>&1

# Determine actual output target
# If no output file specified, objcopy modifies input in-place.
# In our spoofing scenario, that means it modifies TMP_IN.
ACTUAL_OUT="$OUTPUT_FILE"
if [ -z "$ACTUAL_OUT" ]; then
    # In-place modification mode
    # We run against temp, then move temp back to input
    ACTUAL_OUT="$TMP_IN"
fi

# Build new command args
# Replace input filename in ARGS with TMP_IN
NEW_ARGS=()
FILE_IDX=0
for arg in "$@"; do
    if [[ "$arg" == "$INPUT_FILE" && $FILE_IDX -eq 0 ]]; then
        NEW_ARGS+=("$TMP_IN")
        FILE_IDX=1
    elif [[ -n "$OUTPUT_FILE" && "$arg" == "$OUTPUT_FILE" ]]; then
        NEW_ARGS+=("$ACTUAL_OUT")
    else
        NEW_ARGS+=("$arg")
    fi
done

# Run objcopy on spoofed file
"$OBJCOPY" "${NEW_ARGS[@]}"
RET=$?

if [ $RET -eq 0 ]; then
    # Success! 
    
    # If the output is ELF, we probably want to restore the ID to Transputer.
    # If the output is Binary (-O binary), we touch nothing.
    
    # Check magic bytes of output (7F 45 4C 46 = .ELF)
    MAGIC=$(head -c 4 "$ACTUAL_OUT")
    if [[ "$MAGIC" == $'\x7fELF' ]]; then
        # Patch output back to Transputer ID (805)
        printf "$HEX_TP" | dd of="$ACTUAL_OUT" bs=1 seek=$OFFSET count=2 conv=notrunc >/dev/null 2>&1
    fi

    # If we were doing in-place modification, copy temp back to original input path
    if [ -z "$OUTPUT_FILE" ]; then
        cp "$TMP_IN" "$INPUT_FILE"
    fi
else
    # Fallback failed too, report original error
    echo "$ERR_MSG" >&2
fi

# Cleanup
rm -f "$TMP_IN"
exit $RET
