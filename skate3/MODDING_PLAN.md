# Archolos skating plan

## Confirmed target

- Skate-style skating physics and tricks inside the real Archolos world, with Archolos retaining NPCs, quests and ordinary gameplay.
- User-selected engine: existing Rust recreation, rather than the original Skate3Recomp simulation.
- Host: installed Steam Archolos, app 1467450, build 18450605, x86 Gothic II 2.6 (fix), existing Union 1.0m loader.
- Guest assets: user's installed Skate3Recomp v2.0.2 game/default.xex and neighboring data directory. Do not change this installation or its saves.
- Guest runtime candidate: BullySkate v0.1.2's isolated x64 physics worker, based on the SK8-ENGINE recreation. Inspect and test its assumptions before adapting it.

## Milestones and evidence

1. Prepare and verify the required animation/state/physics data from the user's local game archives. Do not fetch game assets.
2. Run the actual Rust worker against authored flat collision in an isolated lab, passing input and verifying finite movement, contact and ollie results. This is a simulation test, not an Archolos gameplay claim.
3. Build an x86 host adapter with a disabled-by-default mount toggle, heartbeat, pause/load guards, and automatic handback on worker failure.
4. Export a small piece of actual Archolos collision, match units and handedness, and move the host player using the worker's confirmed output. Verify in Archolos before adding more features.
5. Add the board, meaningful rider pose mapping and camera, then expand collision to ramps/rails, area changes, scripted interactions and recovery.

## Constraints and unknowns

- No C++, Rust, LLVM, CMake or Ninja compiler was found in the initial toolchain checks. The host adapter needs a suitable x86 compiler; using a prebuilt worker can avoid rebuilding the Rust engine initially.
- BullySkate's worker contains host-specific paths, protocol names, a 36-bone pose layout and collision format. It cannot be copied into Archolos and treated as a complete mod.
- No Bully game files are available or needed for this project. Test geometry and the lab skeleton must be authored locally; the real host geometry and skeleton must come from the user's Archolos installation.
- Gothic's world uses centimeters and +Y up. The worker's exposed protocol is meter-scaled and Z-up; calibrate the exact mapping before enabling movement.
- Renderer integration and moving-object collision are unresolved. Do not assume the installed GD3D11 directory means a renderer is active.
- Keep the existing working aiming setup intact. Back up any files affected by the new host plugin before installing it.

## Distribution

This is local work. Nothing is published. Preserve upstream notices and license requirements if a release is later requested; do not distribute either game's files or private conversions.
