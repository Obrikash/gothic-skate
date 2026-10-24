# Archolos skating integration

Status: experimental. Single-press F8 mounting, sustained movement, braking and rolling collision recapture have run in Archolos. The user confirmed the rider appearance and the installed 7-metre recapture fix work in game. Separate collision, restoration and fallback scenarios remain unverified as recorded in [MODLOG.md](MODLOG.md). The user also confirmed the new [modern exploration controls](docs/MODERN_CONTROLS.md) feel good. A follow-up retaining the orbit camera during melee is installed; live combat verification is pending.

The requested target is Skate-style board physics and tricks in the existing Archolos world. The user selected the existing Rust skating engine after reviewing the difference from the original Skate3Recomp simulation.

## Route

Use the [SK8-ENGINE Rust recreation](https://github.com/SK8-ENGINE/skate-3-rust-engine) through a separate x64 physics worker, with an x86 adapter in Gothic's existing Union loader. [BullySkate](https://github.com/Faiqie/BullySkate) is the nearest implementation: it already separates a 32-bit game adapter from a 64-bit worker.

This uses a recreated engine whose gameplay parity remains in progress. It does not run the linked Skate3Recomp executable as the physics provider.

The installed Skate3Recomp v2.0.2 supplies the user's already extracted Xbox 360 game data. Derived assets, upstream source and third-party binaries stay under the private sibling `.modding/skate3-archolos/` directory.

See [MODDING_PLAN.md](MODDING_PLAN.md), [docs/CONTRACT.md](docs/CONTRACT.md), and [MODLOG.md](MODLOG.md). The existing aiming installation remains untouched.

## Using it

Load a save and press **F8**. The skating engine starts in the background once a world is running (about 7 s, logged as "Skating ready"). If F8 comes earlier, the bridge mounts as soon as the engine is ready; F8 again cancels. On F8 the bridge captures the collision around Marvin in-process and mounts the warm worker, which reloads it in about 15 ms (offline measurement). No separate capture or arming step remains.

Controls while riding: W push, S brake, A/D steer, hold Space and release to ollie (hold Q or E while releasing for a kickflip or heelflip), Shift/Ctrl grab, F7 cycles the rider: skate (the physics engine's own Skate rider motion retargeted onto Marvin's skeleton), a Gothic pose, or off, F8 to step off. The collision follows the board, so rides are not limited to the first 14 m. `.modding/skate3-archolos/host-bridge.ini` holds `[Bridge] Enabled` (installed as 1), optional `[Rider] Enabled/Ground/Air/Method` and `[Board] Mesh/YawDegrees/LiftCentimeters`.

The worker's lab lives in `.modding/skate3-archolos/host-worker-lab` (prepared by `tools/prepare_warm_lab.py`; the installer runs it); a LocalAppData lab was invisible to the game. The external `capture_bridge.py`/`arm_bridge.py` flow is obsolete for play; its geometry functions remain the reference for `tools/test_capture_core.py`.

## Evidence and limits

The first sustained host ride applied 303 worker outputs before the board fell through a gap in the sampled ground; with gap filling and the per-cell support guard, a second ride applied 393 outputs over 6.6 s and 14 m on the real floor. Both used the old external capture.

Later sessions confirmed F8 capture (15-47 ms), the warm worker, the board object and multiple rolling recaptures during a 32-second ride. The user confirmed braking works. The skate skeleton's missing world-origin conversion was corrected after the user reported invisibility.

The latest batch uses Gothic's object-collision flag and includes stationary mover meshes such as the ship. Water and noCollDet materials are omitted. Captured objects are monitored; moving or changed geometry causes dismount because moving platforms are not supported. F7/off/dismount restore the saved rider skeleton, and malformed worker poses fall back to a Gothic pose.

Offline checks: `tools/test_capture_core.py` compares ground/support/boxes/rails to the Python reference, `tools/test_mesh_core.py` tests the indexed mesh pipeline and real worker collision, and `tools/test_retarget_core.py` compares six motions at three world origins and rejects corrupted poses. The latest live ship collision, capture cost, rider appearance/restoration, moving-object guard and fallback still need gameplay verification. Grinding has not been demonstrated. See [engine findings](docs/ENGINE_NOTES.md).
