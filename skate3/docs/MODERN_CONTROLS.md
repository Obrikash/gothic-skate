# Modern exploration controls

Experimental separate plugin for the inspected Archolos / Gothic II 2.6 (fix) build.
The target is modern third-person exploration: mouse orbit independent of the
character, and WASD relative to that view. This does not recreate either The
Witcher 3 or Resident Evil 2's combat system.

## Controls

- Mouse: orbit around Marvin; looking around while idle leaves his facing alone.
- W/A/S/D: move in the corresponding camera-relative direction. Marvin turns to
  face that direction and uses Gothic's own forward locomotion and collision.
  Opposite keys cancel and diagonals use the same locomotion speed.
- Mouse wheel: camera distance.
- F6: toggle modern/original controls, persisted in the private configuration.
- F8 and all existing skating controls keep their skating meanings.

The orbit camera also runs during skating in ordinary exploration camera modes.
The skating movement lock excludes the WASD remapping, leaving push/brake/steer
with the physics worker. Melee weapons retain the orbit camera, including hit
and parry states; Gothic still owns melee movement, targeting and attacks.
Bows, crossbows and magic yield to Gothic and the existing Free Aim plugin.
Menus, dialogs, interaction, death,
unconsciousness, swimming, climbing, first-person and scripted camera modes use
Gothic's own controls. Melee locomotion is not modernized in this version.

## Configuration and rollback

`.modding/skate3-archolos/modern-controls.ini`, relative to the game install:

```ini
[Controls]
Enabled=1
MouseSensitivity=0.25
InvertY=0
CameraDistance=280
CameraHeight=135
ShoulderOffset=25
TurnRate=720
```

Distances are centimetres, turn rate is degrees/second, and sensitivity is
degrees per Gothic mouse unit (so the game's own sensitivity still applies).
Settings reload about once a second. F6 or `Enabled=0` returns to Gothic's
controls without removing the DLL. Camera zoom persists for the session; editing
CameraDistance changes its starting distance. Horizontal camera recentering is
deliberately off: moving in a new direction never drags the camera behind Marvin.

With the game closed, use the private RE Python interpreter to run:

```text
skate3/tools/build_modern_controls.py
skate3/tools/test_modern_controls.py
skate3/tools/install_modern_controls.py install
skate3/tools/install_modern_controls.py status
skate3/tools/install_modern_controls.py remove
```

Paths above are relative to this repository. Installation snapshots system/ and
both save folders to a timestamped private backup before copying the DLL. It
sets only Gothic.ini's `zSmoothMouse` to 0. Removal parks the known DLL and
restores the previous smoothing value if it is still our 0, preserving other
settings. The installer leaves the aiming dependencies and skating DLL alone.

## Implementation and checks

The separate `ArcholosModernControls.dll` uses Union lifecycle callbacks and
MinHook 1.3.4. It validates the host executable when built and its entry-point
fingerprints at runtime. Five detours implement the integration:

| Function | Address | Purpose |
| --- | --- | --- |
| oCAIHuman::_WalkCycle | 0x69AE20 | Scope remapped input to Marvin's locomotion |
| oCAIHuman::PC_Turnings | 0x69A940 | Suppress Gothic mouse/tank turning inside that scope |
| zCInput_Win32::GetState | 0x4D4D30 | Map camera-relative intent to forward motion |
| zCInput_Win32::GetToggled | 0x4D5020 | Supply matching forward transition events |
| zCAICamera::DoAI | 0x4A4FB0 | Position the orbit camera after native camera processing |

NPCs and input outside the locomotion scope call the original functions. Action
modifier interactions also run the original walk cycle. Position and heading
use the host's own SetPositionWorld / SetHeadingAtWorld methods. Camera traces
test five rays, with near-plane offsets and 15 cm wall clearance, against BSP
and collidable objects, ignoring characters. This is an approximation of camera
volume collision; tight corners still need visual testing.

The controller structure was informed by the MIT-licensed
[Gothic Game Mixing Witcher controller](https://github.com/Kopselek/gothic-game-mixing/blob/53d2483a6b308234ffac9938ce8055d728d9482c/mashups/witcher/plugin/src/Plugin.ipp).
Only its control approach was used; no Witcher assets or combat replacement is
installed. See [third-party notices](../THIRD_PARTY_NOTICES.md).

Native offline regression covers 1,152 direction/heading/handedness cases,
normalized diagonals, 30–240 Hz turning, the yaw seam, 180-degree turns, camera
basis length, world-offset focus height and separate melee-camera / melee-input /
ranged-aiming ownership. The first live run exposed a wrong
bbox component in camera height; the corrected code uses min.y explicitly and
has a regression at the real ship-area coordinates plus an invalid-height
fallback. Assertions are explicitly enabled in this optimized test build.

Live verification is recorded separately in MODLOG.md. A loaded DLL and rising
hook counters do not prove camera framing, collision or control feel.

The user confirmed exploration feels good after testing the corrected version.
Their report that combat reverted to the old camera identified the overly broad
weapon guard. The next revision separates camera ownership from locomotion:
weapon modes 0–4 retain the camera and modes 5–7 return to Free Aim. The allowed
camera modes now include CAMMODMELEE. Melee gameplay verification remains pending.
