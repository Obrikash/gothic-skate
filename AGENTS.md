# better-aim

Read `docs/MODLOG.md` and `docs/VERIFICATION.md` before changing the game setup.

- This repository lives directly inside the Archolos game directory. The parent directory is the game installation, not a Git working tree.
- Keep only our integration code, presets, dependency metadata and documentation in Git. Game files, saves, extracted assets, downloaded upstream source, binaries and local backups belong in the sibling `.modding/archolos-free-aim/` directory.
- The user approved installing Union + Union Free Aim and a brief interactive game test on 2026-10-24. That test hit Windows Code Integrity event 3077, blocking `UNIONFREEAIM.DLL`. The original game setup was restored and all 42 original file hashes were verified.
- Current update: the user manually turned Smart App Control off; state 0 was verified. Free Aim was reinstalled, both plugin DLLs loaded, and the user subsequently confirmed it works. Automated weapon testing was stopped with Escape. Preserve this working aiming installation.
- Do not alter Windows security policy as part of routine mod installation.
- Use `manage.ps1 validate` before installation and `manage.ps1 status` afterward. The current backup and executable hash target one installed build; review them before supporting another build.
- Close the exact game process before replacing runtime files. Preserve all backups and record installed paths and hashes.
- Update verification notes with what actually ran, and retain explicit untested items.
- Publishing or pushing this local repository requires the user's instruction.
- New skating work belongs under `skate3/`; private dependencies and derived assets belong in sibling `.modding/skate3-archolos/`. Read its journal and contract before implementation. The user selected the Rust skating recreation rather than running the original recomp as the simulation.
