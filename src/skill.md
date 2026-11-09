---
name: rewind
description: Find and restore earlier versions of files from the user's restic backups with the rewind CLI. Use when asked to recover a deleted or overwritten file, see how a file looked on an earlier date, or list backups.
---

# Rewind

`rewind` reads the restic repository the user's backups go to
(`~/.config/restic/env`, or `RESTIC_REPOSITORY` and friends when set) and
works on it read-only, except `restore`, which writes beside the original.

```sh
rewind snapshots --json                  # backups, newest first: id, time, host, paths
rewind versions <path> --json            # distinct versions of a file across backups
rewind restore <snapshot|latest> <path>  # restores beside the current file as "name (restored <date>).ext"
rewind restore <snapshot> <path> --replace   # replaces it; the old one is kept in ~/.local/state/rewind/holding
```

- Paths are the file's real location, also for files that are deleted now.
- `versions` lists one row per distinct version with the newest backup that
  holds it; `current: true` marks the version that matches the file on disk.
- Every command exits 0 or 1 and prints errors on stderr.
- Prefer a restore beside the original; only `--replace` when the user asks.
- `rewind <path>` opens the window at that file for the user to look at.
