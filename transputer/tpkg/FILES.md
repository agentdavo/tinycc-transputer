# Files

Open the Files tile on the desktop or press `F` when the terminal is inactive. Use the arrow keys to select an entry and `Enter` to open a directory. `Enter` on a `.tpz` opens Packages with the archive path filled in. Press `Enter` in Packages to install it.

| Key | Action |
| --- | --- |
| `G` | Go to a directory by path. |
| `C` | Copy a regular file, symlink, or folder tree to a destination pathname. |
| `M` | Move an entry to a destination pathname. Across volumes, Files copies the tree before removing the source. |
| `R` | Rename an entry in its current directory. |
| `D` | Delete a file or a whole folder tree after confirming with `Y`. |
| `V` | Preview a regular file. |
| `S` | Mount or unmount the SD volume. |
| `Esc` | Cancel a prompt. |
| `Q` | Return to the desktop. |

Copy, move and rename require an unused destination. Enter an absolute destination or a path relative to the displayed directory.

Recursive copy and cross-volume moves preserve symlink target text exactly, including relative and dangling targets. A special file or nested mount stops the copy and triggers cleanup of the new destination. Recursive deletion removes symlinks themselves and stops at mounted filesystems. Files supports up to 32 folder levels and paths of up to 511 bytes.

`S` toggles the ext2 SD partition at `/dev/t4sd1`, mounted at `/sd`. Wait for **SD unmounted. Safe to remove** before removing it. If the volume is busy, close applications using `/sd` and try again. Install Files and the desktop on the root filesystem so their running program images can stay available while the card is unmounted.

Build Files with `transputer/tpkg/build.sh`, then run `transputer/packages/build-packages.sh tfiles` to produce `tfiles-1.2.1.tpz`. The root filesystem staging script makes the package available under `/opt/packages`.

Use Linux 7 commit `8e3ae258b7b2` or later for symlink operations and SD writeback on unmount.
