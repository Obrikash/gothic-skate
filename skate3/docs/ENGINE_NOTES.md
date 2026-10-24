# Engine findings used by the skating bridge

Inspected the user's Gothic II 2.6 (fix) executable, SHA-256
`0655b4811e008a353ce372fde463ef00b6101f3e89be850e2f14980140447c7f`,
with the private Capstone-based `tools/disasm_host.py` and pinned Gothic API
`6e554b0acb199c9544037fb7136e07fad44a2fc4`. These addresses apply to that
exact build. Extracted data and disassembly stay outside Git.

## Terrain collision versus object collision

- `zCVob::SetCollDetStat` at `0x61CE50` writes bit `0x40` at vob `+0x104`.
- `zCVob::SetCollDetDyn` at `0x61CF40` writes bit `0x80` at the same offset.
- `CollectCollisionContext_Vobs` at `0x61E870` tests the sign of the flag
  byte on both the moving vob (`0x61E88B`) and each candidate (`0x61E8E7`).
  Both need `0x80` to participate in object/object collision.
- `CollectCollisionContext_Static` at `0x61EAD0` checks `0x40` and collects
  BSP polygons. This is terrain collision for the moving object; it does
  not decide whether another object is a solid obstacle.

Our former `0x40` obstacle filter excluded the ship. The real session logged
a large `zCMover` with flags `0x90`: object collision enabled, terrain
collision disabled. The class filter also excluded movers. Both filters
now follow the appropriate path. Plain visual vobs with neither collision
bit remain omitted; their existence does not prove they are solid.

Stationary movers can contribute a mesh snapshot. A captured object's
transform/visual/world/collision flag is checked before each riding update.
Translation over 2 cm, a rotation-element change over 0.01, removal or a
visual change causes dismount. This prevents riding against stale snapshot
geometry; it does not implement moving platforms.

## Materials and progressive meshes

The native BSP collision filter (`0x61EC27` onward) reads the polygon's
material, rejects material group 5 (water) at material `+0x40`, and rejects
the `0x10` noCollDet bit at material `+0x70`. The bridge now applies these
filters to nearby BSP polygons and progressive-mesh submeshes. Ground
traces also use `zTRACERAY_VOB_IGNORE_NO_CD_DYN` (1), documented in the
pinned API, to omit non-colliding decorative vobs.

Progressive-mesh layout, checked in `CheckRayPolyIntersection` (`0x5C7BF0`)
and `TraceRay` (`0x5C81A0`), with the pinned API:

| Data | Offset / size |
| --- | --- |
| Position array, count | mesh `+0x34`, `+0x38` |
| Submesh array, count | mesh `+0xA4`, `+0xA8` |
| Submesh size | `0x58` |
| Material pointer | submesh `+0x00` |
| Triangle array, count | submesh `+0x04`, `+0x08` |
| Wedge array, count | submesh `+0x0C`, `+0x10` |
| Triangle | three u16 wedge indices |
| Wedge | `0x18` bytes, u16 position index at `+0x14` |

`capture_core.c::progmesh_faces` handles this indexed data after the host
validates the backing memory. It transforms every vertex from model to
world centimetres, then into the fixed ride frame in metres, reflecting Z
and reversing winding. It retains floor/ceiling face direction and makes
walls two-sided. It omits invalid indices, non-finite/degenerate triangles
and triangles outside the current scan. A triangle's full bounds are used,
so a large floor spanning the capture is retained.

`tools/test_mesh_core.py` tests authored indexed fixtures, then gives their
generated BMGEO2 collision to one real worker. Open floor: 18.07 m travel.
Four walls 1.5 m away: maximum 1.052 m. Both mount/reloads took 16 ms in the
final run. This proves the extraction-to-worker pipeline offline, not the
live ship geometry or capture time.

## Direct rider poses and restoration

`zCModel::CalcNodeListAniBlending` (`0x57F880`) walks the same node list
(`+0x70`, count `+0x78`) and calls each node's blend function. Node-local
transforms (`zCModelNodeInst +0x0C`) are ordinary pose storage, not a
separate rendering overlay. Direct writes persist while the vob sleeps.

Consequently, selecting "off" must restore those transforms explicitly.
The bridge snapshots the live locals when binding, restores them when
switching methods or dismounting, and checks model/list/node identity
before writing. A replaced node list is rebound even if its allocation
address and count were reused. `[Rider] Enabled=0` now disables every method
and clears stale pose state on mount.

Before applying a worker pose, the bridge checks finite values, reasonable
rotation rows and every joint's distance from the board. Final local
matrices must also be finite and remain within 5 m per translation axis.
Invalid data falls back to the Gothic pose without changing movement
authority. Native regression tests cover real worker motions plus injected
NaNs, collapsed rotations, detached joints and the original missing-origin
failure. Visual restoration and the live fallback remain to be checked in
game.
