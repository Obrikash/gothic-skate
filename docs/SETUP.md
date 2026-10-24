# Preparing dependencies and backups

The current machine has completed these steps. This document records the procedure for another installation; the checked executable hash and snapshot name in the installer must be reviewed for a different game build.

1. Close Archolos. Confirm the installed executable is the supported Gothic II 2.6 (fix) x86 build. Keep the existing executable.
2. Create `.modding/archolos-free-aim/` next to this repository, with `downloads/`, `staging/`, and `backups/` subdirectories.
3. Back up `system/`, `Saves/`, `Saves_TheChroniclesOfMyrtana/`, `vdfs.cfg` and `Data/SystemPack.vdf`. The current snapshot is `backups/before-union-20261024/`, with `backup-manifest.json` recording each relative path, size and SHA-256 hash. Do not overwrite an existing snapshot.
4. Download `union/Union 1.0m.7z` from Gratt's `gothic-fix-archive` at commit `d61fd89478de5817225d08675a7cdc478a445896`. Verify the archive SHA-256 is `9A5FDFF7C1E8D33C0E6B9051375F2569A352B1AAC835040B6EF81CB7F92CF308`, then extract to `staging/union/`.
5. In Steam's console, run `workshop_download_item 39510 3749987428`. This downloads the author's Workshop package. Copy its `Data/UnionFreeAim.vdf` to `staging/plugin/UnionFreeAim.vdf` and compare its hash against `dependencies.json`. A newer Workshop package needs deliberate manifest review.
6. Run `manage.ps1 validate`, then `manage.ps1 install`.
7. Launch Archolos through its existing Steam entry and perform the checks in `VERIFICATION.md`.

The installed payload comprises six files from the Union archive's `System/` directory, `Data/Union.vdf`, and `Data/Plugins/UnionFreeAim.vdf`. The old `Data/SystemPack.vdf` is parked outside the mounted Data directory. Optional Bink/Miles replacements are excluded.

Do not commit the game, original backup, downloaded binaries, upstream source checkout, or full game INI to Git. The source URLs, version pins and hashes are enough for this integration repository.
