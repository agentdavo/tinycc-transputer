# .tpz Package Format — TransputerOS

## Overview

A `.tpz` file is a **gzip-compressed tar archive** containing installable
files and a metadata manifest. Designed to be minimal, parseable with
BusyBox tar/gzip, and manageable by the `tpkg` package manager.

## Naming Convention

    <name>-<version>.tpz

Examples:
    doom-1.0.0.tpz
    tcc-toolchain-0.9.28.tpz
    fbwm-1.0.0.tpz

## Archive Layout

    .tpz-info               # Package metadata (MUST be first entry)
    usr/bin/doom             # Files relative to / (root filesystem)
    usr/share/doom/...
    etc/...

All file paths are **relative** — no leading `/`. Installation extracts
to the root filesystem: `tar xzf package.tpz -C /`

## .tpz-info Manifest

Plain text, one field per line, `Key: Value` format.
Required fields marked with *.

    Package: doom              *  Unique package name (lowercase, a-z0-9-)
    Version: 1.0.0             *  Semantic version (MAJOR.MINOR.PATCH)
    Description: DOOM for TransputerOS   *  One-line summary
    Size: 990696               *  Installed size in bytes (uncompressed)
    Arch: transputer              Target architecture (always "transputer")
    Depends:                      Comma-separated dependency list (empty = none)
    License: GPL-2.0              SPDX license identifier
    Maintainer: djs               Package maintainer
    Homepage:                     Upstream URL (optional)
    Files: 3                      Number of installed files

### Version Ordering

Versions follow semantic versioning (semver.org):
- MAJOR: breaking changes
- MINOR: new features, backward compatible
- PATCH: bug fixes

Comparison: 1.0.0 < 1.0.1 < 1.1.0 < 2.0.0

## Installation Database

Installed package metadata is stored in:

    /var/lib/tpkg/<package>/info       # Copy of .tpz-info
    /var/lib/tpkg/<package>/files      # List of installed files (one per line)

This allows `tpkg remove` to know which files to delete and
`tpkg list` to enumerate installed packages.

## Operations

    tpkg install <file.tpz>       # Extract to /, record in database
    tpkg remove <package>         # Delete files, remove from database
    tpkg list                     # List installed packages
    tpkg info <package>           # Show package details
    tpkg files <package>          # List files owned by package

## Compression

gzip level 6 (default). The archive MUST be extractable by:

    tar xzf package.tpz -C /

BusyBox tar with CONFIG_FEATURE_TAR_AUTODETECT or explicit `z` flag.

## Constraints

- No pre/post install scripts (security, simplicity)
- No circular dependencies
- No file conflicts between packages (undefined behavior)
- Max package name length: 31 characters
- Max version length: 15 characters
