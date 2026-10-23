# Model overrides

Every model in the game has a generated placeholder (see `tools/genmodels.py`).
Drop a `.glb` here with the same name and the build uses it instead. No code or
Makefile changes are needed.

| File | What it is |
|---|---|
| `soldier.glb` | Player trooper. Skinned, animations `run` and `idle`. Budget ~100 triangles |
| `skitter.glb` | Fast swarmer bug. Skinned, animations `walk` and `attack`. Budget ~45 triangles |
| `gnasher.glb` | Standard warrior bug. Skinned, `walk` and `attack`. Budget ~70 triangles |
| `bulwark.glb` | Slow armored brute bug. Skinned, `walk` and `attack`. Budget ~100 triangles |
| `dropship.glb` | Extraction dropship |
| `beacon.glb` | Calldown beacon |
| `turret.glb` | Sentry turret |
| `rock_a.glb`, `rock_b.glb`, `rock_c.glb` | Arena obstacles. Collision radii are 0.9, 1.35 and 0.55 m (`rock_radius` in `src/sim/arena.c`); keep a replacement about that size or update the table |
| `ground.glb` | The whole 64x64 m arena floor, centred on the origin |

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

## Budgets for the bugs

Up to the enemy cap (around a hundred) bugs are on screen at once, so per-bug
cost is what limits the swarm. In the M2 bench (in ares so far; hardware
numbers pending) the RSP/RDP time per bug is what runs out first, and it
scales with:

- **Triangles.** Stay inside the budgets above; leave out faces the overhead
  camera never sees (undersides).
- **Vertices.** Smooth-shade where you can: shared vertices are loaded and lit
  once. A bug that fits in one 70-vertex load draws in one batch.
- **Bones.** Each bone is a matrix load per bug drawn. Legs that move together
  can share a bone (the placeholders use one bone per side per gait group).
- **One material per bug.** The renderer sets a bug type's material once per
  frame and draws every bug of that type with geometry only. A bug model with
  several materials still works, but falls back to the slower per-bug path.

