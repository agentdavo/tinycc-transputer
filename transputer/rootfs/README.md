# TransputerOS Root Filesystem

Custom root filesystem for TransputerOS (tpos) — a Linux-based OS
for the INMOS T800 transputer.

## Boot Chain

```
kernel → /init (bFLT) → fbwm (GUI) → tpsh (shell)
```

## Build

```bash
bash transputer/rootfs/stage.sh
```

## Package Format

`.tpz` packages (gzip'd tar with manifest).
Install with `tpkg install package.tpz`.

Set `TPOS_SLIM_PACKAGES=1` to omit the Doom engine and CoreMark package
archives when their executable is already staged. For offline reinstall,
keep the archives on the SD image and pass their paths to `tpkg install`.
