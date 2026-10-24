# Verification

Date: 2026-10-24, Europe/Moscow.

## Current status

The user manually changed Smart App Control; policy state 0 was verified. Union and Free Aim were reinstalled and both DLLs loaded. The user subsequently confirmed aiming works and requested no further aiming changes. The latest status check verifies all eight installed dependency hashes and the unchanged executable. Individual automated weapon scenarios below remain unverified by the agent.

## Completed

- Official Union archive matches the author's repository Git blob and pinned SHA-256.
- Steam reported a successful download of Workshop item 3749987428, manifest 8655955822285345073.
- Inspected the VDF catalog and confirmed both plugin DLLs are x86.
- Confirmed INI option names from source and the packaged menu script.
- Backed up 42 original system, save and configuration files with SHA-256 hashes.
- Installed the eight staged files and verified their hashes during copying.
- Confirmed the original game executable remains unchanged.
- Compared the loaded plugin's Union ordinal imports and named MSVC, kernel32 and dbghelp imports with the installed x86 libraries; no missing checked symbols were found.
- Restored the original setup after the startup block. Verified all 42 original files against the backup SHA-256 manifest, and verified all eight staged files remain intact.

## Earlier startup block (resolved by the user)

The first Steam test stalled during first-start initialization and was stopped by its exact PID, 2360. A direct retry with `-game:TheChroniclesOfMyrtana.ini` reached a Windows **Bad Image** dialog for `Data/$Templates$/UNIONFREEAIM.DLL`, error `0xc0e90002`.

Windows Code Integrity event **3077**, dated **2026-10-24 11:04:32 Europe/Moscow**, explicitly reports that `Gothic2.exe` attempted to load this DLL and that it did not meet the signing requirements or violated code integrity policy. The local event log is the source of this finding; architecture and import checks do not override it.

The blocked test process, PID 23852, was stopped. No test save was created. The original game files and settings were restored, and the generated DLL cache was preserved under the private `disabled/` directory. No Windows security policy was changed.

## Not verified

- Archolos main menu and plugin settings with this plugin active.
- Bow and crossbow: crosshair follows mouse, free vertical/horizontal aim, firing and movement.
- Clean gameplay exit with the mod active.

These are limits of the agent's automated testing; they do not override the user's subsequent successful gameplay report.

## Known limits

- The plugin also changes spell aiming; no independent bows-only toggle was found.
- The packaged camera zoom menu item writes the `enabled` key instead of `cameraZoomEnabled`. Change camera zoom directly in Gothic.ini and restart the game.
- Archolos does not officially support arbitrary Union plugins. Successful checks apply to this installed build and the scenarios actually tested.
