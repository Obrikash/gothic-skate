# Upstream dependencies

This repository records dependency versions and checksums and does not redistribute the dependencies.

| Component | Author / project | Official source |
| --- | --- | --- |
| Union 1.0m | Union team, Gratt | https://github.com/Gratt-5r2/gothic-fix-archive |
| Union Free Aim | Blackscull, based on Gratt's zEasyFreeAim | https://gitlab.com/blackscull/union-free-aim |
| Compiled Union Free Aim package | Blackscull | https://steamcommunity.com/sharedfiles/filedetails/?id=3749987428 |
| zModelProtoExtender, included upstream | Gratt | Included in the author's Workshop package |
| Modding workflow skills | rehan-remade/universal-modder | https://github.com/rehan-remade/universal-modder |
| Rust skating recreation | SK8-ENGINE, GPL-3.0-only core | https://github.com/SK8-ENGINE/skate-3-rust-engine |
| Pinned x64 worker and host-bridge reference | Faiqie/BullySkate v0.1.2; preserve upstream component licenses | https://github.com/Faiqie/BullySkate |
| Gothic API layout reference, kept private | Union framework, GPL notices | https://gitlab.com/union-framework/gothic-api |
| Portable C compiler | Zig 0.17.0 | https://ziglang.org/ |

Skating dependency pins and hashes are in skate3/dependencies.json. Extracted game assets, reference SDK/API files, downloaded binaries, generated address/fingerprint headers and backups remain outside this repository. The initial retired render probe used MinHook; the active callback implementation does not link it. No third-party game files are redistributed here.

The upstream package includes its own textures, animations and dependencies. Obtain it through the author's official distribution and consult the corresponding upstream terms before redistributing or modifying it.

The integration scripts and documentation in this repository were prepared with Codex. The aiming implementation comes from the upstream plugin.
