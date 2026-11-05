#!/bin/bash
# build-uclibc-part.sh - build uClibc-ng's libc.a for one transputer part.
#
# The tree's lib/libc.a is -mt9000 code: linked into a T800, T805, T425 or
# T414 program it brings instructions the part lacks (gtu in close, fstat,
# ...; on the T425/T414 the FPU instructions of printf's %f too).  This
# builds the same library with -m<cpu> in a copy of the tree, so the
# original and its objects are left alone, and installs the result beside
# the original as lib/libc-<cpu>.a, which doom/build-transputer.sh links
# for CPU=<cpu>.
#
# usage: bash transputer/scripts/build-uclibc-part.sh <t805|t800|t425|t414>
# env:   UCLIBC  the configured uClibc-ng tree (default transputer/uClibc-ng;
#                in WSL use one whose symlinked headers are real files,
#                e.g. ~/tinycc-transputer/transputer/uClibc-ng)
#        TCC     the transputer-tcc to build with (default: the tree's
#                ./transputer-tcc), passed to the tcc-gcc wrapper as
#                TCC_BIN_OVERRIDE
#        WORK    where the copy is built (default a new temp dir)
#        JOBS    make -j (default 4)
set -euo pipefail
CPU=${1:?usage: build-uclibc-part.sh <t805|t800|t425|t414>}
case $CPU in t805|t800|t425|t414) ;; *) echo "unknown part $CPU"; exit 1 ;; esac
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
UCLIBC=${UCLIBC:-$ROOT/transputer/uClibc-ng}
TCC=${TCC:-$ROOT/transputer-tcc}
WORK=${WORK:-$(mktemp -d)}
JOBS=${JOBS:-4}

[ -x "$TCC" ] || { echo "no transputer-tcc at $TCC"; exit 1; }
[ -f "$UCLIBC/.config" ] || { echo "$UCLIBC is not a configured uClibc-ng tree"; exit 1; }
mkdir -p "$WORK"
B="$WORK/uClibc-ng-$CPU"
echo "copying $UCLIBC -> $B"
cp -a "$UCLIBC/." "$B/"
find "$B" \( -name '*.o' -o -name '*.os' -o -name '*.oS' -o -name '*.a' \) -delete
# The repo's own wrappers (the tree's .config may name an older copy, and
# on NTFS the tcc-gcc/tcc-ar links are empty files), with the links made
# here and transputer-tcc beside them for tcc-ld-wrapper.sh.
mkdir -p "$WORK/bin"
for w in tcc-wrapper.sh tcc-ar-wrapper.sh tcc-ld-wrapper.sh tcc-nm-wrapper.sh \
         tcc-objcopy-wrapper.sh; do
    cp "$ROOT/transputer/$w" "$WORK/bin/$w"
    chmod +x "$WORK/bin/$w"
done
ln -sf tcc-wrapper.sh "$WORK/bin/tcc-gcc"
ln -sf tcc-wrapper.sh "$WORK/bin/tcc-strip"
ln -sf tcc-ar-wrapper.sh "$WORK/bin/tcc-ar"
ln -sf tcc-ld-wrapper.sh "$WORK/bin/tcc-ld"
ln -sf tcc-nm-wrapper.sh "$WORK/bin/tcc-nm"
ln -sf tcc-objcopy-wrapper.sh "$WORK/bin/tcc-objcopy"
ln -sf "$TCC" "$WORK/bin/transputer-tcc"
sed -i -e "s|^UCLIBC_EXTRA_CFLAGS=.*|UCLIBC_EXTRA_CFLAGS=\"-m$CPU\"|" \
       -e "s|^CROSS_COMPILER_PREFIX=.*|CROSS_COMPILER_PREFIX=\"$WORK/bin/tcc-\"|" "$B/.config"
grep -q "^UCLIBC_EXTRA_CFLAGS=\"-m$CPU\"" "$B/.config"
echo "building with $TCC -m$CPU (log: $B/build.log)"
(cd "$B" && LD_LIBRARY_PATH="" TCC_BIN_OVERRIDE="$TCC" make -j"$JOBS" > build.log 2>&1) || {
    tail -20 "$B/build.log"; echo "uClibc build failed"; exit 1; }
cp "$B/lib/libc.a" "$UCLIBC/lib/libc-$CPU.a"
echo "installed $UCLIBC/lib/libc-$CPU.a ($(stat -c%s "$UCLIBC/lib/libc-$CPU.a") bytes)"
