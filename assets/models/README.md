# Model overrides

Every model in the game has a generated placeholder (see `tools/genmodels.py`).
Drop a `.glb` here with the same name and the build uses it instead. No code or
Makefile changes are needed.

| File | What it is |
|---|---|
| `soldier.glb` | Player trooper |
| `skitter.glb` | Fast swarmer bug |
| `gnasher.glb` | Standard warrior bug |
| `bulwark.glb` | Slow armored brute bug |
| `dropship.glb` | Extraction dropship |
| `beacon.glb` | Calldown beacon |
| `turret.glb` | Sentry turret |
| `rock_a.glb`, `rock_b.glb`, `rock_c.glb` | Arena obstacles |
| `ground.glb` | 4x4 m ground tile |

## Export rules (Blender + Fast64)

- Use Fast64 materials, and tick **Include > Custom Properties** in the glTF
  export dialog. Tiny3D's importer rejects materials without Fast64 data.
- 1 Blender unit = 1 metre. The model faces Blender **-Y** (it ends up facing
  +Z in glTF), Z up, origin at the ground contact point.
- Keep it low-poly and vertex-colored. If you need a texture, it must fit in
  4 KB of TMEM (CI4/CI8 preferred). Put the PNG next to the `.glb`; texture
  conversion is added to the build with the first textured asset.
- Skinned models: one armature per file, no transforms on the armature's
  parent objects (the importer refuses them).
