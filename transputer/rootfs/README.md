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
