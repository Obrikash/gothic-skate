# gothic-skate

Skate-style physics and rider motion inside **The Chronicles of Myrtana: Archolos**, using an x86 Union bridge and a separate x64 Rust physics worker. The repository also preserves the existing verified Free Aim setup.

The current checkpoint includes the user-confirmed rider presentation, real object-mesh collision, braking, rolling collision capture, and the fix for dismounting at the first 7-metre refresh. That fix pauses the simulation clock during our capture/reload work and reduces capture overhead; it is installed and passes offline regression checks, with its latest in-game verification pending. Camera changes have not started.

Start with the [skating documentation](skate3/README.md), [engine findings](skate3/docs/ENGINE_NOTES.md) and [verification journal](skate3/MODLOG.md).

This repository contains integration source, tests, dependency metadata and documentation. Game files, extracted assets, downloaded dependencies, binaries, saves and private lab data remain outside Git. The local checkout retains the folder name `better-aim/` for the established setup below.

## Existing aiming setup

Free aiming with a crosshair for bows and crossbows in **The Chronicles of Myrtana: Archolos**.

This project prepares **Union 1.0m** and **Union Free Aim by Blackscull** with a checked installation manifest, a reversible installer and local verification notes. It does not yet contain a custom aiming plugin.

**Current status:** Union and Free Aim are installed. After the user changed Smart App Control, both plugin DLLs loaded and the user confirmed aiming works. All eight installed dependency hashes pass; the original executable is unchanged.

The separate [skating prototype](skate3/README.md) is under development. Preserve the working aiming setup while testing it.

## Controls

With a bow or crossbow drawn, hold the **right mouse button** to aim and press the **left mouse button** to fire. The upstream plugin also changes spell aiming and supports moving while aiming.

The preset enables the plugin and camera zoom, with ballistic elevation disabled. The exact INI fragment is in [config/union-free-aim.ini](config/union-free-aim.ini). These values live under `[UNIONFREEAIM]` in the game's `system/Gothic.ini`.

## Local layout

Place this repository in the game's root directory as `better-aim/`. The installer resolves the game directory from that location.

```
TheChroniclesOfMyrtana/
  better-aim/                       # Git repository: our code and notes
  .modding/archolos-free-aim/        # Private downloads, staging, backups, state
  Data/
  system/
```

The private `.modding` folder is already prepared on this computer. It holds the original 42-file backup and the staged official dependencies. Game files, saves, third-party binaries and the downloaded upstream source are outside this repository.

## Commands

Run from this repository in PowerShell:

```powershell
.\manage.ps1 status
.\manage.ps1 validate
.\manage.ps1 install
.\manage.ps1 restore
```

`status` checks an installed setup or reports a previous restoration. `validate` checks the original setup and staging before installation. Close Archolos before `install` or `restore`. `restore` restores affected original files, including configuration files Union writes on first start, and preserves displaced mod files under `.modding/archolos-free-aim/disabled/`; it leaves saves alone.

The wrapper uses Codex's bundled Python when present, otherwise Python on PATH. The tool uses only Python's standard library. On machines without the prepared private folder, follow [docs/SETUP.md](docs/SETUP.md) first.

## Scope and verification

The targeted installation is Steam app **1467450**, build **18450605**, using the existing **Gothic II 2.6 (fix)** x86 executable. No executable replacement or new renderer is included. The existing DirectInput wrapper is preserved.

See [docs/VERIFICATION.md](docs/VERIFICATION.md) for actual test results, [dependencies.json](dependencies.json) for pinned file hashes, and [THIRD_PARTY.md](THIRD_PARTY.md) for upstream credits.

The GitHub repository is named `gothic-skate`; the working checkout on this machine remains `better-aim/`.
