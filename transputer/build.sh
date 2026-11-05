#!/bin/bash
# Consolidated Transputer Build System
# Usage: ./transputer/build.sh [OPTIONS]
#
# Options:
#   --help                    Show this help
#   --host-tcc                Build native TCC (step 1)
#   --transputer-tcc          Build Transputer cross-compiler (step 2)
#   --temu                    Build TEMU emulator
#   --kernel                  Build Linux kernel (step 3)
#   --xip-kernel              Build kernel with .text in the SD boot bank
#   --initramfs               Build initramfs /init binary (step 3b)
#   --fbwm                    Build TransputerOS framebuffer desktop (step 3c)
#   --tpsh                    Build tpsh (transputer shell) for initramfs (step 3d)
#   --busybox                 Build busybox.flt for initramfs (step 3c)
#   --tpkg                    Build tpkg, tpkg-ui, ttop and tfiles
#   --packages                Rebuild package payloads and all .tpz archives
#   --btl                     Create vmlinux.btl (step 4)
#   --boot [btl] [--debug|--debug-full]    Boot in T4 emulator (step 5)
#   --test                    Boot + validate (step 6)
#   --debug                   Enable full debug tracing (with --boot)
#   --debug-full              Enable enhanced debugging with uninit memory halt
#   --prolog-map [log]        Extract prolog map (step 7)
#   --test-suite              Run c-testsuite (compile+run 220 C tests on T4)
#   --all                     Full build (steps 1-4)
#   --all-boot                Full build + boot test
#   --transputer-os           Build complete TransputerOS + .tpz repository
#   --transputer-os-boot      Build complete TransputerOS, then boot it
#   --clean                   Clean all artifacts (deep clean)
#   --clean-kernel            Deep clean kernel tree only (.o, built-in.a)
#   --verbose                 Show detailed build output
#   --log-dir=DIR             Logs directory (default: logs/)
#
# Examples:
#   ./build.sh --all
#   ./build.sh --kernel
#   ./build.sh --xip-kernel
#   KERNEL_SKIP_DEEP_CLEAN=1 ./build.sh --kernel
#   KERNEL_TREE=linux-7 KERNEL_DEEP_CLEAN=1 ./build.sh --kernel
#   ./build.sh --boot vmlinux.btl
#   ./build.sh --test
#   ./build.sh --verbose --transputer-tcc

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
ELF2FLT_SRC="$SCRIPT_DIR/tools/transputer-elf2flt.c"
LOGS_DIR="${LOGS_DIR:-logs}"
KERNEL_TREE="${KERNEL_TREE:-linux-7}"
if [ -z "${TCC_GCC_VERSION:-}" ]; then
    if [ "$KERNEL_TREE" = "linux-7" ]; then
        TCC_GCC_VERSION="12.2.0"
    else
        TCC_GCC_VERSION="4.4.0"
    fi
fi
export TCC_GCC_VERSION
VERBOSE=0
mkdir -p "$LOGS_DIR"

# OS detection
if [[ "$OSTYPE" == "darwin"* ]]; then
    MAKE_CMD="/usr/local/bin/gmake"
    SED_INPLACE="sed -i.bak"
    TEMU_MAKEFILE="Makefile.macOS"
else
    MAKE_CMD="make"
    SED_INPLACE="sed -i"
    TEMU_MAKEFILE="Makefile.linux"
fi

# GCC detection
find_gcc() {
    if [ -x "$(command -v gcc-14)" ]; then echo "gcc-14"; return; fi
    if [ -x "$(command -v gcc-13)" ]; then echo "gcc-13"; return; fi
    if [ -x "$(command -v gcc-12)" ]; then echo "gcc-12"; return; fi
    echo "gcc"
}

GCC_BIN="$(find_gcc)"
[ "$VERBOSE" = "1" ] && echo "Detected GCC: $GCC_BIN"

log_step() {
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    echo "$1"
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
}

# Helper to run command with output control
run_quiet() {
    if [ "$VERBOSE" = "1" ]; then
        "$@"
    else
        "$@" >/dev/null 2>&1
    fi
}

deep_clean_kernel_tree() {
    local kernel_dir="$1"
    cd "$kernel_dir"
    # NOTE: Do NOT use 'make clean' — it follows CROSS_COMPILE paths and
    # deletes ../../transputer-tcc! Use manual find-based clean instead.
    find . -type f \( -name '*.o' -o -name 'built-in.a' -o -name '*.cmd' -o -name '.*.d' \) -delete 2>/dev/null || true
    rm -f vmlinux vmlinux.o System.map .config
}

patch_config_mak() {
    if [ -f config.mak ]; then
        $SED_INPLACE 's/-fheinous-gnu-extensions//g' config.mak
        $SED_INPLACE 's/-Wno-string-plus-int//g' config.mak
    fi
}

show_usage() {
    sed -n '/^# Usage:/,/# Examples:/p' "$0" | sed '1d;$d'
}

# Parse flags and filter them out
FILTERED_ARGS=()
for arg in "$@"; do
    case "$arg" in
        --verbose) VERBOSE=1 ;;
        *) FILTERED_ARGS+=("$arg") ;;
    esac
done

# Restore filtered args
set -- "${FILTERED_ARGS[@]}"

case "${1:-}" in
    --help|-h) show_usage; exit 0 ;;
    --clean)
        log_step "Cleaning all artifacts..."
        cd "$ROOT_DIR"
        $MAKE_CMD clean || true
        rm -f transputer-tcc transputer-libtcc1.a transputer-netload vmlinux.btl
        rm -rf "$LOGS_DIR"/*
        cd transputer/t4 && $MAKE_CMD -f $TEMU_MAKEFILE clean || true
        echo "Deep cleaning kernel tree..."
        deep_clean_kernel_tree "$ROOT_DIR/linux-kernel/linux-2.6.39"
        echo "Clean complete."
        exit 0
        ;;
    --clean-kernel)
        log_step "Deep cleaning kernel tree only..."
        echo "Removing stale kernel build artifacts..."
        deep_clean_kernel_tree "$ROOT_DIR/transputer/$KERNEL_TREE"
        echo "Kernel clean complete."
        exit 0
        ;;
esac

cd "$ROOT_DIR"

# Step 1: Host TCC
do_host_tcc() {
    log_step "Step 1: Building Host TCC"
    $MAKE_CMD GITHASH=no GITMODF= clean || true
    rm -f config.mak
    if [ "$VERBOSE" = "1" ]; then
        CC="$GCC_BIN" ./configure
    else
        CC="$GCC_BIN" ./configure >/dev/null
    fi
    patch_config_mak
    $MAKE_CMD GITHASH=no GITMODF= CC="$GCC_BIN" &> "$LOGS_DIR/01-host-tcc.log"
    echo "✓ Host TCC: $ROOT_DIR/tcc"
}

# Step 2: Transputer TCC
do_transputer_tcc() {
    log_step "Step 2: Building Transputer Cross-Compiler"
    if [ ! -x "./tcc" ]; then echo "Run --host-tcc first"; exit 1; fi
    rm -f transputer-tcc transputer-libtcc1.a transputer-netload
    if [ "$VERBOSE" = "1" ]; then
        ./configure --cpu=transputer
    else
        ./configure --cpu=transputer >/dev/null
    fi
    patch_config_mak
    # Build transputer cross-compiler with GCC (TCC self-hosting has linker issues).
    # The kernel build itself uses TCC exclusively via tcc-wrapper.sh (no GCC).
    $MAKE_CMD GITHASH=no GITMODF= CC="$GCC_BIN" transputer-tcc transputer-libtcc1.a transputer-netload &> "$LOGS_DIR/02-transputer-tcc.log"
    echo "✓ Transputer toolchain built"
}

# Step 3: TEMU
do_temu() {
    log_step "Step 3: Building TEMU Emulator"
    cd transputer/t4
    $MAKE_CMD -f $TEMU_MAKEFILE clean
    $MAKE_CMD -f $TEMU_MAKEFILE &> "$ROOT_DIR/$LOGS_DIR/03-temu.log"
    cd "$ROOT_DIR"
    echo "✓ TEMU: transputer/t4/t4"
}

# Step 4: Kernel
do_kernel() {
    log_step "Step 4: Building Linux Kernel"
    if [ ! -x "./transputer-tcc" ]; then echo "Run --transputer-tcc first"; exit 1; fi

    local KERNEL_DIR="$SCRIPT_DIR/$KERNEL_TREE"
    local kernel_jobs="${KERNEL_JOBS:-1}"
    local kernel_v="${KERNEL_VERBOSE:-$VERBOSE}"
    local hostcc_default="/usr/bin/clang -I/usr/local/include"
    local hostcc_bin="${HOSTCC:-$hostcc_default}"
    local offsets_cc="${TCC_OFFSETS_CC:-${hostcc_bin%% *}}"
    local hostld_bin="${HOSTLD:-/usr/bin/ld}"
    local hostas_bin="${HOSTAS:-/usr/bin/as}"
    local hostar_bin="${HOSTAR:-/usr/bin/ar}"

    # Do not silently repack stale userspace binaries.  Kbuild only sees the
    # generated CPIO, so refresh source-built rootfs programs before staging.
    if [ ! -f "$SCRIPT_DIR/initramfs/init" ] ||
       [ "$SCRIPT_DIR/initramfs/init.c" -nt "$SCRIPT_DIR/initramfs/init" ] ||
       [ "$SCRIPT_DIR/initramfs/crt1.S" -nt "$SCRIPT_DIR/initramfs/init" ]; then
        do_initramfs
    fi
    if [ ! -f "$SCRIPT_DIR/initramfs/fbwm.flt" ] ||
       [ "$SCRIPT_DIR/fbwm/fbwm.c" -nt "$SCRIPT_DIR/initramfs/fbwm.flt" ] ||
       [ "$SCRIPT_DIR/fbwm/fbwm_main.c" -nt "$SCRIPT_DIR/initramfs/fbwm.flt" ] ||
       [ "$SCRIPT_DIR/initramfs/crt1.S" -nt "$SCRIPT_DIR/initramfs/fbwm.flt" ] ||
       [ "$SCRIPT_DIR/fbwm/build-fbwm.sh" -nt "$SCRIPT_DIR/initramfs/fbwm.flt" ]; then
        do_fbwm
    fi
    if [ ! -f "$SCRIPT_DIR/initramfs/tpsh.flt" ] ||
       [ "$SCRIPT_DIR/initramfs/tpsh.c" -nt "$SCRIPT_DIR/initramfs/tpsh.flt" ] ||
       [ "$SCRIPT_DIR/initramfs/crt1.S" -nt "$SCRIPT_DIR/initramfs/tpsh.flt" ]; then
        do_tpsh
    fi

    cd "$KERNEL_DIR"

    if [ "${KERNEL_DEEP_CLEAN:-0}" = "1" ]; then
        echo "Kernel build mode: deep clean (KERNEL_DEEP_CLEAN=1)"
        # Deep clean - remove ALL kernel build artifacts.
        # NOTE: Do NOT use 'make clean' here — Kbuild's clean target follows
        # CROSS_COMPILE paths and deletes ../../transputer-tcc!
        # Instead, manually delete known build artifacts.
        find arch/ block/ crypto/ drivers/ firmware/ fs/ init/ ipc/ kernel/ \
             lib/ mm/ net/ security/ sound/ usr/ virt/ \
             -name '*.o' -type f -delete 2>/dev/null || true
        find arch/ block/ crypto/ drivers/ firmware/ fs/ init/ ipc/ kernel/ \
             lib/ mm/ net/ security/ sound/ usr/ virt/ \
             -name 'built-in.o' -type f -delete 2>/dev/null || true
        find arch/ block/ crypto/ drivers/ firmware/ fs/ init/ ipc/ kernel/ \
             lib/ mm/ net/ security/ sound/ usr/ virt/ \
             -name 'built-in.a' -type f -delete 2>/dev/null || true
        find arch/ block/ crypto/ drivers/ firmware/ fs/ init/ ipc/ kernel/ \
             lib/ mm/ net/ security/ sound/ usr/ virt/ \
             -name '*.o.cmd' -type f -delete 2>/dev/null || true
        rm -f .config vmlinux vmlinux.btl vmlinux.o System.map
        rm -f scripts/mod/empty.o scripts/mod/elfconfig.h scripts/mod/modpost
        rm -f .version include/generated/compile.h
    fi

    if [ ! -f .config ] || [ "${KERNEL_DEEP_CLEAN:-0}" = "1" ]; then
        # Generate .config from arch/transputer/configs/tpos_defconfig for TransputerOS.
        # This replaces the old allnoconfig + 130 scripts/config overrides.
        # tpos_defconfig includes TransputerOS settings.
        env MAKE=$MAKE_CMD $MAKE_CMD ARCH=transputer \
            TCC_OFFSETS_CC="$offsets_cc" \
            CC="$SCRIPT_DIR/tcc-wrapper.sh" \
            LD="$SCRIPT_DIR/tcc-ld-wrapper.sh" \
            HOSTCC="$hostcc_bin" \
            tpos_defconfig

        # olddefconfig/silentoldconfig generates include/config/auto.conf and
        # include/generated/autoconf.h needed by the build.
        local config_update_target="silentoldconfig"
        if grep -qE '(^|[[:space:]])olddefconfig([[:space:]]|$)' scripts/kconfig/Makefile; then
            config_update_target="olddefconfig"
        fi
        yes '' | env MAKE=$MAKE_CMD $MAKE_CMD ARCH=transputer \
            TCC_OFFSETS_CC="$offsets_cc" \
            CC="$SCRIPT_DIR/tcc-wrapper.sh" \
            LD="$SCRIPT_DIR/tcc-ld-wrapper.sh" \
            HOSTCC="$hostcc_bin" \
            "$config_update_target" 2>/dev/null || true
    fi

    # Rebuild initramfs CPIO — use TransputerOS rootfs
    local ROOTFS_STAGE="$SCRIPT_DIR/rootfs/stage.sh"
    local ROOTFS_OUT="$SCRIPT_DIR/rootfs/output"
    if [ -x "$ROOTFS_STAGE" ]; then
        echo "Rebuilding TransputerOS rootfs..."
        bash "$ROOTFS_STAGE" 2>&1 | tail -3
        echo "Packing CPIO..."
        mkdir -p "$ROOTFS_OUT"
        # Check first: the redirect below empties rootfs.cpio before a
        # missing cpio fails, and 2>/dev/null hides why the build stopped.
        if ! command -v cpio >/dev/null 2>&1; then
            echo "ERROR: cpio is not installed (sudo apt install cpio)" >&2
            return 1
        fi
        (cd "$ROOTFS_OUT/stage" && find . -print | cpio -o -H newc 2>/dev/null) > "$ROOTFS_OUT/rootfs.cpio"
        echo "  rootfs.cpio: $(du -h "$ROOTFS_OUT/rootfs.cpio" | cut -f1)"
    fi

    # Always remove vmlinux to force relink (Kbuild may not detect .o changes)
    rm -f vmlinux

    # Build kernel with TCC wrappers (all transputer/*.sh wrappers required)
    if [ "$kernel_v" = "1" ]; then
        env MAKE=$MAKE_CMD PATH="$ROOT_DIR:$SCRIPT_DIR:/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin" \
            TCC_OFFSETS_CC="$offsets_cc" \
            $MAKE_CMD ARCH=transputer \
            CC="$SCRIPT_DIR/tcc-wrapper.sh" \
            LD="$SCRIPT_DIR/tcc-ld-wrapper.sh" \
            AS="$ROOT_DIR/transputer-tcc" \
            AR="$SCRIPT_DIR/tcc-ar-wrapper.sh" \
            NM="$SCRIPT_DIR/tcc-nm-wrapper.sh" \
            OBJCOPY="$SCRIPT_DIR/tcc-objcopy-wrapper.sh" \
            KBUILD_ARFLAGS="cDPrS" \
            HOSTCC="$hostcc_bin" \
            HOSTLD="$hostld_bin" \
            HOSTAS="$hostas_bin" \
            HOSTAR="$hostar_bin" \
            CONFIG_CC_VERSION_TEXT="tcc transputer" \
            KCFLAGS="-nostdlib -nostdinc -fstack-check -I$ROOT_DIR ${EXTRA_KCFLAGS:-}" \
            V=1 \
            -j"$kernel_jobs" vmlinux 2>&1 | tee "$ROOT_DIR/$LOGS_DIR/04-kernel.log"
    else
        env MAKE=$MAKE_CMD PATH="$ROOT_DIR:$SCRIPT_DIR:/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin" \
            TCC_OFFSETS_CC="$offsets_cc" \
            $MAKE_CMD ARCH=transputer \
            CC="$SCRIPT_DIR/tcc-wrapper.sh" \
            LD="$SCRIPT_DIR/tcc-ld-wrapper.sh" \
            AS="$ROOT_DIR/transputer-tcc" \
            AR="$SCRIPT_DIR/tcc-ar-wrapper.sh" \
            NM="$SCRIPT_DIR/tcc-nm-wrapper.sh" \
            OBJCOPY="$SCRIPT_DIR/tcc-objcopy-wrapper.sh" \
            KBUILD_ARFLAGS="cDPrS" \
            HOSTCC="$hostcc_bin" \
            HOSTLD="$hostld_bin" \
            HOSTAS="$hostas_bin" \
            HOSTAR="$hostar_bin" \
            CONFIG_CC_VERSION_TEXT="tcc transputer" \
            KCFLAGS="-nostdlib -nostdinc -fstack-check -I$ROOT_DIR ${EXTRA_KCFLAGS:-}" \
            V=0 \
            -j"$kernel_jobs" vmlinux > "$ROOT_DIR/$LOGS_DIR/04-kernel.log" 2>&1
    fi

    cd "$ROOT_DIR"

    if [ -f "$KERNEL_DIR/vmlinux" ]; then
        echo "Kernel: $KERNEL_DIR/vmlinux"
        ls -lh "$KERNEL_DIR/vmlinux"
        return 0
    else
        echo "Kernel build failed (check $LOGS_DIR/04-kernel.log)"
        return 1
    fi
}

# Step 5: BTL
do_btl() {
    log_step "Step 5: Creating BTL Image"

    local kernel_vmlinux="$SCRIPT_DIR/$KERNEL_TREE/vmlinux"
    local legacy_kernel_vmlinux="$ROOT_DIR/linux-kernel/linux-2.6.39/vmlinux"

    # Find vmlinux or create fallback test program
    if [ -f "$kernel_vmlinux" ]; then
        echo "Converting kernel to BTL..."
        ./transputer-netload "$kernel_vmlinux" vmlinux.btl &> "$LOGS_DIR/05-btl.log"
        echo "✓ vmlinux.btl created"
        ls -lh vmlinux.btl
    elif [ -f "$legacy_kernel_vmlinux" ]; then
        echo "Converting kernel to BTL (legacy path)..."
        ./transputer-netload "$legacy_kernel_vmlinux" vmlinux.btl &> "$LOGS_DIR/05-btl.log"
        echo "✓ vmlinux.btl created"
        ls -lh vmlinux.btl
    else
        echo "⚠ vmlinux not found - creating simple test program instead..."
        cat > test_program.c << 'EOF'
/* Simple Transputer test program */
void _start(void) {
    /* Initialize and halt - minimal boot test */
    while(1) {
        /* Infinite loop - visible in emulator */
    }
}
EOF
        echo "Compiling test program..."
        ./transputer-tcc -static -nostdlib -o test_program.elf test_program.c

        if [ -f test_program.elf ]; then
            echo "Converting test program to BTL..."
            ./transputer-netload test_program.elf test_program.btl
            echo "✓ test_program.btl created"
            ls -lh test_program.btl
        fi
    fi
}

# Pass at most BOOT_LOG_MAX bytes (default 256 MB) of emulator output through,
# then note how much more there was and discard it.  Draining the rest keeps
# t4 running to its timeout instead of dying on a closed pipe.  A guest stuck
# on a bad AJW once made t4 print one line per instruction: a 24 GB boot log
# that filled the disk under WSL.
cap_boot_log() {
    local max="${BOOT_LOG_MAX:-268435456}" dropped
    head -c "$max"
    dropped=$(wc -c)
    if [ "$dropped" -gt 0 ]; then
        printf '\n[boot log truncated at %s bytes; at least %s more discarded]\n' \
            "$max" "$dropped"
    fi
}

# Step 6: Boot
do_boot() {
    local btl="vmlinux.btl"
    local debug_flag=""
    local trace_level="1"
    local boot_timeout="${BOOT_TIMEOUT:-1200}"
    local default_mem_bits="28"
    if [ "$KERNEL_TREE" = "linux-7" ]; then
        default_mem_bits="25"
    fi
    local t4_mem_bits="${T4_MEM_BITS:-$default_mem_bits}"
    local tap_flag=""
    local tap_dev="${T4_TAP_DEV:-tap0}"

    # -k: t4 exits on SIGTERM only between instructions, so a guest stuck
    # inside one would otherwise outlive the timeout.
    run_t4() {
        if [[ "$boot_timeout" =~ ^[0-9]+$ ]] && [[ "$boot_timeout" -gt 0 ]]; then
            timeout -k 30 "$boot_timeout" "$@"
        else
            "$@"
        fi
    }

    while [[ $# -gt 0 ]]; do
        case "$1" in
            --debug)
                debug_flag="--debug"
                trace_level="15"
                shift
                ;;
            --debug-full)
                debug_flag="--debug-full"
                trace_level="15"
                shift
                ;;
            --tap)
                tap_flag="yes"
                if [[ -n "${2:-}" && ! "$2" =~ ^-- ]]; then
                    tap_dev="$2"
                    shift
                fi
                shift
                ;;
            --boot)
                shift  # Skip the --boot argument
                ;;
            *)
                btl="$1"
                shift
                ;;
        esac
    done

    if [[ ! -f "$btl" ]]; then
        echo "ERROR: Boot image '$btl' not found"
        if [[ "$btl" == "vmlinux.btl" ]]; then
            echo "Hint: build kernel + BTL first:"
            echo "  bash transputer/build.sh --kernel"
            echo "  bash transputer/build.sh --btl"
        else
            echo "Hint: pass an existing .btl path to --boot"
        fi
        return 1
    fi

    # TAP network setup
    local tap_args=""
    if [[ -n "$tap_flag" ]]; then
        echo "Setting up TAP networking (device: $tap_dev)..."

        # Create TAP device if it doesn't exist
        if ! ip link show "$tap_dev" &>/dev/null; then
            echo "  Creating $tap_dev..."
            sudo ip tuntap add dev "$tap_dev" mode tap user "$(whoami)" || {
                echo "⚠ Failed to create $tap_dev (need sudo?)"
                echo "  Manual setup: sudo ip tuntap add dev $tap_dev mode tap user \$USER"
                return 1
            }
        fi

        # Configure TAP if not already configured
        if ! ip addr show "$tap_dev" 2>/dev/null | grep -q "10.0.2.1"; then
            echo "  Configuring $tap_dev: 10.0.2.1/24..."
            sudo ip link set "$tap_dev" up
            sudo ip addr add 10.0.2.1/24 dev "$tap_dev" 2>/dev/null || true
        fi

        # Enable IP forwarding and NAT for internet access
        if [[ "$(cat /proc/sys/net/ipv4/ip_forward 2>/dev/null)" != "1" ]]; then
            echo "  Enabling IP forwarding..."
            sudo sysctl -qw net.ipv4.ip_forward=1
        fi

        # Find default outbound interface for NAT
        local wan_iface
        wan_iface=$(ip route show default 2>/dev/null | awk '{print $5; exit}')
        if [[ -n "$wan_iface" ]]; then
            # Add MASQUERADE rule if not already present
            if ! sudo iptables -t nat -C POSTROUTING -s 10.0.2.0/24 -o "$wan_iface" -j MASQUERADE 2>/dev/null; then
                echo "  Adding NAT rule ($tap_dev → $wan_iface)..."
                sudo iptables -t nat -A POSTROUTING -s 10.0.2.0/24 -o "$wan_iface" -j MASQUERADE
            fi
        fi

        tap_args="--tap $tap_dev"
        echo "  TAP ready: guest=10.0.2.15 ↔ host=10.0.2.1 (NAT via ${wan_iface:-?})"
    fi

    # Avoid stale UART bytes being mistaken for current boot output
    rm -f boot.uart

    # P0b: default headless boot (no SDL window — survives no DISPLAY / WSLg)
    # Override: T4_HEADLESS=0 DISPLAY=:0 bash transputer/build.sh --boot
    export T4_HEADLESS="${T4_HEADLESS:-1}"
    # Fixed simplefb has no FLIP register: present dirty RAM periodically.
    export T4_PRESENT_ON_FLIP="${T4_PRESENT_ON_FLIP:-0}"
    # Self-test is off by default in kernel; request full suite with:
    #   EXTRA_CMDLINE=t4_selftest bash ...  (when __setup works) or rebuild

    log_step "Step 6: Booting $btl in T4"
    ls -lh "$btl"
    echo "T4_HEADLESS=$T4_HEADLESS T4_PRESENT_ON_FLIP=$T4_PRESENT_ON_FLIP"
    if [[ "$boot_timeout" =~ ^[0-9]+$ ]] && [[ "$boot_timeout" -gt 0 ]]; then
        echo "Boot timeout enabled: ${boot_timeout}s"
    fi
    if [[ -n "$tap_flag" ]]; then
        echo "Network: TAP=$tap_dev (10.0.2.1 ↔ 10.0.2.15)"
    fi
    local boot_rc=0
    if [[ "$debug_flag" == "--debug-full" ]]; then
        echo "Full debug mode enabled (with uninitialized memory halt)"
        run_t4 ./transputer/t4/t4 -s806 -sm "$t4_mem_bits" -ss $tap_args --debug -sg -sb "$btl" 2>&1 | cap_boot_log | tee "$LOGS_DIR/06-boot.log" debug.txt
        boot_rc=${PIPESTATUS[0]}
    elif [[ -n "$debug_flag" ]]; then
        echo "Debug mode enabled (trace level $trace_level)"
        run_t4 ./transputer/t4/t4 -s806 -sm "$t4_mem_bits" -ss $tap_args -sx "$trace_level" -sb "$btl" 2>&1 | cap_boot_log | tee "$LOGS_DIR/06-boot.log" debug.txt
        boot_rc=${PIPESTATUS[0]}
    else
        run_t4 ./transputer/t4/t4 -s806 -sm "$t4_mem_bits" -ss $tap_args -sb "$btl" 2>&1 | cap_boot_log > "$LOGS_DIR/06-boot.log"
        boot_rc=${PIPESTATUS[0]}
    fi

    # Success if guest reached DESKTOP_READY even when timeout kills T4.
    if grep -q "DESKTOP_READY" "$LOGS_DIR/06-boot.log" 2>/dev/null; then
        echo "✓ Boot success: DESKTOP_READY"
        if grep -q "echo-test-ret=00000000" "$LOGS_DIR/06-boot.log" \
            && grep -q "uname-test-ret=00000000" "$LOGS_DIR/06-boot.log" \
            && grep -q "ls-test-ret=00000000" "$LOGS_DIR/06-boot.log"; then
            echo "✓ Multi-applet smoke: echo/uname/ls OK"
        else
            echo "⚠ Multi-applet smoke markers incomplete (see $LOGS_DIR/06-boot.log)"
        fi
        if grep -q "lsbin-test-ret=00000000" "$LOGS_DIR/06-boot.log" 2>/dev/null; then
            echo "✓ ls /bin OK"
        fi
        if grep -qE "^(bin|etc|usr)$" "$LOGS_DIR/06-boot.log" 2>/dev/null \
            || grep -q "busybox" "$LOGS_DIR/06-boot.log" 2>/dev/null; then
            echo "✓ Directory listing content seen in UART"
        fi
        return 0
    fi
    echo "Boot finished with rc=$boot_rc (no DESKTOP_READY)"
    return "$boot_rc"
}

# Step 7: Prolog map
do_prolog_map() {
    local log="$2"
    [[ -z "$log" ]] && log="kernel-build.log"
    log_step "Step 7: Extracting Prolog Map"
    rg -n "^PROLOG: func=" "$ROOT_DIR/$log" > prolog-map.txt
    echo "✓ prolog-map.txt"
}

# Step 4b: Initramfs /init binary
do_initramfs() {
    log_step "Step 4b: Building initramfs /init"
    if [ ! -x "$ROOT_DIR/transputer-tcc" ]; then echo "Run --transputer-tcc first"; exit 1; fi

    local INITRAMFS_DIR="$SCRIPT_DIR/initramfs"
    local ELF2FLT="$ROOT_DIR/transputer-elf2flt"
    mkdir -p "$INITRAMFS_DIR"

    # Build elf2flt if needed
    if [ ! -x "$ELF2FLT" ] || [ "$ELF2FLT_SRC" -nt "$ELF2FLT" ]; then
        echo "Building transputer-elf2flt..."
        cc -Wall -o "$ELF2FLT" "$ELF2FLT_SRC" || {
            echo "⚠ elf2flt build failed"; return 1
        }
    fi

    if [ -f "$INITRAMFS_DIR/init.c" ]; then
        echo "Compiling initramfs /init..."

        # Step 1: Compile to ELF
        local init_sources=()

        if [ -f "$INITRAMFS_DIR/crt1.S" ]; then
            init_sources+=("$INITRAMFS_DIR/crt1.S")
        fi
        # syscall_shim.S contains old bare-metal probes with direct kernel
        # symbol references. PID 1 uses the published syscall gate instead,
        # so linking those unused probes would leave invalid cross-image
        # relocations in a strict userspace build.
        if [ -f "$SCRIPT_DIR/tests/syscall_stub.c" ]; then
            init_sources+=("$SCRIPT_DIR/tests/syscall_stub.c")
        fi
        init_sources+=("$INITRAMFS_DIR/init.c")

        env T4_STRICT_UNRESOLVED_CALLS=1 \
        "$ROOT_DIR/transputer-tcc" -nostdlib -nostdinc \
            -o "$INITRAMFS_DIR/init.elf" \
            "${init_sources[@]}" \
            "$ROOT_DIR/transputer-libtcc1.a" \
            &> "$LOGS_DIR/04b-initramfs.log"

        if [ ! -f "$INITRAMFS_DIR/init.elf" ]; then
            echo "⚠ init.elf build failed (check $LOGS_DIR/04b-initramfs.log)"
            return 1
        fi

        # Step 2: Convert ELF to bFLT
        echo "Converting init.elf to bFLT..."
        "$ELF2FLT" -k "$INITRAMFS_DIR/init.elf" "$INITRAMFS_DIR/init" \
            >> "$LOGS_DIR/04b-initramfs.log" 2>&1

        if [ -f "$INITRAMFS_DIR/init" ]; then
            echo "✓ initramfs /init built (bFLT): $INITRAMFS_DIR/init"
            ls -lh "$INITRAMFS_DIR/init"
        else
            echo "⚠ elf2flt conversion failed (check $LOGS_DIR/04b-initramfs.log)"
            return 1
        fi
    else
        echo "⚠ No init.c found in $INITRAMFS_DIR — skipping"
    fi
}

do_shprobe() {
    log_step "Step 4d: Building /bin/sh probe"
    if [ ! -x "$ROOT_DIR/transputer-tcc" ]; then echo "Run --transputer-tcc first"; exit 1; fi

    local INITRAMFS_DIR="$SCRIPT_DIR/initramfs"
    local ELF2FLT="$ROOT_DIR/transputer-elf2flt"
    local UCLIBC="$SCRIPT_DIR/uClibc-ng"
    local SHPROBE_SRC="$INITRAMFS_DIR/shprobe.c"

    if [ ! -f "$SHPROBE_SRC" ]; then
        echo "Missing $SHPROBE_SRC"
        return 1
    fi

    if [ ! -x "$ELF2FLT" ] || [ "$ELF2FLT_SRC" -nt "$ELF2FLT" ]; then
        echo "Building transputer-elf2flt..."
        cc -Wall -o "$ELF2FLT" "$ELF2FLT_SRC" || {
            echo "⚠ elf2flt build failed"; return 1
        }
    fi

    "$ROOT_DIR/transputer-tcc" -nostdlib -nostdinc \
        -c -o /tmp/shprobe_crt1.o "$INITRAMFS_DIR/crt1.S" \
        &> "$LOGS_DIR/04d-shprobe.log" || {
        echo "⚠ crt1.S compile failed"; return 1
    }
    "$ROOT_DIR/transputer-tcc" -nostdlib -nostdinc -I"$ROOT_DIR/include/transputer" -I"$ROOT_DIR/include" -I"$UCLIBC/include" \
        -c -o /tmp/shprobe_bridge.o "$INITRAMFS_DIR/crt_bridge.c" \
        >> "$LOGS_DIR/04d-shprobe.log" 2>&1 || {
        echo "⚠ crt_bridge.c compile failed"; return 1
    }
    "$ROOT_DIR/transputer-tcc" -nostdlib -nostdinc \
        -c -o /tmp/shprobe_syscall_stub.o "$SCRIPT_DIR/tests/syscall_stub.c" \
        >> "$LOGS_DIR/04d-shprobe.log" 2>&1 || {
        echo "⚠ syscall_stub.c compile failed"; return 1
    }
    "$ROOT_DIR/transputer-tcc" -nostdlib -nostdinc \
        -o "$INITRAMFS_DIR/shprobe.elf" \
        /tmp/shprobe_crt1.o /tmp/shprobe_bridge.o \
        /tmp/shprobe_syscall_stub.o \
        "$SHPROBE_SRC" \
        "$UCLIBC/lib/libc.a" \
        >> "$LOGS_DIR/04d-shprobe.log" 2>&1 || {
        echo "⚠ shprobe link failed"; return 1
    }

    "$ELF2FLT" -k "$INITRAMFS_DIR/shprobe.elf" "$INITRAMFS_DIR/shprobe" \
        >> "$LOGS_DIR/04d-shprobe.log" 2>&1 || {
        echo "⚠ shprobe elf2flt failed"; return 1
    }

    echo "✓ shprobe built: $INITRAMFS_DIR/shprobe"
    ls -lh "$INITRAMFS_DIR/shprobe"
}

do_directprobe() {
    log_step "Step 4e: Building direct crt1/main probe"
    if [ ! -x "$ROOT_DIR/transputer-tcc" ]; then echo "Run --transputer-tcc first"; exit 1; fi

    local INITRAMFS_DIR="$SCRIPT_DIR/initramfs"
    local ELF2FLT="$ROOT_DIR/transputer-elf2flt"

    if [ ! -x "$ELF2FLT" ] || [ "$ELF2FLT_SRC" -nt "$ELF2FLT" ]; then
        echo "Building transputer-elf2flt..."
        cc -Wall -o "$ELF2FLT" "$ELF2FLT_SRC" || {
            echo "⚠ elf2flt build failed"; return 1
        }
    fi

    "$ROOT_DIR/transputer-tcc" -nostdlib -nostdinc \
        -o "$INITRAMFS_DIR/directprobe.elf" \
        "$INITRAMFS_DIR/crt1.S" \
        "$INITRAMFS_DIR/crt_bridge_direct.c" \
        "$SCRIPT_DIR/tests/syscall_stub.c" \
        "$INITRAMFS_DIR/directprobe.c" \
        &> "$LOGS_DIR/04e-directprobe.log" || {
        echo "⚠ directprobe link failed"; return 1
    }

    "$ELF2FLT" -k "$INITRAMFS_DIR/directprobe.elf" "$INITRAMFS_DIR/directprobe" \
        >> "$LOGS_DIR/04e-directprobe.log" 2>&1 || {
        echo "⚠ directprobe elf2flt failed"; return 1
    }

    echo "✓ directprobe built: $INITRAMFS_DIR/directprobe"
    ls -lh "$INITRAMFS_DIR/directprobe"
}

do_uclibcprobe() {
    log_step "Step 4f: Building minimal uClibc probe"
    if [ ! -x "$ROOT_DIR/transputer-tcc" ]; then echo "Run --transputer-tcc first"; exit 1; fi

    local INITRAMFS_DIR="$SCRIPT_DIR/initramfs"
    local ELF2FLT="$ROOT_DIR/transputer-elf2flt"
    local UCLIBC="$SCRIPT_DIR/uClibc-ng"

    if [ ! -x "$ELF2FLT" ] || [ "$ELF2FLT_SRC" -nt "$ELF2FLT" ]; then
        echo "Building transputer-elf2flt..."
        cc -Wall -o "$ELF2FLT" "$ELF2FLT_SRC" || {
            echo "⚠ elf2flt build failed"; return 1
        }
    fi

    "$ROOT_DIR/transputer-tcc" -nostdlib -nostdinc \
        -c -o /tmp/uclibcprobe_crt1.o "$INITRAMFS_DIR/crt1.S" \
        &> "$LOGS_DIR/04f-uclibcprobe.log" || {
        echo "⚠ crt1.S compile failed"; return 1
    }
    "$ROOT_DIR/transputer-tcc" -nostdlib -nostdinc -I"$ROOT_DIR/include/transputer" -I"$ROOT_DIR/include" -I"$UCLIBC/include" \
        -c -o /tmp/uclibcprobe_bridge.o "$INITRAMFS_DIR/crt_bridge.c" \
        >> "$LOGS_DIR/04f-uclibcprobe.log" 2>&1 || {
        echo "⚠ crt_bridge.c compile failed"; return 1
    }
    "$ROOT_DIR/transputer-tcc" -nostdlib -nostdinc \
        -c -o /tmp/uclibcprobe_syscall_stub.o "$SCRIPT_DIR/tests/syscall_stub.c" \
        >> "$LOGS_DIR/04f-uclibcprobe.log" 2>&1 || {
        echo "⚠ syscall_stub.c compile failed"; return 1
    }
    "$ROOT_DIR/transputer-tcc" -nostdlib -nostdinc \
        -o "$INITRAMFS_DIR/uclibcprobe.elf" \
        /tmp/uclibcprobe_crt1.o /tmp/uclibcprobe_bridge.o \
        /tmp/uclibcprobe_syscall_stub.o \
        "$INITRAMFS_DIR/uclibcprobe.c" \
        "$UCLIBC/lib/libc.a" \
        >> "$LOGS_DIR/04f-uclibcprobe.log" 2>&1 || {
        echo "⚠ uclibcprobe link failed"; return 1
    }

    "$ELF2FLT" -k "$INITRAMFS_DIR/uclibcprobe.elf" "$INITRAMFS_DIR/uclibcprobe" \
        >> "$LOGS_DIR/04f-uclibcprobe.log" 2>&1 || {
        echo "⚠ uclibcprobe elf2flt failed"; return 1
    }

    echo "✓ uclibcprobe built: $INITRAMFS_DIR/uclibcprobe"
    ls -lh "$INITRAMFS_DIR/uclibcprobe"
}

# Summary helper
show_summary() {
    echo ""
    log_step "Build Complete!"
    echo ""
    echo "Available BTL boot images:"
    for btl in "$ROOT_DIR"/*.btl; do
        if [ -f "$btl" ]; then
            ls -lh "$btl"
        fi
    done
    echo ""

    if [ -f "$ROOT_DIR/vmlinux.btl" ]; then
        echo "To boot the kernel in T4 emulator, run:"
        echo "  ./transputer/t4/t4 -s806 -sm 28 -ss -sx 1 -sb vmlinux.btl"
    elif [ -f "$ROOT_DIR/test_program.btl" ]; then
        echo "To boot the test program in T4 emulator, run:"
        echo "  ./transputer/t4/t4 -s806 -sm 28 -ss -sx 1 -sb test_program.btl"
    fi
    echo ""
}

do_test_suite() {
    local TCC="$ROOT_DIR/transputer-tcc"
    local TESTSUITE_DIR="$SCRIPT_DIR/c-testsuite"

    if [ ! -x "$TCC" ]; then
        echo "ERROR: transputer-tcc not found. Run --transputer-tcc first."
        return 1
    fi

    log_step "Building test runtime"
    "$TCC" -nostdlib -nostdinc -c -o "$TESTSUITE_DIR/test_boot_main.o" "$TESTSUITE_DIR/test_boot_main.S"
    "$TCC" -nostdlib -nostdinc -I"$TESTSUITE_DIR/include" -c -o "$TESTSUITE_DIR/test_runtime.o" "$TESTSUITE_DIR/test_runtime.c"

    log_step "Running c-testsuite (220 tests)"
    cd "$TESTSUITE_DIR"
    ./single-exec tcc-transputer
}

do_tpsh() {
    log_step "Step 4d: Building tpsh (transputer shell) for initramfs"
    if [ ! -x "$ROOT_DIR/transputer-tcc" ]; then echo "Run --transputer-tcc first"; exit 1; fi

    local TCC="$ROOT_DIR/transputer-tcc"
    local ELF2FLT="$ROOT_DIR/transputer-elf2flt"
    local INITRAMFS_DIR="$SCRIPT_DIR/initramfs"
    local UCLIBC="$SCRIPT_DIR/uClibc-ng"
    local TPSH_SRC="$INITRAMFS_DIR/tpsh.c"

    if [ ! -f "$TPSH_SRC" ]; then
        echo "Missing $TPSH_SRC"
        return 1
    fi

    # Build elf2flt if needed
    if [ ! -x "$ELF2FLT" ] || [ "$ELF2FLT_SRC" -nt "$ELF2FLT" ]; then
        echo "Building transputer-elf2flt..."
        cc -Wall -o "$ELF2FLT" "$ELF2FLT_SRC" || {
            echo "⚠ elf2flt build failed"; return 1
        }
    fi

    mkdir -p "$LOGS_DIR"

    # Step 1: Compile kernel-mode CRT
    echo "Compiling CRT for tpsh..."
    "$TCC" -nostdlib -nostdinc -c -o /tmp/tpsh_crt1.o "$INITRAMFS_DIR/crt1.S" \
        &> "$LOGS_DIR/04d-tpsh.log" || {
        echo "⚠ crt1.S compile failed"; return 1
    }
    "$TCC" -nostdlib -nostdinc -I"$ROOT_DIR/include/transputer" -I"$ROOT_DIR/include" -I"$UCLIBC/include" \
        -c -o /tmp/tpsh_crt_bridge.o "$INITRAMFS_DIR/crt_bridge.c" \
        >> "$LOGS_DIR/04d-tpsh.log" 2>&1 || {
        echo "⚠ crt_bridge.c compile failed"; return 1
    }

    # Step 2: Compile syscall stub
    echo "Compiling syscall stub for tpsh..."
    "$TCC" -nostdlib -nostdinc -c -o /tmp/tpsh_syscall_stub.o "$SCRIPT_DIR/tests/syscall_stub.c" \
        >> "$LOGS_DIR/04d-tpsh.log" 2>&1 || {
        echo "⚠ syscall_stub.c compile failed"; return 1
    }

    # Step 3: Compile tpsh.c
    echo "Compiling tpsh.c..."
    "$TCC" -mt9000 -w -nostdlib -nostdinc \
        -I"$ROOT_DIR/include/transputer" -I"$ROOT_DIR/include" \
        -I"$UCLIBC/include" \
        -c -o /tmp/tpsh.o "$TPSH_SRC" \
        >> "$LOGS_DIR/04d-tpsh.log" 2>&1 || {
        echo "⚠ tpsh.c compile failed (check $LOGS_DIR/04d-tpsh.log)"; return 1
    }

    # Step 4: Link tpsh ELF
    echo "Linking tpsh..."
    env T4_STRICT_UNRESOLVED_CALLS=1 \
    "$TCC" -nostdlib -nostdinc \
        -o /tmp/tpsh.elf \
        /tmp/tpsh_crt1.o /tmp/tpsh_crt_bridge.o \
        /tmp/tpsh.o \
        /tmp/tpsh_syscall_stub.o \
        "$UCLIBC/lib/libc.a" \
        "$ROOT_DIR/transputer-libtcc1.a" \
        >> "$LOGS_DIR/04d-tpsh.log" 2>&1

    if [ ! -f /tmp/tpsh.elf ]; then
        echo "⚠ tpsh link failed (check $LOGS_DIR/04d-tpsh.log)"
        return 1
    fi

    # Step 5: Convert ELF to bFLT
    echo "Converting tpsh to bFLT..."
    "$ELF2FLT" /tmp/tpsh.elf "$INITRAMFS_DIR/tpsh.flt" \
        >> "$LOGS_DIR/04d-tpsh.log" 2>&1

    if [ -f "$INITRAMFS_DIR/tpsh.flt" ]; then
        echo "✓ tpsh.flt built: $INITRAMFS_DIR/tpsh.flt"
        ls -lh "$INITRAMFS_DIR/tpsh.flt"
    else
        echo "⚠ tpsh elf2flt conversion failed (check $LOGS_DIR/04d-tpsh.log)"
        return 1
    fi
}

do_fbwm() {
    log_step "Step 4b: Building TransputerOS framebuffer desktop"
    if [ ! -x "$ROOT_DIR/transputer-tcc" ]; then
        echo "Run --transputer-tcc first"
        exit 1
    fi

    env KERNEL_TREE="$KERNEL_TREE" \
        bash "$SCRIPT_DIR/fbwm/build-fbwm.sh" \
        "$SCRIPT_DIR/initramfs/fbwm.flt" \
        &> "$LOGS_DIR/04b-fbwm.log"
    echo "✓ fbwm: $SCRIPT_DIR/initramfs/fbwm.flt"
    ls -lh "$SCRIPT_DIR/initramfs/fbwm.flt"
}

do_coremark() {
    log_step "Step 4c: Building spawnable CoreMark"
    env ITERATIONS="${COREMARK_ITERATIONS:-200}" \
        bash "$SCRIPT_DIR/coremark/transputer/build-flt.sh" \
        &> "$LOGS_DIR/04c-coremark.log"
    echo "✓ coremark: $SCRIPT_DIR/initramfs/coremark.flt"
    ls -lh "$SCRIPT_DIR/initramfs/coremark.flt"
}

do_tpkg() {
    log_step "Step 4d: Building TransputerOS package tools"
    env KERNEL_TREE="$KERNEL_TREE" \
        bash "$SCRIPT_DIR/tpkg/build.sh" \
        &> "$LOGS_DIR/04d-tpkg.log"
    echo "✓ tpkg, tpkg-ui, ttop and tfiles built"
    ls -lh /tmp/tpkg.flt /tmp/tpkg_ui.flt /tmp/ttop.flt /tmp/tfiles.flt
}

do_native_tcc() {
    log_step "Step 4e: Building native TCC package payload"
    env KERNEL_TREE="$KERNEL_TREE" \
        bash "$SCRIPT_DIR/build-native-tcc.sh" \
        &> "$LOGS_DIR/04e-native-tcc.log"
    echo "✓ native TCC: /tmp/native-tcc.flt"
    ls -lh /tmp/native-tcc.flt
}

do_doom() {
    log_step "Step 4f: Building DOOM package payload"
    bash "$SCRIPT_DIR/doom/build-transputer.sh" --all \
        &> "$LOGS_DIR/04f-doom.log"
    echo "✓ DOOM: $SCRIPT_DIR/doom/build-tp/doom.flt"
    ls -lh "$SCRIPT_DIR/doom/build-tp/doom.flt"
}

do_packages() {
    log_step "Step 4g: Rebuilding TransputerOS .tpz repository"

    # Package archives are made only after every source-backed payload, so a
    # kernel build that follows stages the newly-created packages into /opt.
    do_fbwm
    do_coremark
    do_tpkg
    do_native_tcc
    do_doom
    bash "$SCRIPT_DIR/packages/build-packages.sh" all \
        &> "$LOGS_DIR/04g-packages.log"

    echo "✓ .tpz repository rebuilt: $SCRIPT_DIR/packages/output"
    ls -lh "$SCRIPT_DIR/packages/output"/*.tpz
}

do_busybox() {
    log_step "Step 4c: Building busybox.flt for initramfs"
    if [ ! -x "$ROOT_DIR/transputer-tcc" ]; then echo "Run --transputer-tcc first"; exit 1; fi

    local TCC="$ROOT_DIR/transputer-tcc"
    local ELF2FLT="$ROOT_DIR/transputer-elf2flt"
    local INITRAMFS_DIR="$SCRIPT_DIR/initramfs"
    local BB="$SCRIPT_DIR/busybox-1.37.0"
    local UCLIBC="$SCRIPT_DIR/uClibc-ng"

    # Build elf2flt if needed
    if [ ! -x "$ELF2FLT" ] || [ "$ELF2FLT_SRC" -nt "$ELF2FLT" ]; then
        echo "Building transputer-elf2flt..."
        cc -Wall -o "$ELF2FLT" "$ELF2FLT_SRC" || {
            echo "⚠ elf2flt build failed"; return 1
        }
    fi

    # Step 1: Compile kernel-mode CRT components
    echo "Compiling kernel-mode CRT..."
    "$TCC" -nostdlib -nostdinc -c -o /tmp/bb_crt1_flat.o "$INITRAMFS_DIR/crt1.S" \
        &> "$LOGS_DIR/04c-busybox.log" || {
        echo "⚠ crt1.S compile failed"; return 1
    }
    "$TCC" -nostdlib -nostdinc -I"$ROOT_DIR/include/transputer" -I"$ROOT_DIR/include" -I"$UCLIBC/include" \
        -c -o /tmp/bb_crt_bridge.o "$INITRAMFS_DIR/crt_bridge.c" \
        >> "$LOGS_DIR/04c-busybox.log" 2>&1 || {
        echo "⚠ crt_bridge.c compile failed"; return 1
    }

    # Step 2: Compile syscall stub
    echo "Compiling syscall stub..."
    "$TCC" -nostdlib -nostdinc -c -o /tmp/bb_syscall_stub.o "$SCRIPT_DIR/tests/syscall_stub.c" \
        >> "$LOGS_DIR/04c-busybox.log" 2>&1 || {
        echo "⚠ syscall_stub.c compile failed"; return 1
    }

    # Step 3: Link busybox ELF
    # BusyBox has a deliberate archive cycle: libbb's applet table references
    # every applet main, while applet objects call back into libbb.  Repeat
    # libbb around the applet archives, which is the static-link equivalent of
    # a --start-group fixpoint scan.  The compiler runtime is explicit because
    # -nostdlib disables TCC's automatic libtcc1 inclusion.
    echo "Linking busybox..."
    rm -f /tmp/busybox_flat.elf "$INITRAMFS_DIR/busybox.flt"
    env T4_STRICT_UNRESOLVED_CALLS=1 \
    "$TCC" -nostdlib -nostdinc \
        -o /tmp/busybox_flat.elf \
        /tmp/bb_crt1_flat.o /tmp/bb_crt_bridge.o \
        "$BB/applets/applets.o" \
        "$BB/libbb/lib.a" \
        "$BB/shell/lib.a" \
        "$BB/coreutils/lib.a" \
        "$BB/coreutils/libcoreutils/lib.a" \
        "$BB/init/lib.a" \
        "$BB/archival/lib.a" \
        "$BB/archival/libarchive/lib.a" \
        "$BB/console-tools/lib.a" \
        "$BB/debianutils/lib.a" \
        "$BB/e2fsprogs/lib.a" \
        "$BB/editors/lib.a" \
        "$BB/findutils/lib.a" \
        "$BB/klibc-utils/lib.a" \
        "$BB/libpwdgrp/lib.a" \
        "$BB/loginutils/lib.a" \
        "$BB/mailutils/lib.a" \
        "$BB/miscutils/lib.a" \
        "$BB/modutils/lib.a" \
        "$BB/networking/lib.a" \
        "$BB/networking/libiproute/lib.a" \
        "$BB/networking/udhcp/lib.a" \
        "$BB/printutils/lib.a" \
        "$BB/procps/lib.a" \
        "$BB/runit/lib.a" \
        "$BB/selinux/lib.a" \
        "$BB/sysklogd/lib.a" \
        "$BB/util-linux/lib.a" \
        "$BB/util-linux/volume_id/lib.a" \
        "$BB/libbb/lib.a" \
        /tmp/bb_syscall_stub.o \
        "$UCLIBC/lib/libc.a" \
        "$ROOT_DIR/transputer-libtcc1.a" \
        >> "$LOGS_DIR/04c-busybox.log" 2>&1 || {
        echo "⚠ busybox link failed (check $LOGS_DIR/04c-busybox.log)"
        return 1
    }

    if [ ! -f /tmp/busybox_flat.elf ]; then
        echo "⚠ busybox link failed (check $LOGS_DIR/04c-busybox.log)"
        return 1
    fi

    # Step 4: Convert ELF to bFLT
    echo "Converting to bFLT..."
    "$ELF2FLT" /tmp/busybox_flat.elf "$INITRAMFS_DIR/busybox.flt" \
        >> "$LOGS_DIR/04c-busybox.log" 2>&1 || {
        echo "⚠ busybox elf2flt conversion failed (check $LOGS_DIR/04c-busybox.log)"
        return 1
    }

    if [ -f "$INITRAMFS_DIR/busybox.flt" ]; then
        echo "✓ busybox.flt built: $INITRAMFS_DIR/busybox.flt"
        ls -lh "$INITRAMFS_DIR/busybox.flt"
    else
        echo "⚠ elf2flt conversion failed (check $LOGS_DIR/04c-busybox.log)"
        return 1
    fi
}

do_transputer_os() {
    log_step "Building complete TransputerOS ($KERNEL_TREE)"

    # Reuse an existing toolchain/emulator for normal OS iteration. A clean
    # checkout still works as one command because missing prerequisites are
    # built here. Set OS_REBUILD_TOOLCHAIN=1 to force those host-side steps.
    if [ "${OS_REBUILD_TOOLCHAIN:-0}" = "1" ] || [ ! -x "$ROOT_DIR/tcc" ]; then
        do_host_tcc
    fi
    if [ "${OS_REBUILD_TOOLCHAIN:-0}" = "1" ] || [ ! -x "$ROOT_DIR/transputer-tcc" ]; then
        do_transputer_tcc
    fi
    if [ "${OS_REBUILD_TOOLCHAIN:-0}" = "1" ] || [ ! -x "$SCRIPT_DIR/t4/t4" ]; then
        do_temu
    fi

    do_initramfs
    do_tpsh
    do_busybox
    do_packages
    do_kernel
    do_btl
    show_summary
}

# Main dispatch
case "${1:-}" in
    --host-tcc) do_host_tcc ;;
    --transputer-tcc) do_transputer_tcc ;;
    --temu) do_temu ;;
    --kernel) do_kernel ;;
    --xip-kernel)
        export T4_XIP_LINK=1
        do_kernel
        ;;
    --initramfs) do_initramfs ;;
    --fbwm) do_fbwm ;;
    --tpsh) do_tpsh ;;
    --busybox) do_busybox ;;
    --tpkg) do_tpkg ;;
    --packages) do_packages ;;
    --shprobe) do_shprobe ;;
    --directprobe) do_directprobe ;;
    --uclibcprobe) do_uclibcprobe ;;
    --btl) do_btl ;;
    --boot) do_boot "$@" ;;
    --test)
        do_boot "$@" --test  # Assuming 05 supports --test
        ;;
    --test-suite) do_test_suite ;;
    --prolog-map) do_prolog_map "$@" ;;
    --all)
        do_host_tcc
        do_transputer_tcc
        do_temu
        do_initramfs
        # Kernel build is experimental - allow failure
        if do_kernel; then
            echo "✓ Kernel build succeeded"
        else
            echo "⚠ Kernel build failed (experimental)"
        fi
        do_btl
        show_summary
        ;;
    --all-boot)
        do_host_tcc
        do_transputer_tcc
        do_temu
        do_initramfs
        # Kernel build is experimental - allow failure
        if do_kernel; then
            echo "✓ Kernel build succeeded"
        else
            echo "⚠ Kernel build failed (experimental)"
        fi
        do_btl
        # Forward optional boot args after --all-boot (e.g. --debug)
        do_boot --boot "${@:2}"
        ;;
    --transputer-os)
        do_transputer_os
        ;;
    --transputer-os-boot)
        do_transputer_os
        do_boot --boot "${@:2}"
        ;;
    *) echo "Unknown option: $1. Run --help"; exit 1 ;;
esac
