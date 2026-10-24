# Proposed bridge contract

This is a partially implemented contract. Gameplay verification and rider calibration remain incomplete; see MODLOG.md.

## Ownership

Archolos owns the world, NPCs, quests, inventory, health, saves, dialogs and menus. While the player deliberately enables skating with F8, the Rust worker owns board physics and trick state. The adapter mirrors the confirmed worker output to the host player and presentation objects.

Dialog, inventory, loading, area changes, death, stale data or a worker error suspend skating and return movement to Archolos. No save may record a worker pointer or rely on a worker process surviving a load. The worker itself stays warm across mounts and loads: it holds no game state, and every mount reloads collision captured in that frame for the current world (a fresh area number per capture, so select_world always reloads). The board object is never archived.

## Process boundary

The host is x86 and the worker x64. Cross-process data contains fixed-width integers and floats, never pointers, handles or C++/Rust objects. The initial investigation targets BullySkate's 6,184-byte event-synchronized block at release v0.1.2; confirm the exact release layout before using it. Command/response events serialize writes, with a nonblocking game-frame poll and bounded input queue.

Use local Windows named objects scoped by process ID. The parent owns the worker lifetime. A stopped, unresponsive or restarted peer cannot authorize movement. No connection to the original games' online services is part of this project.

Wire output[13] indicates ground-physics category 100. It does not indicate off-board state and must not cause a normal grounded dismount. Game JSON output can be redirected into Windows UAC VirtualStore; choose the newest artifact matching the exact process/request instead of assuming the physical install path is current.

## Coordinates

Gothic: centimeters, +Y up (ZenGin playbook). Rust core: meters, +Y up. The reused worker's existing wire output is Z-up and includes its own axis conversion. Keep those two mappings distinct.

Axis signs, forward direction, triangle winding and pose conventions remain uncalibrated. The host adapter must refuse to enable skating until a known translation, height and turn agree in both processes. Do not pass Gothic coordinates through unchanged.

## Verification

First prove worker initialization, a stable flat-ground contact, forward motion and an ollie with finite outputs. Then prove one real host position crossing with sequence counters, a level/load handback and deterministic cleanup. Synthetic lab success does not prove host collision or gameplay integration.
