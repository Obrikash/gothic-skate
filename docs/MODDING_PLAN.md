# Archolos free aiming plan

- Target: this installed Steam copy of The Chronicles of Myrtana: Archolos, app 1467450, build 18450605.
- Goal: aim bows and crossbows freely with a visible crosshair.
- Engine: x86 ZenGin, Gothic II NotR report 2.6 (fix).
- Route: existing Union free-aim plugin, configured for the requested behavior; customize only where required.
- First step: obtain official plugin and Union archives, inspect contents and configuration.
- Backup: snapshot system/, Saves/, Saves_TheChroniclesOfMyrtana/, vdfs.cfg and existing Data/SystemPack.vdf before runtime changes; keep checksums and restoration instructions.
- Install: stage only the required Union runtime files and plugin; preserve the game executable, localization and DirectInput wrapper.
- Approval: the user approved loader installation and a brief interactive test on 2026-10-24.
- Verification: compare installed hashes, launch through the existing Archolos launcher, confirm the plugin menu, then test bow/crossbow aiming and firing in a disposable session.
- Unknowns: official standalone plugin binary availability, bows-only configuration support, interaction with Archolos scripts, startup and in-game behavior.
- Rollback: restore changed runtime/configuration files from the snapshot and remove or disable only the recorded newly installed files. Saves are restored only if explicitly needed.

## Current result

The payload installed and passed hash verification. Windows Code Integrity event 3077 blocked UNIONFREEAIM.DLL at startup, so gameplay checks could not run. Restored the original setup and verified every original backup hash (42 files). Dependencies and installer remain prepared. Resolve the Windows signing-policy issue before another install/test cycle.
