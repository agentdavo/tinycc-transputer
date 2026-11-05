# TransputerOS (tpos)

A complete Linux-based operating system for the INMOS T800 transputer,
built entirely with TCC (Tiny C Compiler). Runs on the T4 emulator with
800x600 VGA framebuffer, keyboard/mouse input, and networking.

## Authority stack (who wins when boot is broken)

**One-liner:** HRM/ISM define the truth. **RTL** implements it. **tinycc** targets
that RTL. **linux-7** runs on that stack. Do **not** reverse the order.

| Priority | Layer | Role |
|---------:|-------|------|
| **1** | **ISM / HRM** | Normative: traps, G/L/P, stacks, opcodes, workspace/THDS (`t9000ism.txt`, `t9000hrm.txt`, digests in the chip tree) |
| **2** | **RTL** (chip repo `ip/cpu`, `ip/bus`, `ip/uart`, …) | SPINE + T9 core must match the manuals before blaming software |
| **3** | **tinycc** (`transputer-gen.c`, libtcc1, wrappers) | Emit legal encodings and CALL/ABI for that ISA |
| **4** | **linux-7** (`arch/transputer`) | OS policy: head, THDS install, mm, console, simplefb — on a **working** machine |

### How to assign a bug

| Symptom class | Prefer first |
|---------------|--------------|
| Wrong trap / G-halt / `Reason` / ThReg never changes / store or DEVICE never completes | **RTL** (cite ISM/HRM) |
| Bad encoding, multi-nibble OPR dropped, wrong CALL frame, ULE/UGE shape | **tinycc** |
| Boot policy, cmdline, memblock/FB carveouts, when to `selth`, initcall order | **linux-7** |
| Kernel assumes T4 quirks that violate ISM | Fix **kernel or tcc** — do not weaken RTL past the manuals |

**Guiding rules (non-negotiable):**

1. **Fix the compiler; do not paper over codegen bugs in the kernel.**
2. **Fix the ISA in RTL; do not paper over ISA bugs in Linux or tcc.**
3. Full Linux boot is the *integration* goal, not permission to redefine the machine.

### Cross-repo map (SPINE chip bring-up)

| Path | Use |
|------|-----|
| Chip: `…/gf180mcu-project-template/ip/cpu/PROCESS.md` | G/L/P RTL honesty + bring-up notes |
| Chip: `ip/bus/README.md` | DEVICE/UART/FB map vs linux-7 headers |
| Chip: `.grok/workflows/t9-boot-diag.rhai` | HRM/ISM-first boot diagnose → fix → rebuild → smoke |
| This tree: `KERNEL_TREE=linux-7` (default) | Only active kernel; never patch `linux-2.6.39` |

Workflow for a hang: **cite ISM/HRM → prove RTL (TB or sim dump) → then tcc → then kernel.**

## Quick Start: Build & Boot TransputerOS

```bash
# From project root — full build in order:
bash transputer/build.sh --host-tcc         # 1. Build host TCC compiler
bash transputer/build.sh --transputer-tcc   # 2. Build transputer cross-compiler
# (uClibc-ng must be built separately: cd transputer/uClibc-ng && make)
bash transputer/build.sh --busybox          # 3. Build BusyBox (bFLT)
bash transputer/build.sh --tpsh             # 4. Build tpsh shell (bFLT)
bash transputer/build.sh --initramfs        # 5. Build /init (bFLT)
# Build fbwm GUI (see below)
bash transputer/build.sh --kernel           # 6. Build kernel (stages rootfs, packs CPIO)
bash transputer/build.sh --btl              # 7. Create bootable BTL image

# Boot on T4 emulator (headless — UART output to boot-{pid}.uart)
bash transputer/build.sh --boot

# Boot with VGA display (requires X11/SDL)
DISPLAY=:0 bash transputer/build.sh --boot
```

### Build fbwm (GUI)

```bash
./transputer-tcc -mt800 -w -nostdlib -nostdinc -I transputer/fbwm \
  -c -o /tmp/fbwm_lib.o transputer/fbwm/fbwm.c
./transputer-tcc -mt800 -w -nostdlib -nostdinc -I transputer/fbwm \
  -c -o /tmp/fbwm_main.o transputer/fbwm/fbwm_main.c
./transputer-tcc -nostdlib -nostdinc -I include -I transputer/uClibc-ng/include \
  -c -o /tmp/fbwm_crt1.o transputer/initramfs/crt1.S
./transputer-tcc -nostdlib -nostdinc -I include -I transputer/uClibc-ng/include \
  -c -o /tmp/fbwm_bridge.o transputer/initramfs/crt_bridge.c
./transputer-tcc -nostdlib -nostdinc -c -o /tmp/fbwm_syscall.o transputer/tests/syscall_stub.c
./transputer-tcc -nostdlib -nostdinc -o /tmp/fbwm.elf \
  /tmp/fbwm_crt1.o /tmp/fbwm_bridge.o /tmp/fbwm_main.o /tmp/fbwm_lib.o \
  /tmp/fbwm_syscall.o transputer/uClibc-ng/lib/libc.a
./transputer-elf2flt /tmp/fbwm.elf /tmp/fbwm.flt
```

### Boot Chain

```
T4 emulator → vmlinux.btl → Linux kernel (transputer/linux-7 — the ONLY kernel tree)
  → Self-test (11 hardware tests)
  → /init (bFLT) → networking, /dev, /proc, /sys
  → /usr/bin/fbwm → boot splash → login screen → desktop (Terminal + DOOM icons)
  → /bin/tpsh (interactive shell)
```

### What You Get

- **800x600 VGA GUI** with boot splash, login screen, desktop, terminal window
- **tpsh shell** with builtins (echo, pwd, cd, export) and external commands via tp_spawn
- **BusyBox** coreutils (ls, cat, grep, tar, gzip, ping, wget, etc.)
- **T800 hardware process scheduling** via STOPP/RUNP instructions
- **DOOM** (996KB bFLT, optional .tpz package)
- **Package manager** (tpkg, .tpz format)

## IMPORTANT: Always Use build.sh

**All builds MUST use `transputer/build.sh`.** Do NOT invoke make directly for the kernel — the build script sets up all required wrapper paths, config injection, and environment variables correctly.

## Project Structure

### Core
- **`transputer-gen.c`** — TCC transputer code generator backend
- **`transputer/build.sh`** — Master build script
- **`transputer/rootfs/stage.sh`** — TransputerOS rootfs builder (1.2MB, self-contained)
- **`transputer/tools/transputer-elf2flt.c`** — ELF to bFLT converter for NOMMU binaries
- **`transputer-netload`** — Creates bootable `.btl` files from ELF

### Kernel & Userspace
- **`transputer/linux-7/`** — **ACTIVE kernel tree** (arch/transputer); `build.sh` builds `transputer/$KERNEL_TREE` and `KERNEL_TREE` defaults to `linux-7`
- **`transputer/linux-2.6.39/`** — DEAD legacy tree: build is broken (bounds.s/wrapper bit-rot) and it will not be fixed — **linux-7 is the only kernel**. Arch patches land in linux-7 only (it is a nested git repo)
- **`transputer/uClibc-ng/`** — C library (1.5MB libc.a, 815+ objects)
- **`transputer/busybox-1.37.0/`** — BusyBox 1.37.0 (814KB bFLT, `tpos_defconfig`)

### Applications
- **`transputer/initramfs/init.c`** — PID 1 init process
- **`transputer/initramfs/tpsh.c`** — Transputer shell (155KB bFLT)
- **`transputer/fbwm/`** — Framebuffer window manager (148KB bFLT)
- **`transputer/doom/`** — DOOM port (996KB bFLT)

### Emulator & Tools
- **`transputer/t4/`** — T800 emulator (VGA, 3 UARTs, SDL keyboard/mouse, networking)
- **`transputer/ttools/`** — Historical transputer toolchain (DAST disassembler, AST assembler)
- **`transputer/tests/`** — Backend test suite (0 failures)
- **`transputer/c-testsuite/`** — C conformance tests (214/220 pass)
- **`transputer/packages/`** — .tpz package builder

### Build Wrappers (required for kernel compilation)
- **`transputer/tcc-wrapper.sh`** — GCC-compatible CC wrapper
- **`transputer/tcc-ld-wrapper.sh`** — LD wrapper
- **`transputer/tcc-ar-wrapper.sh`** — AR wrapper
- **`transputer/tcc-nm-wrapper.sh`** — NM wrapper
- **`transputer/tcc-objcopy-wrapper.sh`** — OBJCOPY wrapper

## Build Logs

Each build.sh stage writes logs to `logs/`:
- `logs/01-host-tcc.log` — Host TCC build
- `logs/02-transputer-tcc.log` — Cross-compiler build
- `logs/04-kernel.log` — Kernel build

## TTOOLS Reference

The repo contains a full historical transputer toolchain in `transputer/ttools/`. This is useful for inspecting instruction encodings, understanding legacy assembly/LIT workflows, and cross-checking backend output when normal ELF tools are not helpful.

### Core tools
- **`transputer/ttools/ast/ast`** — assembler for transputer assembly language, emits **LIT object files**
- **`transputer/ttools/lit/lit`** — linker for **LIT** objects/libraries, emits a **LIT executable**
- **`transputer/ttools/litdump/litdump`** — browser/dumper for **LIT** objects/executables; can disassemble selected segments
- **`transputer/ttools/dast/dast`** — raw binary image disassembler for transputer opcodes
- **`transputer/ttools/oserver/oserver`** — legacy host-side boot/output server for `/dev/link0` transputer link hardware
- **`transputer/ttools/t2s`** — tiny `sed` script that converts some KRoC-style asm syntax to TTOOLS syntax

### What each tool is for
- Use **`dast`** when you already have a flat binary blob or a byte range extracted from `vmlinux`, `.btl`, or emulator memory and want to see the actual transputer instructions.
- Use **`litdump`** when you are inspecting historical **LIT** files and want structure plus optional per-segment disassembly.
- Use **`ast`** and **`lit`** mainly as references for original transputer assembly/link semantics, especially instruction sizing, `call/j/cj` offset handling, and the old fragment-based link model.
- Use **`oserver`** only for real/legacy link hardware workflows; for this repo’s normal emulation path, use `transputer-netload` plus `transputer/t4/t4`.

### Practical commands

```bash
# Disassemble bytes from a flat image
transputer/ttools/dast/dast -O 0x80000070 image.bin

# Disassemble a byte range out of flat vmlinux (flat ELF with one PT_LOAD segment)
# CAUTION: the PT_LOAD file offset differs per tree - 0x54 for linux-2.6.39,
# 0x70 for linux-7.  Read it with `readelf -lW vmlinux` (Offset column) rather
# than hardcoding; a wrong base disassembles 28 bytes off and produces
# plausible-looking garbage.
off=$((0x54 + 0x80186036 - 0x80000070))
dd if=transputer/linux-2.6.39/vmlinux bs=1 skip=$off count=192 status=none | \
  transputer/ttools/dast/dast -O 0x80186036

# Dump a LIT object/executable
transputer/ttools/litdump/litdump some.lit

# Dump and disassemble a LIT segment
transputer/ttools/litdump/litdump -d text some.lit
```

### Important behavior notes
- **`dast`** works on raw bytes, not ELF metadata. Use `-O` to tell it the runtime origin address, and `-s` to skip bytes before disassembly.
- **`ast`** automatically treats `call`, `j`, and `cj` operands as PC-relative offsets from the end of the instruction, matching transputer hardware semantics.
- **`lit`** links **LIT** fragments, not ELF objects. Its docs describe the original fragment/need-list model, which is still useful background when reasoning about compact transputer code generation.
- **`litdump -d SEGMENT`** disassembles chosen LIT segments instead of hex-dumping them.
- **`t2s`** is a very small/incomplete converter, not a general assembler frontend.

### Documentation files
- `transputer/ttools/docs/ttools.1` — overview of the package
- `transputer/ttools/docs/ast.1` — assembler behavior and input language
- `transputer/ttools/docs/lit.1` — linker behavior and LIT link model
- `transputer/ttools/docs/litdump.1` — LIT dump/disassembly usage
- `transputer/ttools/docs/dast.1` — raw binary disassembler usage
- `transputer/ttools/docs/lit.5` — LIT file format reference

### Repo-specific caveats
- In this checkout, `dast` and `litdump` are executable, but `ast`, `lit`, `oserver`, and `t2s` currently lack execute bits. If you need to run them locally, `chmod +x` them first.
- For current kernel/backend work, prefer `dast` over generic `objdump` when examining `transputer/linux-2.6.39/vmlinux`, because `vmlinux` is emitted as a flat ELF (`Machine: 0x325`) with no section headers.

## Build & Test (Test Program)

```bash
bash transputer/build.sh --transputer-tcc

# Compile test program (with optional debug: T4_GENOPI_DEBUG=1)
./transputer-tcc -nostdlib -nostdinc -c -o /tmp/test_strlen_dbg.o transputer/test_strlen.c
./transputer-tcc -nostdlib -nostdinc -c -o /tmp/test_boot.o transputer/test_boot.S
./transputer-tcc -nostdlib -nostdinc -o /tmp/test_strlen_boot.elf /tmp/test_boot.o /tmp/test_strlen_dbg.o

# Create bootable and run on T4 emulator
./transputer-netload /tmp/test_strlen_boot.elf /tmp/test_strlen_boot.btl
DISPLAY= timeout 5 transputer/t4/t4 -s8 -si -se -sb /tmp/test_strlen_boot.btl
```

Expected output: `S T L 5 ! O K \n H`

## Regression Testing (C Test Suite)

The `transputer/c-testsuite/` directory contains a C test suite adapted for the transputer target. Run it after any code generator or linker changes to check for regressions.

```bash
# Prerequisites: transputer-tcc and transputer-netload must be built first
bash transputer/build.sh --transputer-tcc

# Run the full test suite (220 tests, takes ~10 minutes)
cd transputer/c-testsuite && sh single-exec tcc-transputer

# Output is TAP format. Count pass/fail:
cd transputer/c-testsuite && sh single-exec tcc-transputer 2>&1 | grep -c '^ok '
cd transputer/c-testsuite && sh single-exec tcc-transputer 2>&1 | grep -c '^not ok '
```

- **Expected baseline**: targeted regressions `00195`, `00204`, and `00216` pass on the current tree; rerun the full suite to refresh the global pass count
- Tests are in `transputer/c-testsuite/tests/single-exec/*.c`
- Skip list: `transputer/c-testsuite/runners/single-exec/tcc-transputer.skip`
- Each test compiles with transputer-tcc, links with boot stub + runtime + `transputer-libtcc1.a` (64-bit divide etc.), runs on T4 emulator, and compares UART output to `.expected` file
- Runner knobs: `TCC_BIN`/`NETLOAD_BIN`/`T4_BIN` (test another build), `TP_EXTRA_CFLAGS` (e.g. `-mt805`; the runtime and boot stub are then built per test with the same flags, and tests tagged `tvx` are skipped unless it is `-mt9000tvx` - with no `-m` they get `-mt9000tvx` - and tests tagged `move2d` are skipped at `-mt414`, which has no 2D block move), `LIBTCC1` (helper library; by default the one built for that part: `-t800.a` for `-mt800`/`-mt805`, `-t425.a`, `-t414.a`, else `transputer-libtcc1.a`), `TP_LAYOUT_PAD=N` (link N bytes of padding before the test so every address moves - catches layout-dependent bugs)
- The runtime's `printf` now does C99 width/flags/precision for every integer conversion (`%08x` used to drop the zeros); test 09110

## Differential Testing (fuzz)

`transputer/c-testsuite/fuzz/` finds compiler and T4 bugs the hand-written suite misses (run in WSL):

```bash
cd transputer/c-testsuite
bash fuzz/difftest.sh 200 1        # seeds 1..200; failures land in ~/tccfuzz/fail
python3 -I fuzz/reduce.py ~/tccfuzz/fail/p18.c small.c          # shrink a miscompile
python3 -I fuzz/reduce.py ~/tccfuzz/fail/p10.c small.c crash    # shrink a compiler crash
```

- `gen.py SEED` writes a random program with no undefined or unspecified behaviour: 8/16/32/64-bit arithmetic and sign extension, guarded division, masked shifts, calls inside expressions (eval-stack spills), struct pass/return/copy, varargs (ints, long longs, doubles), bitfields, unions, 2D arrays, function-pointer tables, memcpy/memset/memmove, switch tables, loops with break/continue, static locals, float conversions.
- `difftest.sh` builds each program with host gcc (-O0 under UBSan, -O2 must agree) for the oracle, then runs it on T4 through the c-testsuite runner at every layout in `PADS` (default `0 1027`).
- A reduced failure goes into `tests/single-exec/091NN.c` with gcc's output as `.expected`.
- First runs (2026-11-02, 440 seeds) found: T4 `move` and link receives corrupting data in the FB window (`test_move_regions`, `tests/net/bytestream.sh`), `x && 2LL` crashing the compiler (09111), `load()` turning stale `sym` pointers into relocations (09112), double conversions clobbering REG_FRET+1 (09113), double `?:`/return high words in the wrong register for want of `RC_FRE2` (09114), and `__ashrdi3` losing the sign for shifts of 33..63 (09115).

## Local Backend Tests

The ad-hoc backend regression tests now live in `transputer/tests/`.

- Use `bash transputer/tests/test_runner_with_uart.sh --list` to see which files are runnable, which are helper-only, and which have explicit UART oracles.
- Use `bash transputer/tests/test_runner_with_uart.sh --all` to run the discovered UART tests on `transputer/t4/t4`.
- Use `bash transputer/tests/test_runner_with_uart.sh test_name ...` to run a focused subset.
- The runner understands three entry styles:
  - `test_main()` via `transputer/tests/test_boot.S`
  - plain `main()` via an auto-generated wrapper to `test_main()`
  - standalone `_start()` programs
- The runner also knows the current multi-file bundles:
  - `main` + `support.c`
  - `simple_main` + `support.c`
  - `test_main` + `test_functions.c`
  - `test_wsubdb_semantics` + `test_wsubdb_semantics_fn.S`
  - `test_xcall_main` + `test_xcall_helper1.c` + `test_xcall_helper2.c`
  - `test_xfile_a` + `test_xfile_b.c`
- Some files under `transputer/tests/` are helper TUs, debug-only experiments, or linker-symbol experiments and are intentionally skipped by the runner.
- Another part: `TP_EXTRA_CFLAGS=-mt805` (with `T4_CPU=-s425`/`-s414` for the strict T4xx modes).  `TP_EXTRA_LIBS` defaults to the runtime built for that part (`transputer-libtcc1.a` with no `-m`).  A test that needs a part says so in its header comment, `Target: -mt9000tvx` (the TVX tests): it gets that flag when `TP_EXTRA_CFLAGS` names no part, and is skipped when it names another - except that `-mt9000tvx`, the T9000 plus TVX, also runs the `Target: -mt9000` tests.  A test that needs a feature rather than a part says so with `Needs:`: `fpu` (skipped at -mt425/-mt414), `t805ops` (the T425 additions the original T800 and the T414 lack, such as `lddevid`; skipped at -mt800/-mt414) or `softfp` (runs only at -mt425/-mt414).

## Build & Test (Kernel)

```bash
# Build kernel
bash transputer/build.sh --kernel

# Create bootable BTL image (step 5)
bash transputer/build.sh --btl

# Boot in T4 emulator (step 6) — uses -sm 28 (256MB), logs to logs/06-boot.log
bash transputer/build.sh --boot

# Boot with debug tracing (trace level 15)
bash transputer/build.sh --boot --debug

# Boot with full debug (trace + halt on uninitialized memory read)
bash transputer/build.sh --boot --debug-full

# Boot a specific BTL file
bash transputer/build.sh --boot /path/to/file.btl

# Full build + boot test
bash transputer/build.sh --all-boot
```

**IMPORTANT**: Always use `build.sh --boot` for kernel testing. It sets `-sm 28` (256MB memory), `-ss` (host services), and proper trace flags. Running t4 manually with default 2MB memory will crash immediately since the kernel is ~7.3MB.

**Boot time** (measured 2026-11-02, `vmlinux.btl` of 2026-11-01, headless, no TAP): T9000 protection self-test @ ~3s, "TransputerOS 0.1.0" @ ~3.5-4s, fbwm `DESKTOP_READY` @ ~10s. The old ~18-minute figures (self-test @ ~744s, /init @ ~817s, prompt @ ~1080s) date from the Apr 2026 boot and no longer apply. See `transputer/t4/T4_PERF.md`.

UART output is logged to `boot-{pid}.uart` (timestamped to avoid conflicts with concurrent T4 instances). A `boot.uart` symlink points to the latest.

## T4 Emulator VGA Registers

The T4 emulator provides VGA control at `0xA0000000`:

| Register | Address | Description |
|----------|---------|-------------|
| CONTROL | 0xA0000000 | bit 0 = enable display |
| FB_BASE | 0xA0000004 | framebuffer base address |
| WIDTH | 0xA0000008 | display width in pixels |
| HEIGHT | 0xA000000C | display height in pixels |
| STRIDE | 0xA0000010 | bytes per row |
| FLIP | 0xA0000014 | write src addr → sync back buffer to display |
| SCREENSHOT | 0xA0000018 | write any value → save numbered BMP |

Input UARTs:
- **Console**: 0x10000000 (data), 0x10000004 (status)
- **Keyboard**: 0x11000000 (VT100 escape sequences from SDL)
- **Mouse**: 0x12000000 (3-byte serial packets: 0x80|buttons, dx, dy)

## Process Model

TransputerOS uses two mechanisms for running child processes:

- **`tp_spawn`** (syscall 2000) — Cooperative: setjmp/longjmp. Parent workspace saved, child runs, longjmp on exit returns to parent. Works now.
- **`tp_hw_spawn`** (syscall 2001) — Hardware: T800 STOPP/RUNP. Parent truly descheduled via hardware queue, child runs as a real transputer process. Enables true concurrency.

Both are transparent to userspace — `tpsh` tries hw_spawn first, falls back to tp_spawn.

## Development Philosophy

**Codegen quality is the highest priority. Always fix the compiler, never patch around it.**

Every bug must be traced to its root cause in `transputer-gen.c`, `transputer-link.c`, `tccelf.c`, or TCC internals. Kernel source patches are a last resort for genuine TCC parser/language limitations (e.g., no forward aliases) — never for codegen bugs. If the compiler generates wrong code, fix the compiler. If the linker resolves wrong addresses, fix the linker. The kernel is the test case, not the thing to fix.

Signs you're doing it wrong:
- Adding `#ifdef CONFIG_TRANSPUTER` workarounds for runtime bugs
- Replacing function calls with macros to avoid linker issues
- Adding UART debug markers to narrow down a hang (use the T4 emulator trace instead)
- Commenting out BUG_ON/assertions that fire due to wrong codegen

Signs you're doing it right:
- Writing a minimal C test case that reproduces the bug
- Fixing the bug in transputer-gen.c and verifying with the C test suite
- Running `T4_CALL_AUDIT=1` or `T4_RELOC_FIX_DEBUG=1` to validate linker output
- The kernel "just works" further after a compiler fix

## Current Status (Apr 2026)

### TransputerOS 0.1.0 — Boots to GUI Desktop

```
7.3MB kernel + 1.4MB rootfs → 12/12 self-test → init → TransputerOS 0.1.0 → BusyBox
```

- **Kernel**: Linux 2.6.39, tpos_defconfig, **7.3MB vmlinux** (down from 8.4MB via compiler optimizations), boots on T4 with 256MB RAM
- **Self-test**: 12/12 hardware tests pass at boot (LDTIMER, FPU f32/f64/ftoi, SD sector, keyboard/mouse UART, VGA, network, console)
- **CPU detection**: T806 @ 200MHz via LDDEVID (new `__lddevid` builtin)
- **CONFIG_FPU=y**: T800 hardware FPU enabled, 61/61 FPU tests pass
- **Hash tables capped**: TCP/UDP/route hash tables hardcoded to 256 entries (saves ~6MB alloc, ~30s boot time). **Note**: `__setup()` handlers don't work on transputer (TCC linker `.init.setup` section issue), so boot parameters are parsed but NEVER dispatched. Fix is to hardcode values under `#ifdef CONFIG_TRANSPUTER`.
- **GUI**: fbwm renders boot splash → login screen → desktop (Terminal + DOOM icons) → terminal window
- **VGA**: 800x600 ARGB framebuffer with double buffering, FLIP register for frame sync
- **Input**: 3 UARTs — console (0x10M), keyboard (0x11M), mouse (0x12M) with SDL event capture
- **Shell**: tpsh with builtins (echo, pwd, cd, export, exit) + external commands via tp_spawn
- **Process creation**: tp_spawn (setjmp/longjmp cooperative) + tp_hw_spawn (T800 STOPP/RUNP hardware)
- **BusyBox**: ~446KB bFLT, coreutils + debug toolkit (`ps`, `free`, `df`, `uptime`, `stat`, `kill`, `pidof`) + tar/gzip/ping/wget/nc/ifconfig
- **uClibc-ng**: 1.5MB libc.a, sprintf/malloc/crt0 chain all working
- **DOOM**: 996KB bFLT, runs on T4 with keyboard/mouse input
- **Packages**: .tpz format, tpkg package manager, tpkg-ui GUI
- **System monitor**: ttop (176KB) — CPU/MEM bars, process list, uptime
- **Backend tests**: 0 failures (full test suite), C test suite 214/220
- **Boot time**: ~10s to the fbwm desktop as of 2026-11-02 (in Apr 2026: self-test @ ~744s, /init @ ~817s, PID 1 ready @ ~1080s)
- **5-byte local encoding**: Fixed codegen for functions with frames > 256 words (was silently truncating)

### Key Fixes (Apr 2026)
- **Hash table caps** (2026-04-09) — TCP/UDP/route hash tables hardcoded to 256 entries via `#ifdef CONFIG_TRANSPUTER` in `net/ipv4/tcp.c`, `udp.c`, `route.c`. Was previously allocating 524K-entry tables (~6MB) on 256MB RAM, taking ~30s+ on interpreter. **Root cause**: `__setup()` handlers don't work on transputer — the TCC linker doesn't properly collect the `.init.setup` section, so `thash_entries=256` in cmdline is parsed but never dispatched. Workaround: hardcode the values.
- **Zero-STL local-to-local binary ops** (2026-04-08) — Skip `gv2()` entirely for local+local operands, emit LDL directly onto eval stack. Saves ~1 STL+LDL round-trip per op. Extended to local+const, const+local, and FPU (float32/float64).
- **FPU fused load-and-operate** — `fpldnladdsn/db`, `fpldnlmulsn/db` combine memory load + arithmetic for `a+b`, `a*b` where both are locals. Based on INMOS BTL analysis of rayshade/raytrace/povray.
- **3-byte local placeholder + 5-byte jump placeholder** — Reduced from 5/9 bytes. Zero-STL + instruction selection reduced kernel from 8.4MB → 7.3MB (~13% smaller).
- **Peephole optimizer** — 8 patterns: j0→pfix0, LDC0+SUM/DIFF→NOP, LDC0+SHL/SHR→NOP, ADC0→NOP, LDC1+PROD→NOP, LDC0+OR/XOR→NOP.
- **Transputer builtins**: `__norm`, `__bitcnt`, `__bitrevword`, `__bitrevnbits`, `__csub0`, `__fmul`, `__lddevid` (the last enables CPU detection for T806).
- **T806 CPU mode**: Emulator and kernel detect T806 @ 200MHz via LDDEVID opcode (OPR 0x17C).
- **Compaction pass (DISABLED)**: Function-level PFIX 0 removal works for forward jumps but breaks backward loop jumps (not tracked in jump_patches). See compaction-findings memory note. Potential savings: ~32% of .text.
- **5-byte local patch encoding** — Prologue AJW and local access (LDL/STL/LDLP) instructions now use 5-byte placeholders instead of 3. Supports frames up to ~1M words. Previously frames > 256 words had silently truncated offsets causing workspace corruption. This was the root cause of "large function tail miscompilation".
- **64-bit `+=` codegen bug** — `load()` used `v+1` for high word instead of `sv->r2` after `lbuild()`. **FIXED**.
- **64-bit shift hang** — Added `gen_opl_shift()` with native LSHL/LSHR + 0-63 clamp. **FIXED**.
- **bFLT relocation** — Embedded reloc prefix before code; runtime fixup handles LDC + R32 relocs. Works correctly (strlit test confirms all 6 patterns pass in bFLT).
- **VGA framebuffer** — Extended `vga_framebuffer` to cover both front + back buffer (FB_TOTAL = 2 * FB_SIZE). Eliminates memory aliasing that caused garbled screenshots.
- **tp_spawn** — Synchronous spawn via setjmp/longjmp, bypasses broken do_fork entirely.
- **hw_spawn.S** — T800 STOPP/RUNP assembly for true hardware process scheduling.
- **Kernel VFS syscalls** — sys_open/read/write/close/lseek/dup/pipe/fcntl/chdir/getcwd all wired to real kernel VFS. Hybrid I/O: fd 0-2 = UART direct, fd 3+ = kernel VFS.

### Codegen wins from the ISA survey (2026-08-05)

Worked through items A1-A6 and B1 of `transputer/ISA-OPPORTUNITIES.md`.
Cumulative object-size change at `-mt800`: **tpsh.c -42%** (20623 -> 11951),
fbwm.c -4%, test_tvx_vec.c -9%; char-heavy benchmark **-21% cycles**.
C suite 223/1 (only the known VLA `00207`) throughout.

- **A1 byte store via `sb`** — was a ~37-instruction word read/modify/write.
  Its "keeps packed u8 fields coherent" justification was backwards: the RMW
  rewrote three neighbouring bytes. ISM p.408 gives `sb` exactly the needed
  semantics. 106 -> 32 bytes on a single store. Also fixed the root cause of a
  latent mis-modelling: **`Tp_SB`/`Tp_SS` popped 1 but `sb`/`ss` consume 2**
  (the peephole's own table already had `delta = -2`).
- **A2 16-bit store via two `sb`s** (re-landed 2026-11-03) — `ldl v; ldl a;
  sb; ldl v; ldc 8; shr; ldl a; adc 1; sb`, the address parked in slot 15,
  instead of a read/modify/write of the containing words: 181 -> ~15 bytes a
  store.  Corpus code (c-testsuite, Doom, fbwm) **-7.3% at `-mt800`** (Doom
  -9.7%) and -7.5% at `-mt425`; `-mt9000` is byte-identical, as it keeps `ss`
  and only an odd displacement reaches this path there.
  It was reverted in August for c-testsuite 09003 (`-mt800`), blamed on the
  dynamic frame policy.  That was not it: 09003 fails the same way with a
  fixed 20-word region (`TCC_FRAME_POLICY=0`/`1`).  `tp_stage_call_words`
  spills a call's register words to the slots just above `func_max_slot_used`
  *as it stands at the call*.  The RMW had touched slot 19, so they went to
  20+; with only slot 15 used they land on 16-18, and printf's halfword
  arguments - still lvalues when a variadic call is staged - are then loaded
  through slots 18-19 (15-16 for a local), over them.  The `late_impure` rule
  added since (spills move to slot 20 when a later push is impure) already
  closed it: disabling it brings 09003 back.  Now also enforced:
  `tp_stage_push` records the staged slots and `emit_instr` stops the compile
  on any access to one from inside a push, so a new scratch-using path cannot
  silently pass a clobbered argument.  `test_stage_scratch.c` (7 call shapes)
  gets wrong arguments at `-mt9000` and `-mt800` without the rule and a
  compile error with only the check.
- **A3 `xword` sign extension** — `USE_XWORD` was `#ifdef`'d but `#define`d
  nowhere, so the 6-instruction and/xor/diff fallback always ran. 4
  instructions instead of 6. The `ldc mask; and` **must stay**: it supplies
  XWORD's ACWG precondition, and `gen_cvt_csti` reaches the helper with an
  arbitrary int.
- **A4/A5/A6 comparisons** — `x != 0` needed no `eqc` at all; `>=`/`<=` invert
  for free via `vset_VT_CMP(TOK_EQ)` instead of an explicit `eqc 0`; and `<`
  and `>=` swap their operands in `gen_opi` instead of emitting `rev`.
  `eqc` 16 -> 8 and `rev` 3 -> 0 on the sample functions.
- **B1 `memcpy` -> `move`** — inlined to the block-transfer instruction, for
  variable lengths too. Not `memmove` (overlap is undefined for `move`).
- **B2 constant local array init** — mostly solved by A1 as a side effect
  (`char buf[16]="hello"`: 3023 -> 959 bytes, -68%), plus a new peephole rule
  killing the dead `rev; rev` pairs it left. The template-in-`.rodata` + `move`
  form is still open.
- Also landed the report's A10: `bsub` added to the peephole's commutative
  list (`wsub`/`ssub`/`wsubdb` deliberately excluded - they are order-sensitive).

New tests: `test_narrow_store.c` (1107 checks, both targets),
`test_sign_extend.c` (53), `test_memcpy_move.c` (4321).

### Bug status (2026-08-04 sweep)

**FIXED — ADC trapped on unsigned overflow.** `ADC` is the *error-checking*
add (sets the transputer Error flag on signed overflow); `SUM` is the
unchecked one. TCC folded `x + const` into `ADC` regardless of signedness, so
any wrapping unsigned arithmetic (LCG, hash, checksum, ring buffer) set the
Error flag and `t4 -se` stopped the run. `tp_adc_would_trap()` in
`transputer-gen.c` now suppresses the fold for unsigned integers only —
**signed keeps ADC** (overflow is UB) and **pointers keep ADC** (transputer
addresses are negative as signed, so `+small` cannot overflow), which
preserves the struct-field-addressing win. Costs ~16 bytes on fbwm.
The same trap was left in `x * -1`, lowered as `not; adc 1`: a 64-bit
multiply's cross terms come that way whenever one operand's low word is
all ones (`x * -1LL`, `(u64)(i32)-1 * y`), and their words carry the
64-bit type's sign, so a signed `long long` trapped too, on a product C
defines.  Negating MinInt there set the Error flag and stopped the run
(difftest seed 9929).  Now `not; ldc 1; sum` (2026-11-04, c-testsuite
`09117`).

**FIXED — the linker silently accepted undefined symbols.** It emitted
`ldc <garbage>; gcall`, exited 0, and produced a binary that wild-jumped at
run time. A check existed but was opt-in behind `T4_STRICT_UNRESOLVED_CALLS`
*and* only covered `IPREL32` with `val == 0`, missing the common case
(indirect calls go through `LDC_ABS`). Now warns by default for
IPREL32/LDC_ABS/R_32, deduplicated per symbol, silent for weak symbols;
`T4_STRICT_UNRESOLVED_CALLS=1` still makes it fatal. This converts a class of
mystery hangs into one-line diagnostics.

**NOT A BUG — 64-bit division.** `long long /` and `%` appeared to hang, but
`gen_opl` correctly calls the `__divdi3`/`__moddi3` helpers; they live in
**`transputer-libtcc1.a`** (the host `libtcc1.a` is x86-64 and irrelevant).
`-nostdlib` omits it. With it on the link line, 64-bit division works. The
old silent-undefined-symbol behaviour is what made this look like a codegen
hang.

**NOT A BUG — "large function" miscompilation.** A 57,000-instruction
function compiles and runs correctly; size alone is not the trigger. The case
that looked like it was the missing `__divdi3` above.

**OPEN — `__init_array_start/end`, `__preinit_array_*`, `__fini_array_*` are
undefined on this target.** uClibc's `__uClibc_main` walks
`__init_array_start..__init_array_end` to run constructors, so it currently
iterates a garbage range (latent: userspace boots because those arrays are
empty in practice). Root cause: `elf_output_file()` returns
`transputer_simple_link()` at the *very top* of the function, skipping both
`tcc_add_runtime()` and `resolve_common_syms()` -> `tcc_add_linker_symbols()`
further down — which is also why `_etext`/`_edata`/`_end` needed the surrogate
data-word hack in `transputer-link.c`. Adding `resolve_common_syms()` before
the early return is *not* sufficient on its own (tried; the symbols are
defined but `transputer_simple_link` does not pick them up), so this needs a
proper look at what state that function captures.

~~**OPEN — VLA / `00207`.**~~ Fixed 2026-11-03: VLAs live in a per-process
arena and `WPtr` never moves; see "Variable-length arrays: a per-process
arena" below.

### Known TCC Backend Bugs (and Status)
- ~~**Cross-file function call relocation**~~ — **ROOT CAUSE FOUND AND FIXED**: Was archive ordering + symbol precedence issue, NOT a relocation bug. TCC's a-la-carte archive loading processes archives left-to-right; function pointer arrays in `.data` referenced symbols from archives not yet loaded. **Fix (Mar 2026)**: `ST_FROM_ARCHIVE` flag in `st_other` (tcc.h), archive-sourced symbols tagged in `tcc_load_object_file()` (tccelf.c), `set_elf_sym()` now lets direct .o override archive symbols. Kernel workarounds (`tp_find_next_zero_bit` etc.) may now be removable.
- ~~**VLA + statement expression**~~ (`min()`/`max()` macros) vstack leak — no longer reproduces (2026-11-03); what remained was VLAs themselves being broken, now fixed (see "Variable-length arrays")
- **Forward `__attribute__((alias(...)))`** not supported — needs TCC parser fix; workaround: reorder definitions
- **Compound literal casts in static initializers** not supported — needs TCC parser fix; workaround: runtime init
- **Linker script `symbol = .;`** — TCC doesn't resolve linker script symbol assignments (all return 0) — fixed in `transputer-link.c` with surrogate data words
- ~~**Unsigned comparison**~~ — **FIXED in `transputer-gen.c`** (XOR with MINT before GT)
- ~~**64-bit float/double register-pair collapse**~~ — **FIXED in `transputer-gen.c`** (`TREG_FB`/`TREG_FC` now participate in `RC_FLOAT`, so double pairs stay `3/4` instead of collapsing to `3/3`)
- ~~**Transputer 64-bit spill save corruption**~~ — **FIXED in `tccgen.c`** (`save_reg_upstack()` no longer performs a generic second-word save on this backend; `store()` already writes the full pair)
- ~~**C-testsuite `%f` truncation mismatch**~~ — **FIXED in `transputer/c-testsuite/test_runtime.c`** (round before digit extraction)
- ~~**`!!x` double logical NOT inversion**~~ — **FIXED in `transputer-gen.c`** (VT_CMP cmp_op in load())
- ~~**`x & (x-1)` miscompilation**~~ — Was actually `!!` bug — **FIXED**

### Key Code Generator Fixes (`transputer-gen.c`)
- **5-byte local patch encoding** — `emit_local_op()` and prologue AJW now use 5-byte placeholders (was 3). Supports frames up to ~1M words. Old 3-byte encoding silently truncated frames > 256 words (1KB), corrupting workspace for functions with large local structs/arrays. This was the root cause of all "large function tail miscompilation" bugs. The epilog patcher now uses `tp_encode_instr()` for correct encoding at any size.
- **Unsigned comparison**: XOR both operands with MINT before GT
- **`!!x` VT_CMP materialization**: Check `cmp_op & 1` when materializing
- **memset/memcpy/memmove SB operand ordering** — fixed reversed operands
- **16-bit loads/stores** — built from `lb`/`sb` pairs where `ls`/`ss` cannot be used
- **gen_cvt_csti sign extension** — reload from workspace before sign extension
- **Workspace memory map** — FRAME_SLOT_MIN_LOCALS=20, scratch at TP_SCRATCH_BASE=15

### Key Linker Fixes (`transputer-link.c` + `tccelf.c`)
- **Archive symbol precedence** (tcc.h + tccelf.c) — `ST_FROM_ARCHIVE` flag tags archive-sourced symbols; `set_elf_sym()` lets direct .o override archive (standard linker behavior). Root cause of the long-standing "cross-file relocation bug".
- **Symbol verification & correction** — name-based lookup fixes stale GLOBAL symbol indices after merge
- **Linker symbol surrogates** — `__linker_stext/etext/edata/end` data words patched with section boundaries
- **tp_patch_iprel()** — handles PFIX/NFIX prefix encoding for CALL/J/CJ relocations
- **tp_patch_ldc_abs()** — handles absolute address loads via LDC prefix sequences
- **Peephole optimizer** — 7 patterns: `j 0`→NOP, `LDC 0;SUM/DIFF`→NOP, `LDC 0;SHL/SHR`→NOP, `ADC 0`→NOP, `LDC 1;PROD`→NOP
- **LDPI optimization** — `tp_patch_ldc_abs()` uses `LDC offset;LDPI` for nearby refs (±32KB, strictly shorter than absolute)
- **GTU instruction** — Unsigned comparisons use single GTU (was 6-instruction MINT/XOR/GT pattern); `-mt9000` only since 2026-11-03, as the T805 has no `gtu`
- **ADC optimization** — `a+const` uses single ADC instruction (was LDC+SUM)
- **BSUB for pointer arithmetic** (2026-08-04) — the final `+` of **any**
  pointer computation now emits `bsub` instead of `sum`. Identical arithmetic
  (`AReg + BReg`, commutative), but the T9000 grouper charges `bsub` to
  **stage 2 (address calcs, 2/cycle, measurably spare)** and `sum` to
  **stage 4 (ALU, 1/cycle, the narrowest resource)**.
  Two hooks: `gen_ptradd_bsub()` for the `elem_size == 1` direct form, and
  the `tp_ptr_add_is_addr` flag set by `tccgen.c` around the generic
  `gen_opic('+')`, which catches every other element size — short arrays,
  non-power-of-two struct strides, arrays of pointers.
  Measured: **−7.4%** cycles on char-heavy code, **−5.4%** on
  short/struct-indexing code *with an identical instruction count*, −0.17%
  on float+byte-store. Object code slightly smaller. C suite 220/220 bar the
  known VLA `00207`; `test_ptradd_bsub.c` is 338 checks, identical on/off.
  Constant displacements deliberately keep `adc` (1 instruction vs
  `ldc n; bsub` = 2, and `ldc` is stage-4 anyway).
  **Gotcha**: the first cut added a `gv2()` fallback for non-simple operands
  to force `bsub`; that lost the zero-STL local-to-local lowering in
  `gen_opi` and cost *more* dynamic instructions than it saved (+0.9% on one
  benchmark, despite fewer static instructions). `bsub` buys nothing the
  generic path cannot do, so emit it **only when it is free** — never spill
  to obtain it. A/B with `TCC_TRANSPUTER_BSUB_OPT=0`.
- **DUP optimization** — Same-operand detection in gen_opi (x*x, x+x → DUP instead of double load)
- **NOT+ADC 1 negation** — Unary minus uses 2 instructions (was 3)
- **Transputer builtins** — __norm, __bitcnt, __bitrevword, __bitrevnbits, __csub0, __fmul
- **NFIX→OPR fix** — OPR operands are unsigned; replaces invalid NFIX→OPR with PFIX→OPR

### Key Platform Fixes (arch/transputer)
- **strnlen_user()** (asm/uaccess.h) — asm-generic maps `__strnlen_user` to `strnlen` which excludes NUL. Fixed with `__transputer_strnlen_user` that returns strnlen+1. Without this, ALL exec'd argv strings lack NUL terminators.
- **transputer_flat_finish()** (arch/transputer/kernel/exec.c) — Exec regs setup moved out of load_flat_binary() into separate compilation unit. TCC miscompiles tail of large functions (layout-sensitive workspace corruption).
- **brk start address** (arch/transputer/kernel/syscall.c) — Moved from 0x80200000 (inside kernel image) to 0x8FC00000 (past loaded binaries ~0x8FAB0000)
- **read() timeout** (arch/transputer/kernel/syscall.c) — UART rx poll has 5M iteration timeout, returns EOF for headless operation

### Kernel Build Patches (temporary workarounds for TCC limitations)
Each patch below exists because of a specific TCC limitation. When the TCC bug is fixed, the patch should be removed:
- **mm/bootmem.c** — Local `tp_find_next_zero_bit` (**may be removable now** — was cross-file archive issue, now fixed), ternary instead of `min()`/`max()` (TCC vstack leak — no longer reproduces, 2026-11-03)
- **mm/backing-dev.c** — Runtime init instead of compound literal (TCC parser limitation — still needed)
- **init/main.c** — UART macros instead of `printk` (**may be removable now** — was cross-file archive issue, try removing after rebuild)
- **head.S** — Linker symbol surrogates (TCC linker script limitation), BSS loop REV fix, workspace AJW moved to 0x80F00000 (was overlapping bootmem allocations)
- **arch/transputer/kernel/Makefile** — Added traps.o (was missing, caused unresolved trap_init)
- **lib/radix-tree.c, lib/vsprintf.c** — VLA→fixed array, ternary for `min()` (TCC vstack leak; **removable now** — VLAs work since 2026-11-03)
- **kernel/timeconst.pl** — `defined(@val)` → `!@val` (Perl 5.36+ compat, not a TCC issue)

### Shell Wrappers (Critical for Kernel Build)
The kernel build system expects GCC-compatible tools. All five `transputer/tcc-*.sh` wrappers are **mandatory**:
- `tcc-wrapper.sh` — Translates GCC flags to TCC, handles `-S` for asm-offsets, filters unsupported flags
- `tcc-ld-wrapper.sh` — Handles `ld -r` (relocatable linking) via TCC
- `tcc-ar-wrapper.sh` — Creates static archives
- `tcc-nm-wrapper.sh` — Lists symbols from object files
- `tcc-objcopy-wrapper.sh` — Handles objcopy operations

## Architecture Notes

### Transputer Eval Stack vs Workspace
The transputer has a 3-deep hardware evaluation stack (AReg, BReg, CReg). TCC's "registers" map to workspace slots accessed via LDL/STL instructions.

### Key Design Decisions
- **Always-STL**: Every `load()` stores to workspace via STL
- **tp_push_sv()**: Reloads values from workspace onto eval stack before operations
- **REG_IRET isolated**: Registers 7-8 NOT in RC_INT pool

### Calling Convention (from TCWG)
- CALL pushes return address to AReg, decrements Wptr by 1 word
- Callee stores return address with STL 0, allocates locals with AJW -N
- Return via: load return value to AReg -> AJW +N -> load return addr -> GCALL
- After return: caller does `STL REG_IRET` to capture return value
- An early `return x;` leaves through an epilogue of its own (`ajw +N;
  ret`, `gfunc_leave()`), not a jump to the shared one: `j` is a timeslice
  point, and a low-priority process descheduled there resumes with whatever
  the other processes left in A/B/C, so the return value it had loaded was
  lost (`tests/test_return_timeslice.c`: 7-10 wrong results in 400,000
  calls between two processes before the fix, 2026-11-03).  A `return;`
  still jumps, having nothing live.  Cost: +0.36% code on the Doom/fbwm/
  c-testsuite corpus, and one `j` fewer executed per early return.  The
  same rule holds for any `j`: nothing may be live on the A/B/C or FPU
  stacks across one (checked over that corpus by logging every `j` emitted
  with a non-empty stack model: only dead leftovers of a taken `cj`).
- **Known limitation, not fixed: loops do not timeslice.**  A loop's
  back-edge is a backward `cj`, which is not a timeslice point, so a loop
  whose body has no `j` (`while (!flag) ;`, a summing loop) never yields to
  another low-priority process: spinning on a flag that one of them sets
  never ends.  Wait on a channel or timer instead (`include/conc.h`).
  `lib/transputer-conc.c` *relies* on this: its process-record search must
  contain no timeslice point (see "Variable-length arrays"), so if loop
  back-edges ever become `j`, that search needs another shape.

### Using the stacks well: rules learned from occam (2026-11-03)

Porting the INMOS occam ray tracer to C (`transputer/t4/examples/
transputer-raytracer/c/`, whose `CODEGEN.md` has the side-by-side listings)
gave a strict comparison: the same 405061 rays, pictures bit for bit, and
763M executed instructions in occam against 1813M for the first C (1062M
now).  The gap was never one construct - both programs spent their time in
the same routines - but how every expression used the stacks.  Dhrystone
against INMOS icc says the same (`t4/examples/dhrystone`, 500000 passes):
229M instructions for icc, 626M for TinyCC, but half of TinyCC's is the
c-testsuite runtime's byte-at-a-time `strcpy`, where icc's library copies a
word at a time; without `strcpy` it is 1.44x, the ray tracer's ratio.  What
the occam compiler does, and what new codegen should be held to:

**The machine** (TCWG chapter 7, ISM)
- A/B/C and FA/FB/FC are separate three-deep stacks.  A load pushes and
  loses the bottom (C, FC); an operation pops two and pushes one; a store
  pops.  A tree that needs at most three registers needs no workspace at
  all: evaluate the operand that needs more registers first (Sethi-Ullman
  labelling), and spill only when both sides need three.
- Every FP load and store takes its address from A (`fpldnlsn`,
  `fpstnlsn`, the fused `fpldnladdsn`/`fpldnlmulsn` and their `db` forms),
  so integer address arithmetic runs while values wait on the FPU stack.
  The FP comparisons (`fpgt`, `fpeq`; `fpge` on the T9000) pop FA and FB
  and leave the truth value in A.  The T800's comparisons order a NaN
  as a large magnitude (ACWG B.2.7), so each C relation there is masked with
  `fpordered`; the T9000's are the IEEE relations (ISM 11.9.1) -
  `tp_emit_fp_compare`.
- `fpsub` and `fpdiv` compute FB op FA.  When the right operand is already
  in FA, `fprev` (two bytes) fixes the order; never store and reload to do
  it.  Only + and * have fused memory forms, so `x - mem` is `fpldnlsn;
  fpsub`.
- IEEE + and * commute bit for bit, so swapping the operands to evaluate the
  deeper side first is legal, and occam does it; reassociating is not.
  The ray tracer's `pic/` references are the check: anything beyond a
  commute changes pixels.
- Nothing stays live on either stack across a call (the callee uses them)
  or a `j`/`lend` (timeslice points: another process runs).
- T800 rounding modes (`fpurp`/`fpurm`/`fpurz` through `fpentry`) govern
  the next FP instruction only - every FP instruction sets the mode back
  to nearest (TCWG: `Round_Mode' = ToNearest`).  The T9000's `fprX` are
  sticky until `fprn`.
- Conversions go through memory by design: `fpi32tor32`/`fpi32tor64`/
  `fpb32tor64` load an integer from the address in A, `fpstnli32` stores
  one.  The workspace word an int<->float conversion uses is not waste.
- Many T800 FP operations are `ldc n; fpentry` (square root first/step/
  last, abs, mulby2/divby2, expinc32/expdec32, r32tor64/r64tor32, the
  rounding modes, seterr/clrerr): two or three bytes and an A-stack entry
  each.  The T9000 has most as single secondaries (`fpsqrt` #D3, `fpabs`
  #DB, `fpmulby2` #D2, ...).  Emit every one through the part check, so
  `-mt800`/`-mt805` never get a T9000 form.

**What TinyCC does now** (`transputer-gen.c`; keep it doing it)
- An FP result that the next operation consumes stays in FA
  (`tp_fpu_take_resident`), and a consumed float slot takes the result.
- A constant subscript of a local or global array folds into the address
  (`tp_ptradd_const_folds`): `&p[0]` is `ldlp p`, not seven instructions.
- At the end of an expression statement, or of a for loop's init or step,
  `tp_stmt_end` removes the dead parking left in the history window:
  `stl x; ldl x`, a push stored to a dead slot, and `ldlp x; fpstnlsn;
  ldl x; stl y` -> `ldlp y; fpstnlsn`.  `i = 0;` is `ldc 0; stl i` and
  `i++;` is `ldl i; adc 1; stl i`.  -16% instructions on the ray tracer.
  `TCC_TRANSPUTER_VPOP_DEAD=0` turns it off for an A/B run.
- Builtin operands that are constants, word locals, registers, `&local` or
  `&global` are pushed directly: `__in(4, c, &w)` is `ldlp w; ldl c; ldc
  4; in`, as occam compiles it.
- A single-word `return x;` cancels its `stl 7; ldl 7`.
- An element at a variable index loads with the indexed forms: `emit_instr`
  folds `wsub; fpldnlsn` into `fpldnlsni` and `wsubdb; fpldnldb` into
  `fpldnldbi` (base in A, index in B, as for wsub); the loop body of
  `s = s + v[i]` (`test_fpu_indexed.c`) is
  `ldl i; ldl v; fpldnlsni; ldlp s; fpldnladdsn; ldlp s; fpstnlsn`.
  `TCC_TRANSPUTER_FPLDNLI=0` keeps them apart.
- `x * 2.0`, `2.0 * x`, `x * 0.5`, `x / 2.0` and `x / 0.5` are
  `fpumulby2`/`fpudivby2` through `fpentry` on the T800 (`fpmulby2`/
  `fpdivby2` on the T9000), as occam does: 10 cycles where the constant,
  parked and multiplied in, cost 23.  `x + x` stays an addition on the
  T800 (`fpldnladdsn`, 8 cycles, beats `fpentry`'s 12).  Exact, as scaling
  by two is in IEEE arithmetic; `test_fpu_by2.c` checks it bit for bit,
  overflow, denormals and round-to-even included.
- Up to two FP values stay on the FPU stack between operations
  (`tp_fpu_take_under`): when an operation's right operand has just been
  taken back into FA and its left operand's store is in the history window
  with nothing since that disturbs it, that store is taken back too, so the
  left operand sits in FB and a bare `fpadd`/`fpsub`/`fpmul`/`fpdiv` or
  compare does the work.  `a*b + c*d` is `fpldnlsn; fpldnlmulsn;
  fpldnlsn; fpldnlmulsn; fpadd`, as occam.  The check simulates the stack
  depth above the left operand over every instruction since: pushes need
  room, the fused loads need it exactly one down (they leave FC undefined),
  binary operations, compares and `fprev` two, and the window must end
  with just the right operand above it.  And an array operand whose address
  was computed and parked is loaded where it was computed
  (`tp_fpu_load_early`): `ldl a; ldnlp 1; fpldnlsn; ldl b; ldnlp 1;
  fpldnlmulsn`, not `...; stl 9; ...; ldl 9; fpldnlmulsn`.  And when the
  left operand of a `+` or `*` cannot stay under the right one (it would
  be in FC under a fused load), its own code moves instead
  (`tp_fpu_take_moved`): if it starts from empty stacks and only loads and
  computes, and the right operand's code stores nothing it reads, the
  right operand is computed first and the left on top of it, occam's
  deeper-side-first order - a commute, so the same bits.  `a*b + (c*d +
  e*f)`, `dotProduct` and each row of `TransLocal`, is then occam's code
  exactly.  Together -275 M T800 cycles on the ray tracer (3,552 M ->
  3,277 M; occam 2,947 M), every picture unchanged; its FP adds and stores
  now match occam's counts.  `TCC_TRANSPUTER_FPSTACK=0` turns all three
  off, `=2` traces each take-back and move,
  `TCC_TRANSPUTER_FPSTACK_ONLY=name` limits them to matching functions;
  `test_fpu_stack.c`.
- `x * x` is one load and `fpdup; fpmul` (12 cycles, where `ldlp x;
  fpldnlmulsn` cost 14), as occam: for a variable, an element of a local or
  global array, or a member of a struct or union, nested or in an array of
  structs (`tp_fpu_same_operand`, which finds the operand's place in its
  symbol's type, `tp_type_holds`), and for `p->x * p->x` through a pointer
  when the two address computations are the same instructions on unchanged
  locals (`tp_fpu_square_twin`).  Never for a volatile: tccgen's arithmetic
  conversion drops the qualifier before the back end sees the operands, so
  `gen_op` sets `tp_opf_volatile` from their types first, and the symbol's
  type is checked too.  The T9000's `x + x` -> `fpmulby2` uses the same
  test (its old one let a volatile through).  A square's code moves like
  any other (`tp_fpu_pure` knows `fpdup`), so `normalize`'s `a*a + (b*b +
  c*c)` is occam's code too.  The ray tracer now executes occam's
  5,268,802 `fpdup`s exactly, where it had none: -12 M cycles (3,277 M ->
  3,266 M).  `TCC_TRANSPUTER_FPSTACK=0` turns it off too;
  `test_fpu_square.c`.

**Invariants for any pass that rewrites code already emitted**
- Never across a jump target.  `gsym_addr` (forward targets) and `gind()`
  (loop heads, `continue` targets, goto and case labels, through
  `tp_label_here`) clear the history; raw `g()` bytes break its
  contiguity.  A cleanup that rolled back over a `gind()` label moved the
  code under the label (`test_vla`, `test_stmt_end_labels.c`).
- A slot is dead only if no vstack entry names it and nothing later in the
  window reads it - and the vstack says that much only at the end of a
  statement.  Mid-expression, tccgen and the builtins hold slots outside
  it: `gv()`'s two-word load pops a temporary before naming the second
  slot, a builtin pops its parked arguments before loading them, VLA code
  keeps the arena top in REG_IRE2 from `gen_vla_result` to
  `gen_vla_sp_save`.  Running the cleanup at every `vpop` miscompiled
  09103, 09109 and the ray tracer's start-up.
- `ldlp x` hands out x and x+1 (a double): it reads both.
- The fused FP loads (`fpldnladdsn`/`fpldnlmulsn` and the `db` forms)
  leave FC undefined (TCWG chapter 11), so nothing may be kept in FC across
  one; pushes, pops, stores, compares and `fprev` carry FC along.  A first
  cut that kept three products on the stack for `a*b + (c*d + e*f)` lost
  the first one (`test_fpu_resident`'s `dot3` caught it).
- `CValue.i` is a `uint64_t`, so `sv->c.i < 0` is never true: the
  "canonical spilled copy" branches in `load()` and `tp_emit_float_addr`
  never run, and a register lvalue is always loaded from its register.

**Open** (roughly in order of what each would buy on the ray tracer;
measured in T800 cycles with every instruction counted, fusion off: occam
2.95 G, TinyCC 3.27 G, 1.11x, where instructions are 1.21x - most of the
extra instructions are one- and two-cycle loads and stores)
- Parking through workspace: `ldl`/`stl`/`ldlp` +187 M cycles of the
  318 M gap.  What is left is integer: values parked between operations
  (`ldl a; adc 1; stl 9; ldl 9`), which the FPU-stack work does not reach.
- Global addresses: `tree`, `worldModel`, ... are 7-cycle `ldc`s, 106 M
  cycles in 15 M loads, where occam pays 2-3 through its static link.
  Keeping a global's address in a frame word from its second use in a
  basic block, labels forgetting it, saved only 2.5 M (blocks seldom use
  a global three times) and hung model 1 on one node, so it was not kept.
  What would pay is a function-wide base: hoisting across loop
  iterations needs dominance, or occam's static-link scheme (an ABI
  change).
- FP constants as eight-nibble `ldc` literals parked in a slot (1.0,
  0.001, 512.0, 4.0: about 25 M cycles after `x * 2.0`): load them from a
  constant table instead.
- `fpordered` before every T800 relation (C's NaN rule): 19 M.
- Loop and condition code: TinyCC's `cj`, `gt`, `adc` and `eqc` cost
  +200 M against occam's `lend` (-174 M); a counted `for` could be `lend`.
- Prologues copy every parameter into the frame (`ldl 16; stl 14`); occam
  uses them where the call left them (`ldl 2`).
- TinyCC does not inline: a `static inline` wrapper round one instruction
  is a call, a frame and a return - 13 instructions to input a word, where
  occam spends 4.  Header helpers of an instruction or two should be macros
  (`conc.h`'s channel I/O).
- Never emitted: `fpnan` and `fpnotfinite` (isnan/isfinite); `fpchki32`;
  the T9000's `fprange` and `fplg`.
- Not worth doing, measured: running `tp_stmt_end` after a local's
  initializer too.  Declarations do leave parking (that is what crossed
  the goto label above), but cleaning it saved 0.04% of c-testsuite code
  and cost the ray tracer 0.006% more instructions: a cleanup resets FA
  residency.
- A float comes back through slot 7, an integer word, and the caller stores
  it again; occam leaves it in FA.  That one is an ABI change.
- Byte loops (Dhrystone's `strcpy`, 25 instructions a byte): `p++` goes
  through two slots (`ldl 13; stl 9; ldl 9; stl 10; ldl 9; adc 1; stl 13`
  where `ldl p; dup; adc 1; stl p` would do), a loaded `char` is sign
  extended (`ldc 128; xword`) although only `sb` uses it, and the loop test
  extends it again (`ldc 255; and; ldc 128; xword; eqc 0`) for a zero test
  that needs no extension at all.
- **A miscompile, fixed 2026-11-03** (difftest seed 9010, older than this
  work): `u64 x4(void) { return (zero ? m64 : m64) + (-(u64)((u16)l0)); }`
  (all globals) returned the wrong sum.  gv() moves the `?:` result out of
  slots 7/8: it allocated 10 for the low word and called `load(10, ...)`,
  whose register-to-register copy of a 64-bit value also writes the high
  word to `r + 1` - 11, where the negation's low word was live - and then
  12 for the high word, whose `load()` wrote 13 as well.  gv()'s
  move-registers path now loads each word as a word (`load_type`, as its
  lvalue path already did), and `gv_dup()` of a double keeps its copy in
  registers and moves the one below to a temporary (it copied the pair as
  `r`/`r + 1` with only `r` allocated, and both copies shared `r2`).
  `load()` itself still fills `r`/`r + 1` for its other callers: the
  constant and lvalue shortcut saves `r + 1` first, and a diagnostic build
  found no `r + 1` outside its class there over the corpus at four
  targets.  Of 302 corpus objects only `00204.o` and `09114.o` change,
  both smaller (dead `rev; stl 10; ldl 10; rev`, a high word written to a
  third slot).  Tests: `tests/test_pair_move.c`, c-testsuite `09116`.

**Writing C for it** (programming, not the compiler)
- Copy arrays and structs with `memcpy` (inlined as `move`): a word loop
  cost the ray tracer 10.5% of its run.  Fill an array by doubling
  `memcpy`s.
- `__fpsqrt`/`__fpabs`, not wrappers round them.
- ALTs of two or three channels: `ChanAlt2`, `ChanAlt3`,
  `ChanAltGuarded3` (`conc.h`, straight-line, about 30 instructions)
  rather than `ChanAlt` over an array (about 150).  occam's inline ALT is
  17.

**Measuring**: t4's `heartbeat` (instructions executed, printed at exit)
does not depend on the host's load; t4's clock is the host's, so times do.
`heartbeat` counts dispatches, and t4 fuses common pairs (`stl; ldl`,
`ldlp; fpldnlsn`, ...) into one, so it understates both compilers by
about 28%; for real instruction counts and T800 cycles use the counting
t4 and `oph.py` in `t4/examples/transputer-raytracer/c` (`CODEGEN.md`
there, "Every instruction, in T800 cycles").
`T4_IPTR_SAMPLE=N` samples IPtr for a profile against the `.elf.tccsym`
map; `T4_DUMP_WORDS=addr:n,...` dumps memory (which is how the occam code
was read).  A bit-exact reference output is the best oracle for an FP
codegen change.

### UART Output
- **UART MMIO address**: 0x10000000 (write data register), 0x10000004 (status register)
- **Critical**: `uart_putc_raw` is a **macro** in `asm/uart.h`, NOT an extern function (TCC linker bug workaround)
- **VGA framebuffer**: 0x90000000 (640x480 ARGB), controller at 0xA0000000
- **T4 emulator**: Logs UART output to `boot.uart` file, creates X11 window when DISPLAY is set

### Debug
- `T4_GENOPI_DEBUG=1` — Prints gen_opi operands and gjmp_cond decisions to stderr
- `T4_DATA_RELOC_DEBUG=1` — Log R_TRANSPUTER_32 data section relocations (function pointer arrays, etc.)
- `T4_RELOC_FIX_DEBUG=1` — Log symbol verification corrections in linker
- `T4_IPREL_PATCH_DEBUG=1` — Log IP-relative (CALL/J/CJ) patching details
- `T4_LINK_DEBUG=1` — Verbose linker debug output
- `T4_RELOC_LOG=1` — Log relocation emission during compilation
- `T4_LINK_RELAX=0` — Turn off link-time call relaxation (`tp_link_relax`, after ICF) for an A/B link; `T4_RELAX_STATS=1` prints what it saved per section. `T4_ICF=0` / `T4_ICF_STATS=1` do the same for ICF.

### T4 Emulator Memory
- **Default**: 2MB — far too small for kernel (crashes immediately)
- **`-sm 26`**: 64MB — minimum for kernel boot (setup.c assumes this)
- **`-sm 27`**: 128MB — recommended for full kernel with drivers
- **`-sm 31`**: 2GB — max; for large model/inference work
- Memory starts at MinInt (0x80000000) and grows upward
- Above 256MB the display windows (FB carveout 0x81E00000, legacy FB
  0x90000000, VGA ctrl 0xA0000000) are **automatically relocated** below
  MemStart so RAM stays contiguous — see `t4_relocate_mmio_for_ram()` in
  `transputer/t4/p.c`. At/below 256MB the historic map is unchanged.

### T4 interpreter hot path
Read the design rules in `transputer/t4/T4_PERF.md` before adding anything to
`mainloop()` in `p.c` or to the memory accessors. In short: every
per-instruction check runs ~10^8 times a second, so optional hooks go in
`run_insn_hooks()` or the every-64-instructions timer block, and any trace
left inside an instruction case starts with `rare_hooks &&`; `t4_sig_pending`
must stay per-instruction (`handler(-1)` is also the emulator's internal stop);
hot helpers need `HOT_INLINE` because GCC will not inline into `mainloop()`;
the accessors test `t9k_prot.active`, which `t9k_update_active()` keeps; and
a `mainloop()` local changed after its `setjmp()` must be reset when a P-trap
`longjmp`s back. T4's own debug prints (`[VGA_STNL]`, `[LDNL]`, ...) appear only
when a T4 diagnostic env var is set.

### T9000 pipeline model
`T4_T9000=1` enables an instruction-grouper / cycle estimator
(`transputer/t4/t9000_group.h`) modelling the T9000's 5-stage pipeline and
8-instruction groups. Prints cycles, IPC and a group-size histogram at exit.
Optimistic static model — no cache/branch effects. See TVX-ISA.md section 12.

Every machine parameter is overridable (`T4_T9K_LOCAL`, `_ADDR`, `_NONLOCAL`,
`_ALU`, `_WRJMP`, `_GROUP`, `_GROUPBYTES`, `_FETCH`, `_WSCACHE`, `_STOREFREE`,
`_SPLITWRJMP`, `_PIPESLOW`) for sensitivity analysis; defaults reproduce the
documented T9000. **Measured result: for TVX-dominated code no pipeline
parameter matters** — a TVX call site costs a flat 22 cycles of scalar
overhead whatever the instruction does, so a 16-block GEMV row is 96%
functional-unit time. Widen the MAC array before touching the pipe
(`T4_T9K_MACLANES`). Of the four candidate pipeline changes evaluated at
8 MAC lanes, **only a decoupled (non-group-closing) TVX unit pays** — 18%,
rising to 27% at 16 lanes. Split write/branch ports = 1%, store-needs-no-
stage-2 = 0%, a second accumulator = 0% (blocked by always-STL codegen, which
stores each GEMV result immediately, so two rows are never in flight).
`T4_T9K_PROFILE=tvx` therefore selects **only** the decoupled unit (enabling
all four is 1 cycle worse); the others stay available as individual switches.
That leaves the hardware within 8% of the functional-unit floor at 8 lanes —
the residual is the dependent FP store, and closing it needs codegen to
software-pipeline the FP path, which is also what would make a second
accumulator worth anything.

The model also charges: workspace-cache references only for the first 32
locations (`ldl 40` costs an address calculation plus a memory read), a
stage-2 address slot for *every* store (`stl` must compute an address, `ldl`
need not), and a 4-bytes/cycle fetch bound reported alongside the issue bound
so `pfix 0` padding shows up as a cost.

### T9000 channels, links and the VCP
`transputer/t4/t9000_vcp.c` implements the T9000 communication architecture:
virtual links multiplexed over the physical links by the VCP (32-byte
packets, acknowledges, link queues, VLCB instructions), event channels,
resource channels (`mkrc`/`grant`/`enbg`/`disg`/..., reachable from C
through `<t9000.h>`), `vin`/`vout`/`ldcnt`,
`stopch`/`resetch`/`setchmode`/`ldchstatus`, and the `ldconf`/`stconf`
configuration space.  `p.c` has the semaphores (`wait`/`signal`) and the
shadow registers
(`stshadow`/`ldshadow`, P-process state saved on interrupt), `restart` and
`tret`.  Links reset into byte-stream mode, so T800 code and the link boot
protocol are unchanged until software selects virtual mode.  A byte-stream
`out`/`outbyte`/`outword` on a threaded (`-st`), shared-memory (`-sl`) or
loopback link completes only once the far process has input the whole
message, as on a real OS-link (ISM 8.4.2; t4 7cb262c, `Channel.ackwait`).
Before that it completed as soon as the packet was in the far mailbox, so a
sender could run a message ahead of its receiver.  Over nanomsg `ipc://` an
output still completes on nanomsg's accept.  Single-node
testing: `T4_LINK_LOOPBACK=1,2-3` (cables a node's links together) and
`T4_EVENT_LOOPBACK=1`; `T4_VCP_DEBUG=1` traces.  Tests live in the t4 repo:
`transputer/t4/tests/test_t9000_vcp.c` (one node) and
`test_t9000_vcp_net.c` (two threaded nodes), run by
`transputer/t4/tests/run_t9000_vcp.sh` (`make -f Makefile.linux test-vcp`).
Threaded networks (`-st -sn -N`) can include IMS C104 routing switches -
`router` statements in the network map (interval tables, header deletion,
link groups), engine in `t4_fabric.c`, `T4_ROUTER_DEBUG=1` traces; tested
by `tests/test_t9000_c104_net.c`.
The control system (HRM ch.8) is `t9000_ctl.c`: CLink0/1 are network-map
links 4/5 (`Connect processor 0 link 2 to processor 1 clink 0`); a node can
be configured (CPoke), booted (Boot/BootData/Run), stopped and reset over
CLink0 by T9000 code on a virtual link of the controller's data link, and
reliable-link VCP errors and halt-on-error CPU errors come back as Error
messages.  Link faults are modelled: `WrongParity`/`ResetOutput` in a
(C)LinkNCommand register give the far end a parity/disconnect error, which
halts the link until restarted and is reported as #80-#83 / #C2 unless
LocalizeError is set.  `T4_CTL_DEBUG=1` traces; tested by `tests/test_t9000_ctl_net.c`
and `test_t9000_ctl_chain.c`.
Details and the spec mapping: `transputer/t4/T9000_CHANNELS.md`.

## Key Constants
```
NB_REGS = 15
REG_IRET = TREG_R0 = 7    (return value workspace slot)
REG_IRE2 = TREG_R1 = 8    (secondary return workspace slot)
RC_INT = 0x0001            (general purpose register class)
Registers 9-14: RC_INT (general purpose)
Registers 7-8: RC_IRET/RC_IRE2 only (return values)
Registers 0-2: eval stack (A, B, C)
Register 6: RC_WREG (workspace pointer)
ELF_START_ADDR = 0x80000070 (T800 MemStart)
UART_MMIO_ADDR = 0x10000000
VGA_FB_BASE = 0x90000000
VGA_CTRL_BASE = 0xA0000000
```

## Future Work

### Near-term
- **Investigate page state BUG** — `BUG: Bad page state in process swapper` appears during aggressive kernel config slimming. May be pre-existing, masked by previous UART debug output flooding.
- **T4 emulator performance** — The 2026-11-02 fixes made the interpreter ~1.8x faster on Dhrystone (CPU time 3.20s -> 1.78s), but it is still ~1.45x slower than the 2023 source and ~1.8x slower than the shipped 2023 binary; `transputer/t4/T4_PERF.md` lists what remains. The boot (~10s) is not interpreter-bound and gained only ~5%. Computed goto dispatch or a JIT would mainly help CPU-heavy guests (benchmarks, DOOM, native TCC).
- **Native TCC** — Loads and starts (4129 relocs applied) but TCC's own main() doesn't produce output. May be too slow on T4, or needs the 5-byte fix applied to the native build.
- **Kernel modules** — `CONFIG_MODULES` not set. Would need `arch/transputer/kernel/module.c` (~150 lines) for ELF relocation support.

### Medium-term
- **Interactive fbwm** — Event loop with keyboard/mouse input, window drag, click-to-focus. Code written, needs testing with DISPLAY=:0.
- **True T800 concurrency** — `tp_hw_spawn` (STOPP/RUNP) enables concurrent processes: fbwm drawing while tpsh runs, background commands.
- **Kernel input driver** — `t4_input.c` polls keyboard/mouse UARTs → `/dev/input/event0,1`. Enable Linux input subsystem for proper event handling.

### Long-term
- **Multi-transputer** — T800 link channel communication between nodes
- **Self-hosting** — Native TCC compiling programs on the transputer itself
- **Networking** — TCP/IP works in kernel; BusyBox has ping/wget/nc. Need socket support in userspace syscall stub.

## uClibc-ng Transputer Port (WORKING)

The C library for transputer userspace lives in `transputer/uClibc-ng/`. Architecture-specific files are under `libc/sysdeps/linux/transputer/`. **libc.a builds successfully (815+ objects, 0 errors, 1.37MB).**

### Build
```bash
cd transputer/uClibc-ng
make ARCH=transputer defconfig
LD_LIBRARY_PATH="" make    # builds libc.a, crt1.o, crti.o, crtn.o
```

### Verified Working
- **sprintf/snprintf** — all format specifiers (%s, %d, %04X, %x, mixed, truncation)
- **malloc/calloc/realloc/free** — dynamic allocation via brk() syscall
- **Full crt0→crt1→__uClibc_main→main chain** — bare-metal boot to C main()
- **25+ standard headers compile**: stdio.h, stdlib.h, string.h, unistd.h, fcntl.h, signal.h, errno.h, sys/types.h, sys/stat.h, sys/wait.h, sys/socket.h, setjmp.h, ctype.h, etc.

### Key Architecture Files
- **`extra/Configs/Config.transputer`** — Arch config: little-endian, NOMMU, no LDSO, no PIC
- **`libc/sysdeps/linux/transputer/crt0.S`** — Bare-metal boot entry (INMOS protocol: MINT+ADC+GAJW, queue/timer init, GCALL to _start)
- **`libc/sysdeps/linux/transputer/crt1.c`** — C runtime entry (_start calls __uClibc_main with main function pointer; written in C for proper relocations)
- **`libc/sysdeps/linux/transputer/setjmp.S`** — Full setjmp (saves WPtr, IPtr, 8 callee-saved workspace slots)
- **`libc/sysdeps/linux/transputer/__longjmp.S`** — Full longjmp (GAJW workspace switch trick)
- **`libc/sysdeps/linux/transputer/clone.S`** / **`vfork.S`** — Return -ENOSYS stubs
- **`bits/syscalls.h`** — Syscall interface via `__transputer_syscall()` C function

### Design Decisions
- **Syscalls via C function** — `__transputer_syscall(number, a1..a6)` instead of inline asm. TCC's transputer asm can't express register binding macros.
- **crt1 in C, not assembly** — TCC generates proper `R_TRANSPUTER_LDC_ABS` relocations for function pointers (main, __uClibc_main). Raw `.byte` in assembly can't generate relocations.
- **crt0.S uses INMOS boot protocol** — `MINT + ADC offset + GAJW` to set workspace to absolute address (2MB above MemStart), then init queues/timers/error flags before calling _start.
- **No dynamic linker** — `ARCH_HAS_NO_LDSO` set. Static linking only.
- **No PIC/PIE** — `HAVE_NO_PIC` set. Transputer has no GOT/PLT mechanism.

### TCC Compatibility Fixes Applied to uClibc Headers
- **`sys/cdefs.h`** — TCC-specific `__REDIRECT`/`__REDIRECT_NTH` that skip `__asm__` symbol aliasing; `__TINYC__` keeps C99 `inline`
- **`bits/types.h`** + **`sys/types.h`** — `__TINYC__` gets `long long` 64-bit typedefs (fixes fcntl/stdlib → byteswap `__uint64_t` parse errors)
- **`sys/wait.h`** + **`sys/socket.h`** — `__TINYC__` guard to avoid `__transparent_union__`
- **`features.h`** — Fallback `libc_hidden_proto()` etc. as empty macros outside libc build
- **`include/stdarg.h`** — Added `__GNUC_VA_LIST` define so `__gnuc_va_list` = `va_list` not `void *`
- **`tcc-wrapper.sh`** — `-D__BCC__=1` disables `__GI_*` hidden aliases; `-dN→-dM` mapping; `-Wl,*` stripping
- **`tcc-ar-wrapper.sh`** — Added `@file` response file support
- **`include/limits.h`** — Created TCC-compatible limits with SSIZE_MAX, HOST_NAME_MAX, NZERO
- **`.config`** — `UCLIBC_SUSV3_LEGACY=y` so `usleep` (and friends) land in `libc.a` for DOOM etc.
- **Full notes:** `transputer/uClibc-ng/UCLIBC-TCC-NOTES.md`

### __transputer_syscall (in `arch/transputer/kernel/syscall.c` and `transputer/tests/syscall_stub.c`)
The NOMMU syscall dispatcher. Userspace calls it as a regular C function (no protection boundary).

Implemented syscalls:
- `write(fd, buf, count)` — stdout/stderr → UART
- `read(fd, buf, count)` — stdin returns EOF
- `exit/exit_group` — UART marker + STOPP
- `brk(addr)` — simple heap bump allocator (initial brk at 0x80200000)
- `ioctl` — returns ENOTTY
- `getpid/gettid/getuid/getgid` — returns 1/1/0/0
- `clock_gettime` — returns 0
- `mmap2` — returns ENOMEM
- `munmap` — silently succeeds

### Linking Userspace Programs Against uClibc-ng
```bash
# Compile
./transputer-tcc -mt800 -w -D__GNUC__=4 -D__GNUC_MINOR__=4 -D__GNUC_PATCHLEVEL__=0 \
  -nostdinc -I./include \
  -Itransputer/uClibc-ng/include \
  -Itransputer/uClibc-ng/libc/sysdeps/linux/transputer \
  -Itransputer/uClibc-ng/libc/sysdeps/linux/common \
  -Itransputer/linux-2.6.39/include \
  -c -o program.o program.c

# Link (with crt0 bare-metal boot + crt1 C runtime + syscall stub + libc)
./transputer-tcc -mt800 -w -nostdlib -o program.elf \
  /tmp/crt0.o transputer/uClibc-ng/lib/crt1.o \
  program.o transputer/tests/syscall_stub.o \
  transputer/uClibc-ng/lib/libc.a

# Create bootable BTL and run on T4 emulator
./transputer-netload program.elf program.btl
DISPLAY= timeout 10 transputer/t4/t4 -s8 -sm 26 -si -se -ss -sb program.btl
cat boot.uart
```

## BusyBox Transputer Port (IN PROGRESS)

BusyBox 1.37.0 source in `transputer/busybox-1.37.0/`. Compiles 177 objects (all applets) successfully. Individual applets work when called directly.

### Current Status
- **Compilation**: All enabled applets compile (echo, cat, true, false, yes)
- **Linking**: Links successfully (~206KB ELF with libc.a)
- **Full applet dispatch**: **WORKS** — `main()→run_applet_and_exit()→find_applet_by_name("echo")→applet_main[1]()→echo_main()` outputs "Hello Transputer!" via UART
- **Root cause of earlier hang**: Archive ordering — `libbb/lib.a` must come before `coreutils/lib.a`. Fixed properly via `ST_FROM_ARCHIVE` symbol precedence in tccelf.c

### Build
```bash
cd transputer/busybox-1.37.0
make allnoconfig    # start minimal
# Edit .config: set CROSS_COMPILER_PREFIX, EXTRA_CFLAGS, EXTRA_LDFLAGS, STATIC=y, NOMMU=y
# Enable desired applets (CONFIG_ECHO=y, CONFIG_CAT=y, etc.)
LD_LIBRARY_PATH="" make V=1
```

### TCC Compatibility Fix
- **`include/platform.h`** — `__TINYC__` guard on `_Pragma("GCC visibility push(hidden)")` (TCC doesn't support `_Pragma`)

## Inspecting Compiled Code

**Always inspect generated code when optimizing or debugging the compiler.**
Use `dast` (in `transputer/ttools/dast/`) to disassemble transputer bytecode.

### Disassemble an ELF or bFLT

```bash
# Disassemble CoreMark ELF (skip 0x54 byte ELF header, origin at MemStart)
transputer/ttools/dast/dast -s 0x54 -O 0x80000070 /tmp/coremark.elf | head -50

# Disassemble a specific function from the kernel (compute offset from ELF)
off=$((0x54 + 0x80186036 - 0x80000070))
dd if=transputer/linux-2.6.39/vmlinux bs=1 skip=$off count=192 status=none | \
  transputer/ttools/dast/dast -O 0x80186036

# Disassemble a small test to see codegen patterns
./transputer-tcc -nostdlib -nostdinc -c -o /tmp/test.o test.c
transputer/ttools/dast/dast -s 0x34 -O 0x0 /tmp/test.o | head -30
```

### Instruction Frequency Analysis

Count instruction types across an entire binary to identify optimization targets:

```bash
transputer/ttools/dast/dast -s 0x54 -O 0x80000070 binary.elf 2>&1 | \
  grep -oP '(pfix|nfix|ldc|ldl|stl|ldlp|ldnl|stnl|adc|eqc|ajw|call|cj|j|rev|sum|diff|prod|and|or|xor|not|shl|shr|gt|gtu|dup|lb|sb|wsub|ret|gcall|mint|move|lmul|ldpi|fpadd|fpsub|fpmul|fpdiv)\b' | \
  sort | uniq -c | sort -rn | head -20
```

### What to Look For

| Pattern | Meaning | Action |
|---------|---------|--------|
| **Many `pfix 0`** | NOP padding from oversized placeholders | 5-byte local encoding or 9-byte LDC_ABS |
| **`stl N; ldl N`** | Redundant store+reload (always-STL) | Known limitation — depth scheduling needed |
| **`ldc 0; sum`** | Add zero (no-op) | Should be caught by peephole |
| **`mint; xor; rev; mint; xor; gt`** | Old unsigned comparison | Should now be `gtu` |
| **`ldc 0; rev; diff`** | Old negation | Should now be `not; adc 1` |
| **`ldc N; sum`** where N small | Add small constant | Should now be `adc N` |
| **No `dup` in x*x patterns** | DUP not firing | Check gv2 operand matching |
| **`ldpi` after `ldc`** | LDPI optimization active | Good — saves vs 9-byte LDC_ABS |

### CoreMark Code Profile (205KB ELF, Apr 2026)

```
Total instructions: 173,984
  pfix 0 (NOP padding):   48,508  (27.9%)  ← #1 waste, from oversized placeholders
  ldl (load local):       18,512  (10.6%)
  stl (store local):      14,400  ( 8.3%)  ← always-STL overhead
  ldc (load constant):     6,865  ( 3.9%)
  and:                     2,632  ( 1.5%)
  shl:                     1,708  ( 1.0%)
  sum:                     1,053  ( 0.6%)
  rev:                       987  ( 0.6%)
  adc (our optimization):   731  ( 0.4%)  ← ADC const working
  cj (conditional jump):    730  ( 0.4%)
  gtu (our optimization):    46  ( 0.03%) ← GTU working
  dup (our optimization):    35  ( 0.02%) ← DUP working
  ldpi (our optimization):   25  ( 0.01%) ← LDPI working
```

**Key insight**: 28% of the binary is PFIX 0 NOP padding from 5-byte local
variable placeholders and 9-byte LDC_ABS. The always-STL strategy (STL+LDL
round-trips) accounts for another ~19% of instructions. Together, ~47% of
generated instructions are overhead.

### Compiler Optimization Status (transputer-upgrades.md)

See `transputer-upgrades.md` for the full optimization plan and status.
Implemented: GTU, ADC, NOT+ADC1, DUP, NORM, BITCNT, BITREV, CSUB0, FMUL,
LDPI, switch threshold, expanded peephole optimizer.
Deferred: Expression depth scheduling (needs TCC core restructuring),
LEND counted loops (needs tccgen.c hooks).

### TVX — MXFP4 / bfloat16 ISA extension (+ TVX-GFX geometry/gaming group)

**The ISA now extends past TVX**: the TVX-GFX group occupies OPR `0x1E2`–`0x1F6`
(transform+project+clip records, sincos/rsqrt/recip, stride/format/light
latches, cull, skinning, format-generic QGEMM, fill/blend/checksum block ops,
STVXSTATE). Spec = `transputer/t4/tvx_gfx.h`; doc, measured results and the
`tvx2` machine profile = **`transputer/TVX-GFX.md`**; tests 09105–09109.
TVX-GFX needs `-mt9000tvx`; only part of TVX-AI has a software version (see
"-mt9000tvx, and objects that know their part").  A kernel built with it
runs `do_csum` on the CSUM instruction (`asm/checksum.h`); the default
kernel uses `lib/checksum.c`'s.

### TVX-AI — MXFP4 / bfloat16 (quant/inference subgroup)

Thirty-four instructions at OPR `0x1C0`–`0x1E1` for low-precision ML
arithmetic, plus three narrow float types in TCC:
- `__bf16` and `__fp16` — **storage-only**, promote to binary32 for
  arithmetic, so `sizeof(a+b) == 4`.  Correct per ARM ACLE / original psABI.
- `_Float16` — a **real C23 Annex H arithmetic type**: operations are
  performed in the type and yield it, so `sizeof(a+b) == 2`.  Emulated by
  widen/compute/`R32TOF16`, rounding after *every* op (strict, = Clang's
  `-ffloat16-excess-precision=none`).
`VT_BTYPE` was widened from 4 to 5 bits (16→32 basic-type slots) to give
`_Float16` a slot distinct from `__fp16`; all modifier/storage flags shifted
up one bit.  Macros: `__FP16__`, `__FLOAT16__`, `__BF16__`.  The `f16`
literal suffix is not implemented.  See `TVX-ISA.md` for the full rationale. Full specification, rationale and compliance
evidence: **`transputer/TVX-ISA.md`**.

- Semantic core: `transputer/t4/tvx.h` — shared by the emulator *and* by
  `tccgen.c` for `__bf16` constant folding, so the two cannot diverge.  The
  compiler carries verbatim copies of it and of `tvx_iq.h`, `tvx_vec.h`,
  `tvx_gguf_tables.h` and `tvx_gguf_decode.h` in `lib/transputer-tvx/`, so
  it builds without transputer/; after changing one in t4 run
  `sh transputer/scripts/sync-tvx-headers.sh` (`--check` only compares).
- Two axes: block instructions (`MXFP4DOT`/`BF16DOT`/`MXFP4GEMV`, 32 or N×32
  elements per dispatch) for the MAC path; sub-word SIMD (`MXFP4UNPK8`,
  `R32TOMXFP4X8`, `BF16MUL2`, `BF16ADD2`, 8 MXFP4 or 2 bf16 lanes per word)
  for conversion/packing. Measured: 36× on quantise, ~4.6× on GEMV.
- Block rotation (`VHAD32`/`MXQROT`/`MXQROTBF`, `0x1DF`–`0x1E1`) implements
  BRQ (arXiv:2511.04214): a 32-point randomised Hadamard *within* each MX
  block. Global rotation (QuaRot/SpinQuant) hurts MXFP4; block rotation
  helps, and because the optimal dimension equals the block size it fits one
  instruction. Weight side unchanged (R folds into W offline) — only
  activations need it. Measured 122× fewer T9000 cycles than a scalar FWHT.
- User header: `transputer/include/tvx.h`.
- Compliance: `transputer/tests/tvx_conformance.c` (host, exhaustive over all
  2^32 binary32 inputs vs an independent spec model), `test_tvx.c` and
  `test_tvx_simd.c` (on-target).
- Real-model GGUF blocks: `tests/gguf/qwen38_blocks.inc` holds 44 entries
  (15 formats, up to three tensors each) from the Qwen3.8-27B IQ1_M, Q5_K_M
  and MTP files in `C:\projects\qwentp`, pulled by
  `gguf/extract_model_blocks.py`; `gguf/qwen38_ref.c` writes the expected
  values (`qwen38_expected.inc`) from llama.cpp's decoders on the host.
  `test_tvx_qwen38.c` checks QDOT on them on every part (the instruction
  at -mt9000tvx, software TVX elsewhere); `test_gguf_decode_parts.c`
  compiles the same decoders (`t4/tvx_gguf_decode.h`, against the libc-free
  `lib/transputer-tvx/include`) for whatever part is selected and checks
  decode and dots bit for bit, so it also covers soft-float on real data.
  Both pass on -mt9000, -mt9000tvx, -mt805, -mt800, and on -mt425/-mt414
  under t4's strict `-s425`/`-s414`.

### -mt9000tvx, and objects that know their part

**TVX is opt-in** (2026-11-03): `-mt9000tvx` is the T9000 with the TVX
extension and the only target that emits the TVX instructions (`__bf16tof32`
.. `__vxfer`, OPR `#1C0`-`#1FA`); it alone predefines `__TVX__` and
`__MXFP4__` (`__BF16__`, `__FP16__` and `__FLOAT16__` stay on every part).
Plain `-mt9000`, still the default, is the T9000 (the RTL core) without
TVX; the T9000's VCP builtins and the T9 core's T&L temps (`__tld` ..
`__fpoly3`) need `-mt9000` or more.  Every target without TVX converts
`__bf16`/`__fp16`/`_Float16` and runs the four 16-bit conversion builtins
through `lib/transputer-f16.c` (bit for bit the TVX instructions; in every
runtime archive), so 16-bit float code compiles everywhere.  The kernel uses
CSUM for `do_csum` only under `__TVX__`, and the TVX tests carry
`Target: -mt9000tvx` (local) or the `tvx` tag (c-testsuite).

**Software TVX** (2026-11-03): without TVX, 21 more TVX builtins compile to
calls into the runtime - the MX/bfloat16 scalar and SIMD ones
(`__bf16pack`, `__bf16unpkh`, `__mxfp4tof32`, `__f32tomxfp4`,
`__mxe8m0tof32`, `__mxe8m0of`, `__mxfp4unpk8`, `__f32tomxfp4x8`,
`__bf16mul2`, `__bf16add2`) and the quantised dots (`__mxfp4dot`,
`__bf16dot`, `__mxfp4gemv`, `__mxfp4gemvbf`, `__q8dot`, `__q8dotbf`,
`__iq1mdot`, `__iq2xxsdot`, `__iq3xxsdot`, `__qsetfmt`, `__qdot`).
`tp_soft_tvx_descs` in `transputer-gen.c` lists them, with their parameter
types (each argument is cast, then `__tvx_<name>` is called);
`lib/transputer-tvx.c`, `-tvx-iq.c` and `-tvx-qdot.c` implement them by
repeating t4's p.c code for each instruction on the vendored headers, with the
same float operations in the same order.  They are three objects so that
`__bf16add2` does not pull in 55 KB of GGUF codebooks; they need no libc
(built `-nostdinc` against `lib/transputer-tvx/include`), and the i-quant
and QDOT helpers put about 1.3 KB on the caller's workspace (a decoded block).
The QSETFMT latch is one static per program.  The rest - vector kernels
(`__vsetlen` ..), block rotation, `__qgemm`/`__vq8k`/`__vq80`/`__vsetfmt`,
`__vcvt`/`__vxfer`, TVX-GFX, CSUM, `__stvxstate` - still errors without
TVX.  `test_tvx_soft.c` (no Target line) runs every covered builtin over
real blocks and LCG data and must print the same hashes on every part,
which proves software TVX gives the instructions' bits; the one allowed
difference is a NaN's payload, which the soft-float helpers and t4's host
FPU need not agree on, so the test keeps float arithmetic away from NaNs.
`test_tvx.c`, `_allformats`, `_deepseek`, `_iquants`, `_q8` and `_simd`
also pass without TVX (on -mt9000, -mt805, -mt414) with their Target line
removed; they keep it so the default run still exercises the instructions.

**Objects record their part, and a link for a part refuses anything
bigger.**  A transputer object's ELF `e_flags` holds what the part it was
compiled or assembled for has beyond the T414 - `TP_EF_T800OPS` 1,
`TP_EF_T805OPS` 2, `TP_EF_FPU` 4, `TP_EF_T9000` 8, `TP_EF_TVX` 16 - so
`-mt414` 0, `-mt425` 3, `-mt800` 5, `-mt805` 7, `-mt9000` #f, `-mt9000tvx`
#1f.  A link given a `-mt` option refuses any object, or archive member it
pulls, that needs more:

    p9k.o: error: this object (compiled for -mt9000) needs T805 instructions
    and T9000 instructions, which -mt800 does not have; rebuild it for
    -mt800 or link for a bigger part

A link without `-m` takes anything.  An output's `e_flags` (object, `-r`
link or image) is the union of what is in it.  Objects from before this
carry 0 and pass everywhere, so rebuild prebuilt libraries (uClibc's
`libc.a`) before relying on the check.  The runtime to link is the one
built for the part: `transputer-libtcc1.a` (`-mt9000`, also right for
`-mt9000tvx`), `-t800.a` (`-mt800`, `-mt805`), `-t425.a`, `-t414.a`; both
runners and CoreMark's `build.sh` pick it from the target.

### -mt805 and -mt800: the IMS T805 and the original T800

Both have the T800's floating-point unit.  `-mt805` is the T805, and is
what `-mt800` meant until 2026-11-03: its code is unchanged, byte for byte
(c-testsuite, Doom and fbwm).  `-mt800` is now the original T800, which
lacks what the T805 shares with the T425: Norcroft's `xpuops.h` marks
`pop`, the timer enables, `ldmemstartval` and the j0 break group "T425
only", and `lddevid` and `start` go with them.  The T800 does have `dup`,
`wsubdb`, the `move2d` group, `crcword`/`crcbyte`, `bitcnt` and the bit
reversals: `xpuops.h` (1990) is written against the T414, so its "Only on
T800" means "not on the T414", and "Not on T800" marks the T414's
floating-point support (`unpacksn`, ...).  In practice `-mt800` code
differs only at `x * 0` (`ldc 0; and` rather than `pop; ldc 0`), and
`__lddevid` is an error; no object in that corpus changes.

- That the T805 has the T425's additions comes from INMOS's T805 data
  sheet, which is not in this tree: `xpuops.h` never names the T805 and
  the TCWG predates it.  `-mt800` code runs on either part.
- **Runtime**: link `transputer-libtcc1-t800.a` (built with the others by
  `make transputer-libtcc1.a`; valid for both parts).  The default
  `transputer-libtcc1.a` is `-mt9000` code - `gtu`, `ls`/`lsx`/`ss`/`lbx`
  in the overflow builtins and atomics, T9000 FPU instructions in the
  float/long long conversions - which t4's lenient `-s8` runs anyway, so
  every `-mt800` program had been linking it unnoticed.  A link with
  `-mt800`/`-mt805` now refuses it.
- Check a binary with `t425-census.py --t800` / `--t805`.  t4 has no
  strict T800/T805 mode; `-s8` runs everything.
- Measured 2026-11-03: c-testsuite 241/241 at both with the T800 runtime
  (8 skipped: the 3 environment skips and the 5 `tvx` tests; `00207` has
  passed since the VLA arena); CoreMark (`CPU=t805`/`t800` in its
  `build.sh`, which now links that runtime and keeps a census) validates,
  85-91 iterations/s.

### -mt425 and -mt414: the FPU-less IMS T425 and T414

`-mt425` targets the T425: the T805's integer instructions, no FPU and none
of the T9000's.  `-mt414` targets the T414, which also lacks what the T425
added (`dup`, `wsubdb`, `pop`, the `move2d` group, `crcword`/`crcbyte`,
`bitcnt`, the bit reversals, `lddevid`, ...).  Like `-mt800`/`-mt9000` the
last `-m` option wins; `-mt9000` stays the default, and `-mt9000`/`-mt800`
output is unchanged by either (c-testsuite, Doom and fbwm objects
byte-identical).

- **Soft float.** Every float/double operation becomes a call to a
  libgcc-named helper (`__adddf3`, `__ltsf2`, `__floatsidf`, `__fixdfsi`,
  `__truncdfsf2`, ...) in `lib/transputer-softfp.c`: IEEE 754
  round-to-nearest-even with subnormals, signed zeros, infinities and NaNs,
  no exception flags.  Floats already live in workspace words and a double
  is passed and returned like a long long, so they are ordinary calls
  (`tp_softfp_*` in `transputer-gen.c`).  `__fpsqrt` calls the correctly
  rounded `__sqrtsf2`/`__sqrtdf2`, `__fpabs` clears the sign bit inline, and
  `__bf16`/`__fp16`/`_Float16` convert through `__extendhfsf2`,
  `__truncsfhf2` and `__truncsfbf2` (bfloat16 widens with a shift), bit for
  bit as the TVX instructions.  The file is plain C, checked on the host:
  2x10^8 random and special-value checks of the arithmetic against x86-64
  SSE, square roots against libm, and the 16-bit conversions against
  `lib/transputer-tvx/tvx.h` for all 2^32 inputs - no differences.  Most
  single-length helpers and the double arithmetic run in assembly instead
  (see "The FP-support instructions" below), with this C as their slow
  path.
- **Runtime.** `make transputer-libtcc1.a` also builds
  `transputer-libtcc1-t425.a` and `-t414.a`: the same runtime compiled for
  that part, plus the soft-float helpers.  Link the matching one instead.
- `__SOFTFP__` is predefined; `tests/test_boot.S` uses it to leave out
  `fptesterr`.
- **`gtu` (opr 0x5F) is a T9000 instruction** (Norcroft `xpuops.h`, the
  TCWG has none) although t4 accepts it in T800 mode.  Only `-mt9000`
  emits it (`TP_T9K_MASK` bit 64); every other target compares unsigned
  values with `mint; xor` on both operands and `gt` (2026-11-03: 145 sites
  in the -mt800 corpus, +0.4% code).
- `-mt414` replaces `dup` by reloading the operand (or by `ldc n; shl` and
  `ldc 3; prod` for the `dup; sum` multiplies), `pop` by `ldc 0; and`, and
  `wsubdb` by the generic multiply; `__builtin_popcount`/`__bitcnt` call
  `lib/builtin.c`.
- Rejected with an error: the T9000 builtins and the TVX builtins without
  a software version under both (the four 16-bit float conversions and the
  21 builtins software TVX covers become calls, as on every part without
  TVX - see "Software TVX"), and under `-mt414` also `__move2d*`,
  `__crcbyte`/`__crcword`, `__bitrev*` and `__lddevid`.  Software TVX
  started as a "no" (it carries t4's TVX semantics, llama.cpp tables
  included, in step with a growing ISA) and was done for the quant group
  on the user's request the same day; the vector kernels are left, and
  SiLU/softmax there use the host's exp(), so a software version cannot be
  bit-exact.
- **t4 `-s425` / `-s414`** (t4 repo) model those parts strictly: any
  secondary instruction the part lacks - FPU, T9000, TVX, and on the T414
  the T425's additions - stops the run with `-E-EMU414: Error - #5A (DUP)
  is not a T414 instruction`.  Instruction fusing is off in these modes, so
  a fused pair cannot hide one.  `-s4` still runs everything (and, with
  the T414's MemStart, boots only `-mt414` images).  `-s414` has the T414's
  2 KB of on-chip RAM and MemStart #80000048: `-mt414` on the link command
  links there instead of #80000070, and transputer-netload accepts either
  base.  Runner knob: `T4_CPU=-s425` (both runners; each also passes its
  `-m` flags to the link).  The in-tree `transputer/t4/t4` is built from
  t4's committed HEAD, the binary before it kept as `t4.pre-82bb587`.
- **Check a binary**: `python3 transputer/scripts/t425-census.py [-v]
  [--hist] [--t414] file.elf|file.o ...` lists every instruction outside
  the T425 (or T414) set with the function it is in (a linked image has no
  section headers, so it reads the linker's `file.elf.tccsym` to find the
  code) and, with `--hist`, the code size and instruction histogram.
- **Tests**: `test_softfloat.c` hashes float/double arithmetic, relations
  and conversions over random bit patterns against hashes from an x86-64
  host, and `test_unsigned_cmp.c` the unsigned relations; both pass at
  every target.  Both runners: `T4_CPU=-s425 TP_EXTRA_CFLAGS=-mt425` (they
  link `transputer-libtcc1-t425.a` by themselves).
  On the strict emulator (2026-11-03, on 258250e9 with t4 351dd89):
  c-testsuite 241/241 at both - the TVX tests are skipped (they need
  -mt9000tvx), and at -mt414 so is `09103` (`__move2dinit`, tagged
  `move2d`); `00207` passes since the arena (see "Variable-length
  arrays").  Local tests, now gated by `Target:`/`Needs:`: 202 pass, 0
  fail, 64 skipped at -mt425; 199 pass, 2 fail, 65 skipped at -mt414.
  The two -mt414 failures predate the soft-float work:
  `test_norm_bitcnt` uses `__bitrevword`, which -mt414 rejects (no
  `Needs:` tag yet means "not the T414" alone), and `test_regtest_ops`,
  whose hand-written assembly runs `pop`.
- **CoreMark**: `CPU=t425 bash transputer/coremark/transputer/build.sh`
  (or `CPU=t414`) writes `coremark-$CPU.btl` and beside it the ELF, its
  `.elf.tccsym` symbol map, `.build.txt` (every command, with the compiler
  revision) and `.census.txt`.  The default iterations are per CPU, enough
  for CoreMark's 10 s minimum on today's t4.  On t4 (2026-11-03) all
  validate: -mt9000 about 145-173 iterations/s (2400 by default), -mt425
  and -mt414 65-80/s under `-s8` and about 40/s under the strict
  `-s425`/`-s414`, which turn instruction fusing off.  The CoreMark image
  has about 38.5 KB of code at -mt425 or -mt414.
- **The FP-support instructions** (2026-11-03).  The T414 and T425 both
  have `unpacksn` (#63), `postnormsn` (#6C), `roundsn` (#6D), `ldinf`
  (#71) and `cflerr` (#73) (TCWG 7.11, D.4; t4 `-s425`/`-s414` run them),
  and `lib/transputer-softfp-sn.S` is single precision on them the way
  INMOS's T414 REAL32 runtime is (`t4/examples/sqrroots/sqrroots.b4h`):
  `unpacksn` gives the fraction with its leading 1 at bit 31 and the biased
  exponent, `norm; postnormsn; roundsn` packs B:A with W[0] = exponent,
  rounding once; multiply is `lmul`, divide `ldiv` of (fa/2 : 0) by fb with
  the remainder as the guard word, add/subtract `lshr` alignment and
  `sum`/`ldiff`, all exact.  It has `__addsf3`, `__subsf3`, `__mulsf3`,
  `__divsf3`, the seven relations (`ldinf` for the NaN test), float to and
  from int, unsigned and double, and double to and from int and unsigned.
  Each does finite operands itself and jumps to the C (renamed
  `__tp_addsf3` etc. on the transputer by `SN()`) for NaNs, infinities,
  zeros and out-of-range values; divide sends denormals there too.  No
  `cflerr`: that is occam's error flag, and C wants non-stop IEEE.
  - The assembler gives a branch to a label not yet seen a 9-byte
    relocated form, and link relaxation leaves assembler code alone, so
    every hot-path branch goes back to a block placed before its function,
    `cj .Lx - .Lbase`, a 1-2 byte constant.
  - **Checked**: `tests/test_softfp_sn.c` (`Needs: softfp`: runs only at
    -mt425/-mt414) compares every helper with its C original bit for bit;
    10^6 iterations (`-DSN_N=1000000`, about 42 million comparisons) agree
    on -s425 and -s414, and it caught all 22 mutations of the assembly
    that change a result (two more, a later cut-off for alignment and
    E = 158 going to the C, are still correct).  The C stays the
    host-checked reference (2x10^8 SSE checks).
  - **Speed** (`tests/bench/softfp.sh`, estimated T414 cycles per element,
    loop included, from t4's instruction counts weighted by the TCWG
    timings): add 1444 -> 294 (x4.9), mul 1252 -> 248 (x5.1), div 3160 ->
    270 (x11.7), `<` 300 -> 204, int to float 847 -> 161, float to double
    1120 -> 207, double to float 967 -> 225, a degree-6 Horner polynomial
    x6.4, a dot product x6.9; t4 instruction counts fall by similar
    factors.  +1 KB of runtime when floats are used.
  - **Doubles** (`lib/transputer-softfp-db.S`): `__adddf3`, `__subdf3`,
    `__muldf3` and `__divdf3` the same way on the long arithmetic, with
    the rounding (`PACK`) by hand, since `roundsn` is single-length only:
    multiply is four `lmul` into 128 bits; divide is Knuth's algorithm D
    with two 32-bit `ldiv` digits, each corrected by adding the divisor
    back; add is the C's own algorithm (fractions at bit 62, the shifted
    one's lost bits ORed into bit 0).  Only finite normal operands with a
    normal result stay in the assembly: zeros, denormals, infinities,
    NaNs, overflow, underflow, x - x and a quotient digit `ldiv` cannot
    estimate (top words equal) are redone in the C.  Checked by the same
    test, with ordinary, nearby and sparse operands (one to three fraction
    bits, for the exact, half-way and zero-word cases), 10^6 iterations on
    -s425 and -s414, and mutations (19 caught; at most one second-digit
    division correction instead of a loop changes about one quotient in
    10^5 and is not).  Speed, as above: add 1975 -> 648 (x3.0), mul 2506
    -> 637 (x3.9), div 13706 -> 852 (x16.1), a degree-6 polynomial x4.0,
    a dot product x3.7.  +1 KB more of runtime when doubles are used.
  - **Double relations** (2026-11-04, same file): `__eqdf2` ...
    `__unorddf2`, branch-free past the NaN test (`(|hi| | (lo != 0))`
    above #7FF00000): the bit patterns compare unsigned with `ldiff`'s
    borrow, the answer negated when either operand is negative, and two
    zeros forced equal.  All 11 mutations caught.  `a < b ? a : b` on
    doubles, per element with its loop: 439 -> 349 (x1.26; the C was
    already short).  +183 bytes.  Everything the soft-float runtime
    calls for float and double arithmetic, relations and int/float
    conversions is now assembly; still C: negation (a sign flip), the
    long long conversions, the square roots and the 16-bit floats.

### Variable-length arrays: a per-process arena (2026-11-03)

TCC's VLA model is a frame pointer plus a stack pointer that the allocation
moves.  The transputer has only `WPtr`, which is both: every local is an
`ldl`/`stl` at a fixed offset from it.  Until 2026-11-03 `gen_vla_alloc`
lowered `WPtr` with `gajw`, so every later local access re-based (the saved
`WPtr` was read back from the wrong slot) and the array landed on top of
locals 0..N; `00207` hung.  Now **`WPtr` never moves**.

- **Codegen** (`gen_vla_*` in `transputer-gen.c`): the stack-pointer save,
  adjust and restore tccgen asks for become calls on an *arena* top -
  `__tp_vla_mark()` before a scope's first VLA, `__tp_vla_alloc(size,
  align)` (base in A, new top in B, both captured by the old-style call),
  `__tp_vla_release(saved)` at scope exit, `goto`, `break`/`continue`,
  `return` and labels.  The arena grows *upward*, so the base is not the
  saved top: `tccgen.c` keeps it in a slot of its own through
  `gen_vla_result`, as it does for Win64 (a target-list addition).  Being
  ordinary `gfunc_call`s, the restores spill whatever the vstack holds -
  the return value, a statement expression's value - first.
- **Whose arena** (`lib/transputer-vla.c`, `lib/transputer-conc.c`):
  processes share globals and no instruction names the running process,
  so the only thing a process owns is `WPtr`, inside its workspace.
  `PSetup` now starts every process in `__tp_proc_start(func, ws)`, entered
  by `runp` with its two parameters written where a CALL would have spilled
  them.  Its frame, at the top of the workspace, holds the process's record
  (workspace range plus arena) for as long as it runs, linked into a list
  per priority; `PStop()` (now a call, `__tp_proc_stop`) or returning from
  `func` unlinks it.  A VLA operation finds the innermost record whose
  workspace holds `WPtr`.  The process's arena is **the bottom of its own
  workspace, growing up toward its stack**; the allocator keeps the top
  128 bytes below the current `WPtr`.  Anything not started by `PSetup` -
  the main program, a process built by hand with `__runp`/`__startp` -
  uses one static 16 KiB arena, so at most one of those may hold VLAs at a
  time.  `ProcVLAArena(buf, size)` (conc.h) gives the calling process a
  buffer instead.  Running out sets the error flag and stops the process
  (`seterr`, then `PStop`) - never an overlap.
- **Why that is safe without atomics**: a low-priority process is only
  descheduled at `j`/`lend` (or when it blocks), a high-priority one only
  when it blocks, and a process only ever touches the list of its own
  priority.  So code with no `j` or `lend` is atomic against every process
  that can touch the same list.  Linking, unlinking and the search are all
  written that way - the search is a do-while, whose back edge is a `cj`
  (see "loops do not timeslice"), and returns a cast, not `?:`, which
  compiles to `j`s.  A generation count restarts a search if an unlink
  ever does slip in.  `PStop` has no timeslice point before its `stopp`,
  as when it *was* a bare `stopp`: a parent that reuses a workspace on
  receiving the process's last message must not catch the process still
  running in it.  `python3 transputer/scripts/tpdis.py conc.o
  __tp_proc_arena __tp_proc_stop` should show one `j`, the restart.
- **Cost**: VLA-free code is byte-identical.  A VLA operation's helper chain
  needs about 51 words of workspace below the caller (81 if it fails); the
  start frame is 30 words and `PStop` 42, so `PROC_WS_MIN` went from 256 to
  512 bytes.  A program with VLAs links `transputer-vla.o` (16 KiB BSS) and
  `transputer-conc.o`; one with only `PSetup` does not get the arena.
- **Not handled**: `longjmp` out of a VLA scope keeps its arrays until a
  VLA scope enclosing the target ends.  Processes not started by `PSetup`
  share the main arena (above).  A process that ends with a bare
  `__stopp()` or blocks forever stays on the list, so its workspace must
  not become another process's stack.
- **Fixed with it**: (1) a `goto` released only the innermost VLA scope,
  leaving the rest to an epilogue that resets the stack pointer; with an
  arena that leaked for good, so on the transputer `goto` releases every
  live VLA and the label re-establishes those live at the target.
  (2) A statement expression's value could still be an lvalue in the VLA it
  had just released; `prev_scope` loads it first.  (3) Indexing a 2-D VLA
  (`int m[r][c]`, or a pointer to a VLA row) took the `wsub` fast path with
  a row size of 4, the static size of a VLA type, so `m[1]` was 4 bytes
  past `m[0]`; VLA element types now take the generic multiply.  All three
  are fenced in `tccgen.c`.
- The old "VLA + statement expression vstack leak" (`char sym[max(...)]` in
  vsprintf, `min()`/`max()` in VLA sizes) no longer reproduces at compile
  time; those now compile and run correctly.
- **Tests**: c-testsuite `00207`, and `transputer/tests/test_vla.c`: locals
  and stack parameters next to a VLA, loops, nested scopes, `goto` out and
  back, labels after a VLA, `return`/`break`/`continue`/`switch` out of VLA
  scopes, recursion, callers' and callees' VLAs, 2-D VLAs and VLA
  parameters, statement expressions, `__tp_vla_mark()` unchanged after
  each, then two low-priority processes passing a token while each holds a
  VLA, a high-priority one blocking inside a VLA scope, workspaces reused
  after `PStop` and after `func` returns, and a process that runs out.  It
  links the two lib files itself (`EXTRA_SOURCES`) and passes at -mt9000,
  -mt805, -mt800, -mt425 and -mt414; with every process on one arena, the
  process checks fail.
- **Validated** (2026-11-03, on d732658b): c-testsuite 241/241 at
  -mt9000, -mt805 and -mt800, 241/241 and 240/241 on t4 -s425/-s414
  (`09103`, a rejected builtin; skips counted as passes - since 2f2c61fc
  `09103` is skipped at -mt414 by its `move2d` tag, so -s414 is 241/241
  too); local tests 209
  pass, none fail; of all c-testsuite objects only `00207.o` changes
  (-mt9000, -mt805, -mt800), and Doom/fbwm objects are identical at
  -mt9000 and -mt800.

### Transputer Builtins Available

```c
/* Bit manipulation (T800 hardware instructions) */
unsigned int __norm(unsigned int x);        /* Leading zeros (0-64) */
unsigned int __bitcnt(unsigned int x);      /* Population count */
unsigned int __bitrevword(unsigned int x);  /* Reverse all 32 bits */
unsigned int __bitrevnbits(unsigned int x, unsigned int n); /* Reverse N bits */
unsigned int __csub0(unsigned int idx, unsigned int bound); /* Bounds check */
int __fmul(int a, int b);                  /* Fractional multiply (a*b)>>31 */

/* FPU square root / absolute value.  Both work on float and double (the FPU
 * takes its width from the loaded value).  -mt9000 emits a single secondary
 * (fpsqrt 0xD3 / fpabs 0xDB); -mt800 emits the documented fpentry sequences.
 * Not spelled sqrtf()/fabsf(): sqrt() owes errno + a domain error that the
 * bare instruction does not provide.
 * NOTE: on T9000 `1.0f/__fpsqrt(x)` measured 17% FASTER than the Quake
 * 0x5f3759df rsqrt bit-hack (idMath::RSqrt) and is exact - the hack is a
 * pessimisation here. */
float  __fpsqrt(float x);   double __fpsqrt(double x);
float  __fpabs(float x);    double __fpabs(double x);
/* The T800 square root is first, steps, last: two steps for single length
 * and five for double (TCWG 7.5.4).  Until 2026-11-03 -mt800/-mt805 always
 * emitted two, which t4 hides (it computes the root at fpusqrtlast); a real
 * T800 would have returned a short double root. */

/* Rounding to an integer value, keeping float or double (an int becomes a
 * double): __fpint to nearest (rint), __fpintrp toward +inf (ceil),
 * __fpintrm toward -inf (floor), __fpintrz toward zero (trunc).  The T800
 * rounds the next FPU instruction only, so these are fpurX; fpint; the
 * T9000's modes are sticky, so fprX; fpint; fprn.  The IEEE remainder
 * __fprem(x, y), exact, double unless both are float: fprem on the T9000,
 * the TCWG fpremfirst/fpremstep loop on the T800 (integer stack destroyed).
 * The FP error flag (set by invalid operations, overflow, division by zero
 * and fpint of a non-finite value): __fpseterr/__fpclrerr (fpuseterr/
 * fpuclrerr through fpentry on the T800), __fptesterr (1 if it was clear;
 * clears it), __fpchkerr (copies it into the Error flag - what INMOS occam
 * emits after each REAL expression).  All of these need an FPU: errors at
 * -mt425/-mt414.  Test: tests/test_fpu_ops.c (`Needs: fpu`). */
float __fpint(float x);  float __fpintrp(float x);   /* and double */
float __fpintrm(float x); float __fpintrz(float x);
double __fprem(double x, double y);
void __fpseterr(void);   void __fpclrerr(void);
int  __fptesterr(void);  void __fpchkerr(void);

/* Timer/scheduler */
unsigned int __ldtimer(void);               /* Read T800 timer */
void __sttimer(unsigned int val);           /* Set timer */
unsigned int __mint(void);                  /* Load MinInt (0x80000000) */

/* Channel I/O */
void __in(int len, const void *chan, void *buf);   /* Channel input */
void __out(int len, const void *chan, const void *buf); /* Channel output */
void __outbyte(const void *chan, int byte);
void __outword(const void *chan, int word);

/* Process management.
 * __startp's `entry` is an offset relative to the NEXT IPtr, not an address,
 * and it schedules immediately - so it cannot launch a function by pointer.
 * To do that, write the entry address into the Iptr slot at Wptr[-1] and
 * hand the descriptor to __runp; see PSetup() in lib/transputer-conc.c. */
void __startp(void *wptr, int entry_offset); /* Start process */
void __endp(void *successor);               /* Join at successor workspace */
void __runp(int descriptor);                /* Schedule Wptr|priority */
void __stopp(void);                         /* Stop process */

/* 2D block operations (T800) */
void __move2dinit(int src_stride, int dst_stride, int width);
void __move2dall(void *src, void *dst, int width);
void __move2dnonzero(void *src, void *dst, int width);
void __move2dzero(void *src, void *dst, int width);

/* CRC (T800 hardware) */
unsigned int __crcword(unsigned int data, unsigned int crc, unsigned int poly);
unsigned int __crcbyte(unsigned int data, unsigned int crc, unsigned int poly);

/* Error/scheduler control */
void __seterr(void);     int  __testerr(void);
void __sethalterr(void); void __clrhalterr(void);
int  __testhalterr(void);                   /* the HaltOnError flag */
void __stoperr(void);    /* deschedule for good if the Error flag is set */
int  __testpranal(void); /* 1 if the processor booted after Analyse */

/* ALT.  Operand order is fixed by the hardware (ISM 7.6, and the emulator
 * agrees - transputer/t4/p.c cases 0x2E/0x2F/0x30, 0x47/0x48/0x49).  The
 * disable instructions return whether THIS guard was selected; only the
 * first ready one in scan order reports true, which is the arbitration.
 * `code` is the branch offset an occam compiler would stash in Temp for
 * altend; a C caller passes the guard index and reads the result instead.
 *
 * An ALT sequence must run start-to-finish inside one function body - no
 * call in between - because its state lives in the workspace slots at and
 * below Wptr (Temp=0, State=-3, TLink=-4, Time=-5).
 *
 * See include/conc.h for the wrapped form (ChanAlt/ChanSkipAlt/ChanTimerAlt).
 */
void __alt(void);  void __altwt(void);  void __altend(void);
void __talt(void); void __taltwt(void);
void __enbc(int guard, const void *chan);   /* AReg=guard, BReg=chan */
void __enbt(int guard, unsigned int time);  /* AReg=guard, BReg=time */
void __enbs(int guard);
int  __disc(int code, int guard, const void *chan);
int  __dist(int code, int guard, unsigned int time);
int  __diss(int code, int guard);
int  __resetch(void *chan);   /* -> previous channel word */

/* Scheduler queue registers.  buf[0] receives the queue front pointer,
 * buf[1] the back. */
void __saveh(int *buf); void __savel(int *buf);
void __sthf(void *p);   void __sthb(void *p);
void __stlf(void *p);   void __stlb(void *p);

/* T9000 configuration space and virtual channel processor - include
 * <t9000.h>, which also has T9_VIN(n)/T9_VOUT(n), the register addresses
 * and the set-up helpers t9_vcp_channels / t9_link_virtual / t9_vlink /
 * t9_vcp_start.  The VCP ops are negative secondaries (nfix k; opr x);
 * inline asm accepts them by name (vin, vout, initvlcb, ..., selth, tret). */
unsigned __ldconf(unsigned addr);         void __stconf(unsigned addr, unsigned v);
void __initvlcb(void *vl, void *pktbuf);  void __sethdr(void *vl, unsigned link, unsigned off);
void __writehdr(void *vl, unsigned len, const void *hdr);
void __readhdr(void *vl, unsigned len, void *dst);
void __setchmode(void *chan, unsigned on); void __stopch(void *chan);
char *__swapbfr(void *vl, void *newbuf);
void __vout(unsigned len, void *chan, const void *src);
unsigned __vin(unsigned max, void *chan, void *dst);   /* length received */
unsigned __ldchstatus(void *chan);        unsigned __chantype(void *chan);

/* T9000 semaphores (ISM 8.6) and resources (ISM 8.8) - also in <t9000.h>,
 * with SemWait/SemSignal/SEMAPHOREINIT, t9_rds/t9_reschan, t9_mark,
 * t9_grant, t9_alt (channels and resources in one ALT) and
 * t9_vcp_resources.  Not __wait/__signal: Linux uses those as names, and a
 * builtin token is a builtin wherever it appears. */
void __semwait(void *sem);                void __semsignal(void *sem);
void __mkrc(void *chan, void *rds, unsigned id);   void __unmkrc(void *chan);
void __grant(void *rds, unsigned *id);    /* may deschedule */
void __enbg(int guard, void *rds);        int __disg(int code, int guard, void *rds);
void __irdsq(void *rds, void *front, void *back);  void *__erdsq(void *rds, void **back);
unsigned __ldresptr(void *chan);          void __stresptr(void *chan, unsigned v);
```

Semaphores and resources (2026-11-03, from the T9000 communications
chapter of *Networks, Routers and Transputers*): a semaphore and an RDS are
three words each (count or waiting server, then a queue front and back); an
internal resource channel is three (channel word, rc.Ptr, rc.Id), and rc.Id
is NotProcess while the channel is out of any resource.  A grant takes the
channel out again, so it is re-marked before its next grant (user or
server), and leaves rc.Ptr NotProcess, a channel back to the user.  An
event or virtual input channel can be marked too, on the server's node, but
the VCP keeps its two words at ExternalRCBase (#4110), which software must
point at an area set to NotProcess first (`t9_vcp_resources`) - with the
register left at 0 the marks land in low memory and nothing is granted.
`conc.h` still has no semaphores for the T800, where they cannot be made
atomic.  Gotcha found writing the tests: a channel holds one waiting process,
so several workers reporting on one `done` channel lose each other; signal a
semaphore instead.

Tests: `tests/test_t9000_vcp_builtins.c` (one node, no cable),
`tests/test_t9000_resource.c` (semaphores - mutual exclusion across timer
waits inside the section, a counting semaphore - and resources: user- and
server-marked channels, reply channels, ALTs with grant guards, unmkrc,
ldresptr/stresptr, erdsq/irdsq), `tests/net/vres.sh` (two nodes: a server
granting three virtual links' requests, an ALT over virtual channels, and a
mixed channel/resource ALT),
`tests/net/vlink.sh` (three virtual links over one cable, two nodes),
`tests/net/vlbw.sh` (virtual-link vs byte-stream throughput under the cycle
model), `tests/net/c104.sh` (DeepSeek's 8-node shared-expert FFN over three
IMS C104s as one threaded T4 network, `T4_NODE_BTL` booting every node).
Measured at DS-link speed (9 MB/s): one virtual link takes 1.36x the raw
link time for 24 KB (stop-and-wait per 32-byte packet: ~3 header/ack bytes
plus 2x link latency), three links 1.06x, byte stream 1.00x; at 90 MB/s one
link is 3.43x.  Through a C104 one virtual link per cable gets ~54% of the
link; the C104 FFN takes 5.7 ms (hypercube 6.7 ms).  A node has at most six
virtual links: their channel words end at MemStart (#80000070).  Timing
model: t4 `T9000_CHANNELS.md`, "Network timing".

### Communicate or fail: INMOS technical note 1 (2026-11-03)

`include/conc.h` has the "communicate or fail" procedures of INMOS
technical note 1, *Extraordinary use of transputer links* (72-TCH-001;
TCWG 8.3.1 summarises it): `ChanInTimeFail`/`ChanOutTimeFail`
(InputOrFail.t/OutputOrFail.t: give up at an absolute deadline),
`ChanInChanFail`/`ChanOutChanFail` (InputOrFail.c/OutputOrFail.c: give up
when an int arrives on a kill channel, which is input only if it does
abandon the transfer), and `ChanReinit` (Reinitialise: `resetch`, plus the
Error flag if a process was still waiting on the channel).  They return 0
when the message got through, 1 when it was abandoned (and the channel
reset), -1 for a workspace under `PROC_WS_MIN`.  The TimeFail pair had been
declared but never defined (`REVIEW-afcc4dfe.md` item 23).

- **How** (`lib/transputer-conc.c`, `xfail`): the transfer runs in a
  high-priority helper process in the caller's `ws`; the caller ALTs on the
  helper's completion channel and the timer or kill channel, and on a
  timeout resets the channel - the helper, waiting on it, is then on no
  queue and simply abandoned.  The helper never stops: on completion it
  outputs on a channel nobody inputs, and the caller just resets that word.
  Stopping it would leave a window where the caller has returned (and
  `ws` been reused) while the helper, rescheduled by the very
  communication that told the caller, has not yet run its `stopp` - the
  INMOS code closes that with a PAR's `endp`.  For the same reason it is
  not a `PSetup` process (it would stay on the record list).  The note's
  rule that the transfer must start before the timeout can be taken
  becomes `cancel`/`started` flags, safe because the high-priority helper
  tests `cancel` and starts the transfer with no deschedule between.
- **What the note says, and the test for each** (`tests/net/xlink.sh`,
  13 cases on two threaded nodes, Link2 a second cable for stepping and
  reporting): timed-out and killed inputs and outputs (2-6, including a
  message beating its kill); an ALT with a timer guard cannot rescue a
  stuck input once the first byte is in (8); after an abort the link is
  usable only when both ends have reinitialised both channels, and only
  once activity has stopped (7, after every later case); a one-sided
  failure - the input completes, the output fails - when the link goes
  between the last data and its acknowledge (9: node 1 resets its own link
  engine; t4 does not model the far-end disconnect a link reset causes);
  real link errors from `WrongParity` and `ResetOutput` with the
  LinkStatus bits each end records and `resetch` restarting the link
  (10, 11); reinitialising an input empties the link, so a packet that
  arrived unread is gone (12); a halted link sends nothing, not even an
  output's first packet (13).  `tests/net/xlink_recover.sh` is the note's
  example 2: ticks every 250 ms, one second of silence is a failure, then
  a three-phase cycle agreed at the first synchronisation (reset, timed
  input parallel with timed output, silence) until both succeed, against
  node 0 sending parity errors from 1.5 s to 4 s.  Measured over a dozen
  runs: 6-7 ticks before, one failure, 3-4 recovery cycles, recovered at
  4.4 s, 11 ticks after, in order, on both nodes.  `tests/test_xlink.c`
  (local suite, soft channels, low- and high-priority callers) passes at
  -mt9000, -mt800, -mt425 (`-s425`) and -mt414 (`-s414`).
- **DS-link registers** for C: `T9_CR_LINKMODE/LINKCOMMAND/LINKSTATUS(l)`
  and their bits in `<t9000.h>` (HRM 13.5-13.7).  Set LinkNMode's
  LocalizeError (an "unreliable" link, HRM 13.5.2) on a link you mean to
  recover.
- **t4 needed two fixes for any of this**, found here and fixed in t4
  351dd89 (`tests/test_timer_alt.c`, one part per fix); the binary
  before them is kept as `transputer/t4/t4.pre-351dd89`.
  (1) `dist` never took the process off the timer list (ISM: "If the
  process is still on the timer list, it is removed"); the purge in
  `schedule()` removed the *running* process instead.  A timer ALT won by
  a channel stayed on the list, and the next timer ALT from the same frame
  linked it to itself and t4 spun: a TimeFail that completes hung the
  next one.
  (2) When the timer list woke a `taltwt`, the ALT state stayed
  `Waiting.p`, so a channel ready before the process ran scheduled it a
  second time.  With both fixed all three tests pass; without them
  `test_xlink` hangs at its third case.  Case 9 also needs link outputs
  that complete only on the acknowledge (t4 7cb262c); before it t4
  completed them once the last packet was in the far mailbox, so 9
  failed there and nothing else did.
- **Three more t4 gaps, fixed 2026-11-03** in t4 067bbc9 (cases 12 and
  13 and `tests/test_timer_alt.c` part C fail without it; the in-tree
  `transputer/t4/t4` is built from it, the binary before kept as
  `t4.pre-067bbc9`): `resetch` on a link input now empties the link's buffer
  (HRM 13.7.2, TCWG 8.3: it resets the link hardware) - it reset only the
  channel, and the next input took the packet that had arrived; an `out`
  on a halted link no longer sends its first packet; and `taltwt` with
  its earliest time already past carries on (ISM, TCWG 10.5.2) instead of
  going on the timer list, and with no timer guard enabled waits off it.
  Together the first two had let data sent while a link was down turn up
  after a resync.  The recovery protocol still ignores words it did not
  expect, which is good practice on real links: before an end
  reinitialises, its link can still hold what was sent before a failure.
  Build a t4 for these tests with `make -f Makefile.linux` (as `build.sh`
  does): the default Makefile's nanomsg build fails the two-process
  scripts (vlink, vres, ...).

## References
- `transputer/searchable-tis-tcwg.txt` — TCWG calling convention & optimization guide
- `transputer-upgrades.md` — Compiler optimization plan & status
- `transputer-readme.md` — Standalone build instructions
- `transputer/TOOLS.md` — Various transputer tools
- `transputer/ttools/dast/dastabs.h` — Comprehensive opcode tables
- `transputer/libxputer/` — Channel I/O, process management reference
- TCC internals: `tccgen.c` (generic code gen), `tcc.h` (data structures)

**Missing documents.** `TVX-ISA.md`, `TVX-GFX.md`, `TOOLS.md`,
`transputer-upgrades.md` and `transputer-readme.md` are cited in this file
but exist nowhere: not on disk, and not in the history of the outer repo,
t4 or linux-7 (checked 2026-11-02).  Treat those references as stale until
the documents are recreated.

## Version control

Five git repositories, no submodules:

| Path | Remote | Branch |
|------|--------|--------|
| `.` (compiler, `include/`, `lib/`) | `agentdavo/tinycc-transputer` (public) | `main` -> `origin/transputer` |
| `transputer/` (build.sh, tests, c-testsuite, apps, docs) | same repo as above | same |
| `transputer/t4` | `agentdavo/t4` | `master` |
| `transputer/linux-7` | `agentdavo/linux-transputer` | `master` -> `origin/transputer` |
| `transputer/uClibc-ng`, `transputer/busybox-1.37.0` | none yet (local only) | `master` |

`transputer/.gitignore` keeps the nested repositories, the dead
`linux-2.6.39`, generated headers, build outputs, run artefacts, the
Freedoom WADs and the INMOS manual text dumps out of the outer repo.
`uClibc-ng` and `busybox-1.37.0` were committed as baselines on
2026-11-02: their histories start there, so a diff against upstream needs
the release tarballs.
