# tinycc_transputer

## Build artifacts and disk space

Disk space is short. On 2026-11-04 C: had 6.6 GB free, and the projects
under `C:\projects` held about 505 GB. A session checks this repository for
output it can remove when it starts, and again before it finishes a task
that built or ran something. It looks at:
- build and output folders, temporary files, and anything
  `git status --ignored --short` lists;
- worktrees: remove one with `git worktree remove` once its branch is
  merged or dropped.

`C:\projects\disk-report.ps1 -Root <this folder>` lists sizes, large files
and build folders without changing anything.

Remove only what can be rebuilt or is no longer needed. Never remove
another session's work in progress, an uncommitted change, or results kept
as evidence. Before removing a folder, check that no running process uses
it. If you are unsure, list the folders with their sizes and ask the lead.
Say what you found and what you removed, with sizes. Files inside WSL count
too, because WSL's disk image on C: grows but never shrinks by itself.

Here that means `logs/` and build output. Keep `transputer/linux-7/`,
including its git history.
