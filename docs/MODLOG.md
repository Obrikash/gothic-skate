# Archolos free aiming

Date: 2026-10-24 (Europe/Moscow)

## Request

Install relevant skills from rehan-remade/universal-modder and add free aiming with a crosshair for bows and crossbows to this Steam Archolos installation.

## Local evidence

- Game root: C:\Program Files (x86)\Steam\steamapps\common\TheChroniclesOfMyrtana
- Steam app 1467450, installed build 18450605.
- Engine: native x86 ZenGin, Gothic2.exe 2.6 (fix), 9,038,140 bytes.
- SystemPack.vdf and SystemPack.ini present. No system/autorun or Data/Plugins directory found; Shw32.dll is 50,176 bytes. No Union runtime identified yet.
- dinput.dll is DirectInput1-7 to 8 Converter 1.0.16.0; preserve it.
- No Gothic game process running during initial inspection.
- Saves_TheChroniclesOfMyrtana contains only current; check Saves as well before backup.

## Skills

Installed via the official Codex skill-installer helper: mod-any-game, game-recon, game-research-websearch, reverse-engineering, game-automation, port-a-mod.

Applied mod-any-game, game-recon and game-research-websearch. Read the ZenGin playbook. The shared knowledge index has one Gothic II passthrough note, no Archolos free-aim note. Manual file reconnaissance used while the optional um CLI is unavailable.

## Route

Prefer Union 1.0m plus Blackscull's Union Free Aim over Ninja. Investigate official build availability, source configuration and a reversible installation before changing runtime files. No replacement game executable is needed: this installation already has the supported report build.

The downloaded skill explicitly says: "Ask before installing a loader into the game folder". Prepare exact files, backup and rollback before requesting that confirmation. No loader or runtime files have been installed.

## Sources

- Toolkit: https://github.com/rehan-remade/universal-modder
- Skill: https://github.com/rehan-remade/universal-modder/blob/main/skills/mod-any-game/SKILL.md
- Engine playbook: https://github.com/rehan-remade/universal-modder/blob/main/skills/mod-any-game/references/engines/zengin.md
- Free-aim source: https://gitlab.com/blackscull/union-free-aim (project 81795513)
- Official plugin Workshop: https://steamcommunity.com/sharedfiles/filedetails/?id=3749987428
- Author announcement: https://worldofplayers.ru/threads/44050/
- Union author's archive: https://github.com/Gratt-5r2/gothic-fix-archive
- Original Ninja-based free aiming: https://github.com/szapp/FreeAiming
- Archolos technical FAQ: https://steamcommunity.com/sharedfiles/filedetails/?id=2874729582

Workshop description documents RMB aim, LMB fire, movement while aiming, ballistics toggle and optional zoom. It also affects magic; inspect whether it can be limited to bows/crossbows. Author's GitLab releases endpoint returned an empty list. Official announcement requires Union 1.0m.

## Research queries

- Gothic 2 Archolos free aim plugin Union free aiming github
- Archolos Ninja incompatible Union Free Aim install download Union Resource Manager official
- Union gothic github releases 1.0m 1.0n Gratt download
- UnionFreeAim.vdf download
- zEasyFreeAim download GitHub

## Prepared installation

- Union archive pinned to author repository commit d61fd89478de5817225d08675a7cdc478a445896.
- Verified archive Git blob SHA-1: 1a04dfebbca8395ee434f23154d6f243591e9e08 (matches GitHub tree). Archive SHA-256: 9A5FDFF7C1E8D33C0E6B9051375F2569A352B1AAC835040B6EF81CB7F92CF308.
- Free-aim source commit: a372c912684f26d1db111d0324276c10e049d76a. Source has no compiled release assets.
- Downloaded the binary with the existing Steam client's `workshop_download_item 39510 3749987428`. Steam workshop log confirms OK / No Error. No Workshop subscription was created.
- Official Workshop manifest: 8655955822285345073. VDF size 4,501,870 bytes; SHA-256 AD4DA763225D00BAB2C72AB2E41C02A4287B62C417AF64861AF537F69F84163A.
- Inspected VDF catalog: contains SYSTEM/AUTORUN/UNIONFREEAIM.DLL (x86, 4,192,256 bytes), ZMODELPROTOEXTENDER.DLL (x86, 150,016 bytes), menu script, crosshair textures and movement animations.
- Confirmed source INI settings: [UNIONFREEAIM] enabled, cameraZoomEnabled, elevationEnabled. No separate bows-only switch; magic is also affected. Prepared defaults 1, 1, 0.
- The packaged camera zoom menu option incorrectly names `enabled` rather than `cameraZoomEnabled`; setting camera zoom directly in Gothic.ini avoids relying on that menu entry. Do not claim this upstream issue is fixed.
- Existing SystemPack.vdf contains CODE_EFD8A07B.PATCH. Installer parks it outside mounted Data when installing Union.
- Staging includes eight planned files. Optional Bink/Miles replacements are excluded.
- Backed up 42 files from system/, Saves/, Saves_TheChroniclesOfMyrtana/, vdfs.cfg and Data/SystemPack.vdf to backups/before-union-20261024; manifest holds hashes. Neither save directory had named save slots.
- Original Gothic2.exe SHA-256: 0655B4811E008A353CE372FDE463EF00B6101F3E89BE850E2F14980140447C7F.
- manage_free_aim.py provides validate/install/restore. It verifies staged and original hashes, refuses changes while Gothic2.exe is running, records installation state and restores on failure. Restore preserves displaced mod files under disabled/ and leaves saves alone.
- Validation passed: all eight staged file hashes match, original executable matches and backup is intact.

## Approved installation and test

- The user approved installing and testing the prepared setup, and requested a Git repository named better-aim.
- Installed all eight payload files and configured aiming. Verified installed hashes and the unchanged Gothic2.exe.
- The first Steam launch stalled in first-start initialization. Stopped only the known test PID 2360. A subsequent UI launch attempt failed before creating a process because the accessibility window-open handler did not become ready.
- Retried directly with the same Archolos game profile and the correct system working directory; test PID 23852 reached a Bad Image dialog naming Data/$Templates$/UNIONFREEAIM.DLL and error 0xc0e90002.
- Confirmed the cached plugin is x86. Checked required Union ordinals 258 and 259 and all named imports from x86 MSVCP140.dll, VCRUNTIME140.dll, KERNEL32.dll and dbghelp.dll; the checked exports exist.
- Read Windows Code Integrity event 3077, 2026-10-24 11:04:32 Europe/Moscow. It explicitly says Gothic2.exe attempted to load UNIONFREEAIM.DLL and that the DLL did not meet signing-level requirements or violated code integrity policy. This establishes the actual blocker; it is not a missing-import diagnosis.
- Stopped only PID 23852 and restored all affected original files. Extended rollback coverage to SystemPack.ini and Paths.d, which Union rewrites on first start.
- Preserved the generated Data/$Templates$ cache under disabled/generated-Data-Templates-20261024. No cache or backup was deleted. No Windows security policy was changed.
- Verified restoration: all 42 original file hashes match the backup. Revalidated all eight staged dependency hashes.

## Repository

Created the local better-aim repository on branch main. Canonical integration code is tools/manage_free_aim.py; manage.ps1 selects the bundled Python. Added dependency pins, a config fragment, a read-only PE import/export inspector, upstream credits and setup/verification notes. Private binaries, upstream source, backups and runtime state remain outside Git. No remote was created or published.

## Current status

The user manually changed Smart App Control; state 0 was verified. Reinstalled Union and Free Aim, verified both loaded DLLs and all eight dependency hashes. The user confirmed aiming works and requested no further aiming changes. Preserve this working installation. New skating work and verification are documented separately under skate3/.
