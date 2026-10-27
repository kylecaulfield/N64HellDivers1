# Model overrides

Every model in the game has a generated placeholder (see `tools/genmodels.py`).
Drop a `.glb` here with the same name and the build uses it instead. No code or
Makefile changes are needed.

| File | What it is |
|---|---|
| `soldier.glb` | Player trooper. Skinned, animations `run` and `idle`. Budget ~100 triangles. Paint the parts that take the player's colour white with vertex alpha 0 (everything else alpha 1); one material |
| `skitter.glb` | Fast swarmer bug. Skinned, animations `walk` and `attack`. Budget ~45 triangles |
| `gnasher.glb` | Standard warrior bug. Skinned, `walk` and `attack`. Budget ~70 triangles |
| `bulwark.glb` | Slow armored brute bug. Skinned, `walk` and `attack`. Budget ~100 triangles |
| `dropship.glb` | Extraction dropship |
| `beacon.glb` | Calldown beacon |
| `turret.glb` | Sentry turret (also seen falling onto its beacon) |
| `crate.glb` | Resupply crate |
| `mgcase.glb` | Machine gun case |
| `pod.glb` | Drop pod: the squad's drop-in at the start of a mission, and Reinforce (seen falling onto its beacon) |
| `rock_a.glb`, `rock_b.glb`, `rock_c.glb` | Arena obstacles. Collision radii are 0.9, 1.35 and 0.55 m (`rock_radius` in `src/sim/arena.c`); keep a replacement about that size or update the table |
| `ground_ash.glb`, `ground_frost.glb`, `rock_*_ash.glb`, `rock_*_frost.glb` | The ground and rocks of ash and ice worlds (same shapes and sizes, other colours) |
| `ground.glb` | The whole 64x64 m arena floor, centred on the origin (the placeholder has grass tufts and pebbles: 842 triangles). Split it into meshes of about 8 m square (the placeholder has 64): each becomes an object with its own bounds, and only those on screen are drawn. One mesh works, but is drawn whole every frame |
| `nest.glb` | Bug nest (OBJ_NESTS): solid out to 1.6 m (`NEST_RADIUS`), where bugs crawl out. Up to four on screen |
| `nest_dead.glb` | What is left of a destroyed nest: walkable, flat |
| `terminal.glb` | Uplink terminal (OBJ_UPLOAD): solid out to 0.7 m (`TERMINAL_RADIUS`) |
| `shadow.glb` | Blob shadow: a flat dark disc, 1 m radius, facing up. Scaled and drawn under soldiers, pods and drops (shrinking as they fall) and the dropship |

The bugs carry their own shadow: a flat dark disc in the mesh, weighted to
the root bone, just above the ground (keep one in a replacement, or they
float).

## Export rules (Blender + Fast64)

- Use Fast64 materials, and tick **Include > Custom Properties** in the glTF
  export dialog. Tiny3D's importer rejects materials without Fast64 data.
- 1 Blender unit = 1 metre. The model faces Blender **-Y** (it ends up facing
  +Z in glTF), Z up, origin at the ground contact point.
- Keep it low-poly and vertex-colored. If you need a texture, it must fit in
  4 KB of TMEM (CI4/CI8 preferred). Put the PNG next to the `.glb`; texture
  conversion is added to the build with the first textured asset.
- Skinned models: one armature per file, no transforms on the armature's
  parent objects (the importer refuses them). One bone per vertex (the
  importer uses only the first joint).
- The soldier: the renderer replaces its material's combiner with one that
  puts the player's colour wherever vertex alpha is 0 (lit like the rest), so
  keep it to one material and paint those parts white. A soldier with
  several materials, or no alpha-0 vertices, draws untinted.

## Budgets for the bugs

Up to the enemy cap (64 by default) bugs are on screen at once, so per-bug
cost is what limits the swarm. In the bench (in ares so far; hardware
numbers pending) the RSP/RDP time per bug is what runs out first, and it
scales with:

- **Triangles.** Stay inside the budgets above; leave out faces the overhead
  camera never sees (undersides).
- **Vertices.** Smooth-shade where you can: shared vertices are loaded and lit
  once. A bug that fits in one 70-vertex load draws with one load.
- **Bones** cost nothing per bug drawn: at boot the renderer samples each
  clip once per sim tick and skins the mesh on the CPU into a vertex buffer
  per frame, so every bug draws as a static mesh. They cost memory instead:
  16 bytes per vertex per sim tick of `walk` and `attack` (the placeholders
  take about 190 KB in all, 133 KB of it the bulwark's). Keep the clips short and looping.
- **The attack clip** is played against the sim, not in a loop: its first 55%
  over the bug's windup (rear up), the rest over its recovery (strike and
  settle), whatever the clip's own length.
- **One material per bug.** The renderer sets a bug type's material once per
  frame and draws every bug of that type with geometry only. A bug model with
  several materials still works, but falls back to the slower per-bug path.

