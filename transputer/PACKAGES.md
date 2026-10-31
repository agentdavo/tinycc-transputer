# TransputerOS packages (.tpz) and boot layout

## How .tpz works (from TPZ-FORMAT.md)

A **`.tpz`** is a **gzip-compressed tar** with:

1. **`.tpz-info` first** — plain-text manifest (`Key: Value`)
2. **File paths relative to `/`** — e.g. `usr/bin/fbwm`, not `/usr/bin/fbwm`

```
.tpz-info
usr/bin/fbwm
usr/share/doc/fbwm/README
```

Install (either way):

```sh
# Manual (BusyBox tar/gzip)
tar xzf package.tpz -C /

# Package manager
tpkg install package.tpz
```

`tpkg` records:

```
/var/lib/tpkg/<name>/info     # copy of .tpz-info
/var/lib/tpkg/<name>/files    # file list for remove
```

Constraints: no scripts, no circular deps, names ≤31 chars, versions ≤15.

## How packages are built (`packages/build-packages.sh`)

```bash
bash transputer/packages/build-packages.sh [fbwm|doom|doom-engine|tpkg|all]
```

`make_tpz()` stages a tree, writes `.tpz-info`, tars with **manifest first**.

| Package | Contents | Typical size |
|---------|----------|--------------|
| `fbwm-1.0.0.tpz` | `/usr/bin/fbwm` | ~28 KB |
| `doom-engine-1.0.0.tpz` | `/usr/games/doom` (no WAD) | ~220 KB |
| `doom-1.0.0.tpz` | engine + freedoom1.wad | ~11 MB |
| `tpkg`, `ttop`, `tfiles`, `coremark` | tools | small |

## Rootfs vs packages

| Layer | What goes here |
|-------|----------------|
| **Initramfs / rootfs stage** | Boot-critical: `init`, BusyBox, `tpsh`, **`fbwm`**, optional doom **engine** |
| **`/opt/packages/*.tpz`** | Optional installs (≤~1.5 MB each staged) for `tpkg install` later |
| **SD (`/sd/...`)** | Large data (full doom WAD), user home |

`rootfs/stage.sh` stages small `.tpz` into `/opt/packages/` and always tries to put **`/usr/bin/fbwm`** when `initramfs/fbwm.flt` exists.

## Boot session order (PID 1)

```
/init
  → echo smoke test (BusyBox)
  → exec /usr/bin/fbwm     # GUI: splash → login → desktop
  → else exec /bin/tpsh
  → else exec /bin/sh      # BusyBox hush
```

**fbwm keys:** any key advances splash/login · `t` = GUI terminal · `s` = spawn shell · ESC leaves desktop.

## Rebuild chain for GUI boot

```bash
bash transputer/fbwm/build-fbwm.sh
bash transputer/packages/build-packages.sh fbwm
bash transputer/build.sh --initramfs
bash transputer/rootfs/stage.sh
# force new CPIO into kernel:
rm -f transputer/linux-2.6.39/usr/initramfs_data.* transputer/linux-2.6.39/vmlinux
bash transputer/build.sh --kernel
bash transputer/build.sh --btl
# Need a display for fbwm pixels:
T4_HEADLESS=0 DISPLAY=:0 bash transputer/build.sh --boot
# Headless still reaches fbwm (UART markers FBWM_START / SPLASH / …)
T4_HEADLESS=1 bash transputer/build.sh --boot
```
