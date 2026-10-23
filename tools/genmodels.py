#!/usr/bin/env python3
"""Generate placeholder low-poly, vertex-colored glTF (.glb) models.

Usage: tools/genmodels.py OUT_DIR [name ...]

Standard library only. Output is deterministic (same bytes every run), so the
build only reconverts models when this script changes.

Conventions every model follows (and that a Blender/Fast64 replacement must
follow too, see assets/models/README.md):
  * 1 unit = 1 metre, +Y up, model faces +Z (Blender: faces -Y, Z up; the glTF
    exporter converts the axes).
  * Origin at the ground contact point, centred.
  * Flat shaded: each triangle has its own vertices, normal and color.
  * Materials carry Fast64 "f3d_mat" extras, because Tiny3D's importer refuses
    materials without them. The combiner is shade-only (vertex color x light).
"""

import json
import math
import os
import random
import struct
import sys

# ---------------------------------------------------------------------------
# Tiny geometry kit. A mesh is a list of triangles: ((p0, p1, p2), rgb), with
# counter-clockwise winding seen from outside. Colors are sRGB 0-255.

def v_add(a, b): return (a[0] + b[0], a[1] + b[1], a[2] + b[2])
def v_sub(a, b): return (a[0] - b[0], a[1] - b[1], a[2] - b[2])
def v_scale(a, s): return (a[0] * s, a[1] * s, a[2] * s)
def v_cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])
def v_len(a): return math.sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2])
def v_norm(a):
    length = v_len(a)
    return (0.0, 1.0, 0.0) if length < 1e-9 else v_scale(a, 1.0 / length)


def quad(a, b, c, d, color):
    """Two triangles for the quad a-b-c-d (CCW)."""
    return [((a, b, c), color), ((a, c, d), color)]


def box(cx, cy, cz, sx, sy, sz, color, top=None, bottom=None):
    """Axis-aligned box centred at (cx, cy, cz) with full sizes (sx, sy, sz)."""
    x0, x1 = cx - sx / 2, cx + sx / 2
    y0, y1 = cy - sy / 2, cy + sy / 2
    z0, z1 = cz - sz / 2, cz + sz / 2
    top = top or color
    bottom = bottom or color
    side = color
    tris = []
    tris += quad((x0, y1, z1), (x1, y1, z1), (x1, y1, z0), (x0, y1, z0), top)     # +Y
    tris += quad((x0, y0, z0), (x1, y0, z0), (x1, y0, z1), (x0, y0, z1), bottom)  # -Y
    tris += quad((x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1), side)    # +Z
    tris += quad((x1, y0, z0), (x0, y0, z0), (x0, y1, z0), (x1, y1, z0), side)    # -Z
    tris += quad((x1, y0, z1), (x1, y0, z0), (x1, y1, z0), (x1, y1, z1), side)    # +X
    tris += quad((x0, y0, z0), (x0, y0, z1), (x0, y1, z1), (x0, y1, z0), side)    # -X
    return tris


def cylinder(cx, cy, cz, radius, height, segments, color, cap_color=None, radius_top=None):
    """Y-axis cylinder (or frustum when radius_top differs), base at cy."""
    cap_color = cap_color or color
    rt = radius if radius_top is None else radius_top
    tris = []
    y0, y1 = cy, cy + height
    for i in range(segments):
        a0 = 2 * math.pi * i / segments
        a1 = 2 * math.pi * (i + 1) / segments
        b0 = (cx + math.sin(a0) * radius, y0, cz + math.cos(a0) * radius)
        b1 = (cx + math.sin(a1) * radius, y0, cz + math.cos(a1) * radius)
        t0 = (cx + math.sin(a0) * rt, y1, cz + math.cos(a0) * rt)
        t1 = (cx + math.sin(a1) * rt, y1, cz + math.cos(a1) * rt)
        tris += quad(b0, b1, t1, t0, color)
        if rt > 1e-6:
            tris.append((((cx, y1, cz), t0, t1), cap_color))
        tris.append((((cx, y0, cz), b1, b0), cap_color))
    return tris


def ellipsoid(cx, cy, cz, rx, ry, rz, color, rings=4, segments=6, jitter=None):
    """Low-poly UV ellipsoid. `jitter(i, j)` may return a radius multiplier."""
    verts = []
    for j in range(rings + 1):
        phi = math.pi * j / rings
        row = []
        for i in range(segments):
            theta = 2 * math.pi * i / segments
            k = jitter(i, j) if jitter else 1.0
            row.append((cx + math.sin(phi) * math.sin(theta) * rx * k,
                        cy + math.cos(phi) * ry * k,
                        cz + math.sin(phi) * math.cos(theta) * rz * k))
        verts.append(row)
    tris = []
    for j in range(rings):
        for i in range(segments):
            i2 = (i + 1) % segments
            a, b = verts[j][i], verts[j][i2]
            c, d = verts[j + 1][i2], verts[j + 1][i]
            if j == 0:
                tris.append(((a, d, c), color))
            elif j == rings - 1:
                tris.append(((a, d, b), color))
            else:
                tris += quad(a, d, c, b, color)
    return tris


def transform(tris, scale=(1, 1, 1), rot_y=0.0, rot_x=0.0, rot_z=0.0, offset=(0, 0, 0)):
    """Scale, then rotate X, Z, Y (radians), then translate."""
    cy, sy = math.cos(rot_y), math.sin(rot_y)
    cx, sx = math.cos(rot_x), math.sin(rot_x)
    cz, sz = math.cos(rot_z), math.sin(rot_z)

    def tf(p):
        x, y, z = p[0] * scale[0], p[1] * scale[1], p[2] * scale[2]
        y, z = y * cx - z * sx, y * sx + z * cx
        x, y = x * cz - y * sz, x * sz + y * cz
        x, z = x * cy + z * sy, -x * sy + z * cy
        return (x + offset[0], y + offset[1], z + offset[2])

    flip = scale[0] * scale[1] * scale[2] < 0
    out = []
    for (p0, p1, p2), col in tris:
        q0, q1, q2 = tf(p0), tf(p1), tf(p2)
        out.append(((q0, q2, q1) if flip else (q0, q1, q2), col))
    return out


def mirror_x(tris):
    return transform(tris, scale=(-1, 1, 1))


# ---------------------------------------------------------------------------
# Palette (sRGB). Original designs: the player corps wears slate-and-amber
# hazard armor; the swarm ("the Gnash") is chitin-violet with acid-green glands.

ARMOR      = (88, 98, 112)
ARMOR_DARK = (52, 58, 68)
AMBER      = (232, 150, 40)
VISOR      = (40, 200, 220)
SKIN_GLOVE = (36, 36, 40)
GUN        = (30, 32, 36)

CHITIN     = (92, 52, 120)
CHITIN_DK  = (52, 28, 72)
CHITIN_LT  = (140, 96, 170)
GLAND      = (150, 230, 60)
CLAW       = (225, 210, 180)
PLATE      = (70, 64, 60)

HULL       = (120, 126, 118)
HULL_DARK  = (70, 74, 70)
THRUSTER   = (255, 140, 40)
GLASS      = (60, 170, 210)

ROCK_A     = (122, 104, 86)
ROCK_B     = (98, 86, 74)
ROCK_C     = (140, 124, 100)
GROUND     = (120, 100, 70)


def soldier():
    """Trooper, ~1.8 m. Faces +Z. Backpack on -Z, rifle on the right (+X... -X)."""
    t = []
    # Legs
    for side in (-1, 1):
        t += box(0.13 * side, 0.42, 0.0, 0.18, 0.84, 0.22, ARMOR_DARK)
        t += box(0.13 * side, 0.06, 0.04, 0.2, 0.12, 0.3, SKIN_GLOVE)        # boots
    # Torso with amber chest stripe
    t += box(0, 1.15, 0, 0.5, 0.62, 0.3, ARMOR, top=ARMOR_DARK)
    t += box(0, 1.22, 0.155, 0.36, 0.12, 0.02, AMBER)
    # Shoulder pads
    for side in (-1, 1):
        t += box(0.33 * side, 1.38, 0, 0.18, 0.12, 0.3, AMBER)
    # Arms, holding the rifle forward
    for side in (-1, 1):
        t += box(0.32 * side, 1.12, 0.12, 0.14, 0.42, 0.14, ARMOR_DARK)
        t += box(0.22 * side, 0.98, 0.32, 0.12, 0.12, 0.14, SKIN_GLOVE)
    # Rifle
    t += box(0.0, 1.0, 0.45, 0.08, 0.1, 0.7, GUN)
    t += box(0.0, 0.92, 0.32, 0.06, 0.14, 0.08, GUN)
    # Backpack
    t += box(0, 1.15, -0.24, 0.4, 0.5, 0.18, ARMOR_DARK, top=AMBER)
    # Helmet: rounded shell with a wide wrap-around visor band
    t += ellipsoid(0, 1.62, 0, 0.17, 0.17, 0.18, ARMOR, rings=4, segments=8)
    t += box(0, 1.63, 0.1, 0.28, 0.07, 0.12, VISOR)
    return t


def bug_legs(count, body_y, spread, length, color, thickness=0.06, splay=0.6):
    t = []
    for i in range(count):
        z = (i - (count - 1) / 2) * spread
        for side in (-1, 1):
            upper = box(0, 0, 0, length * 0.6, thickness, thickness, color)
            upper = transform(upper, rot_z=-0.5 * side, offset=(side * (0.12 + length * 0.25), body_y, z))
            lower = box(0, 0, 0, thickness, body_y * 1.05, thickness, color)
            lower = transform(lower, rot_z=0.25 * side,
                              offset=(side * (0.12 + length * splay), body_y * 0.5, z))
            t += upper + lower
    return t


def skitter():
    """Fast swarmer, ~0.7 m long, low and spindly."""
    t = []
    t += ellipsoid(0, 0.28, -0.12, 0.16, 0.12, 0.24, CHITIN, rings=3, segments=6)   # abdomen
    t += ellipsoid(0, 0.3, 0.14, 0.11, 0.09, 0.12, CHITIN_LT, rings=3, segments=6)   # thorax
    t += ellipsoid(0, 0.3, 0.3, 0.08, 0.07, 0.08, CHITIN_DK, rings=3, segments=5)    # head
    t += box(0, 0.36, -0.18, 0.12, 0.04, 0.16, GLAND)                                # back gland
    for side in (-1, 1):
        t += transform(box(0, 0, 0, 0.03, 0.03, 0.14, CLAW), rot_y=0.4 * side,
                       offset=(0.05 * side, 0.27, 0.4))
    t += bug_legs(3, 0.28, 0.16, 0.42, CHITIN_DK, thickness=0.04)
    return t


def gnasher():
    """Standard warrior, ~1.3 m tall, scythe forelimbs."""
    t = []
    t += ellipsoid(0, 0.62, -0.35, 0.3, 0.26, 0.42, CHITIN, rings=4, segments=7)    # abdomen
    t += ellipsoid(0, 0.8, 0.1, 0.22, 0.24, 0.24, CHITIN_LT, rings=4, segments=7)    # thorax
    t += ellipsoid(0, 1.0, 0.36, 0.16, 0.14, 0.16, CHITIN_DK, rings=3, segments=6)   # head
    t += box(0, 0.86, -0.42, 0.2, 0.06, 0.4, GLAND)
    for side in (-1, 1):
        # Mandibles
        t += transform(box(0, 0, 0, 0.04, 0.04, 0.18, CLAW), rot_y=0.5 * side,
                       offset=(0.08 * side, 0.94, 0.52))
        # Scythe arms
        arm = box(0, 0, 0, 0.07, 0.5, 0.07, CHITIN_DK)
        t += transform(arm, rot_x=0.9, offset=(0.24 * side, 1.0, 0.32))
        blade = box(0, 0, 0, 0.04, 0.06, 0.5, CLAW)
        t += transform(blade, rot_x=-0.5, offset=(0.26 * side, 1.08, 0.66))
    t += bug_legs(2, 0.6, 0.34, 0.7, CHITIN_DK, thickness=0.07)
    return t


def bulwark():
    """Slow armored brute, ~2.2 m tall, heavy frontal plating."""
    t = []
    t += ellipsoid(0, 1.0, -0.6, 0.6, 0.52, 0.8, CHITIN, rings=4, segments=8)
    t += ellipsoid(0, 1.25, 0.3, 0.52, 0.5, 0.5, CHITIN_LT, rings=4, segments=8)
    t += ellipsoid(0, 1.2, 0.8, 0.28, 0.24, 0.24, CHITIN_DK, rings=3, segments=6)
    # Armor plates: a frontal shield and overlapping back plates
    t += transform(box(0, 0, 0, 1.2, 0.9, 0.12, PLATE, top=CHITIN_DK), rot_x=-0.35,
                   offset=(0, 1.45, 0.75))
    for k in range(3):
        t += box(0, 1.55 - k * 0.08, -0.15 - k * 0.42, 1.0 - k * 0.12, 0.12, 0.45, PLATE)
    for side in (-1, 1):
        t += box(0.42 * side, 1.62, -0.6, 0.16, 0.1, 0.5, GLAND)
        tusk = cylinder(0, 0, 0, 0.07, 0.5, 5, CLAW, radius_top=0.0)
        t += transform(tusk, rot_x=1.3, offset=(0.2 * side, 1.0, 0.95))
    t += bug_legs(3, 0.95, 0.5, 1.0, CHITIN_DK, thickness=0.14, splay=0.5)
    return t


def dropship():
    """Extraction dropship, ~9 m long, landing skids at y=0."""
    t = []
    t += box(0, 2.0, 0, 3.0, 1.8, 7.0, HULL, top=HULL_DARK)                      # fuselage
    t += transform(box(0, 0, 0, 2.6, 1.4, 2.0, HULL), rot_x=0.35, offset=(0, 2.2, 4.1))  # nose
    t += box(0, 2.6, 4.5, 1.6, 0.5, 1.0, GLASS)                                  # canopy
    t += box(0, 2.0, -3.8, 2.4, 1.2, 0.8, HULL_DARK)                             # tail
    t += box(0, 3.2, -3.4, 0.2, 1.4, 1.2, AMBER)                                 # fin
    for side in (-1, 1):
        t += box(2.6 * side, 2.4, 0, 2.6, 0.25, 2.4, HULL_DARK)                  # wings
        t += cylinder(3.6 * side, 1.8, 0.6, 0.55, 1.4, 8, HULL, cap_color=THRUSTER)
        t += cylinder(3.6 * side, 1.8, -0.8, 0.55, 1.4, 8, HULL, cap_color=THRUSTER)
        t += box(1.1 * side, 0.5, 0, 0.15, 1.0, 0.15, HULL_DARK)                 # struts
        t += box(1.1 * side, 0.06, 0, 0.3, 0.12, 5.0, HULL_DARK)                 # skids
    t += box(0, 1.2, -2.6, 2.0, 0.1, 1.6, AMBER)                                 # ramp stripe
    return t


def beacon():
    """Calldown beacon: a spiked canister with a glowing light, ~0.6 m."""
    t = []
    t += cylinder(0, 0.0, 0, 0.08, 0.06, 6, CHITIN_DK, radius_top=0.12)
    t += cylinder(0, 0.06, 0, 0.12, 0.34, 6, ARMOR, cap_color=ARMOR_DARK)
    t += box(0, 0.25, 0, 0.26, 0.05, 0.05, AMBER)
    t += cylinder(0, 0.4, 0, 0.05, 0.08, 6, ARMOR_DARK)
    t += ellipsoid(0, 0.52, 0, 0.07, 0.07, 0.07, THRUSTER, rings=3, segments=6)
    return t


def turret():
    """Sentry turret, ~1.4 m: tripod base, rotating head, twin barrels."""
    t = []
    for k in range(3):
        a = 2 * math.pi * k / 3
        leg = box(0, 0, 0, 0.08, 0.08, 0.7, ARMOR_DARK)
        t += transform(leg, rot_x=0.6, rot_y=a, offset=(math.sin(a) * 0.3, 0.25, math.cos(a) * 0.3))
    t += cylinder(0, 0.4, 0, 0.18, 0.4, 6, ARMOR, cap_color=ARMOR_DARK)
    t += box(0, 1.0, 0.05, 0.5, 0.36, 0.6, ARMOR, top=AMBER)
    t += box(0, 1.02, 0.36, 0.3, 0.08, 0.04, VISOR)
    for side in (-1, 1):
        t += box(0.12 * side, 0.98, 0.65, 0.07, 0.07, 0.7, GUN)
    return t


def rock(seed, base_color, size):
    rnd = random.Random(seed)
    table = {}

    def jitter(i, j):
        if (i, j) not in table:
            table[(i, j)] = 0.75 + rnd.random() * 0.45
        if j in (0,):  # keep the poles welded
            return table.setdefault((0, 0), table[(i, j)])
        return table[(i, j)]

    tris = ellipsoid(0, size * 0.35, 0, size * 0.6, size * 0.5, size * 0.55, base_color,
                     rings=3, segments=6, jitter=jitter)
    # Shade by facing: tops lighter, so rocks read well from a top-down camera.
    out = []
    for (p0, p1, p2), col in tris:
        n = v_norm(v_cross(v_sub(p1, p0), v_sub(p2, p0)))
        k = 0.8 + 0.3 * max(0.0, n[1])
        out.append(((p0, p1, p2), tuple(min(255, int(c * k)) for c in col)))
    return out


def arena_floor():
    """The whole 64x64 m arena floor, 4 m tiles with colour variation so
    movement reads on screen. The outermost ring of tiles is darker to mark
    the arena edge. Matches ARENA_HALF_SIZE in src/sim/tuning.h."""
    rnd = random.Random(99)
    half, tile = 32.0, 4.0
    n = int(2 * half / tile)
    tris = []
    for iz in range(n):
        for ix in range(n):
            x0, z0 = -half + ix * tile, -half + iz * tile
            x1, z1 = x0 + tile, z0 + tile
            k = 0.9 + rnd.random() * 0.14 + (0.04 if (ix + iz) % 2 else 0.0)
            if ix in (0, n - 1) or iz in (0, n - 1):
                k *= 0.7
            col = tuple(min(255, int(c * k)) for c in GROUND)
            tris += quad((x0, 0, z1), (x1, 0, z1), (x1, 0, z0), (x0, 0, z0), col)
    return tris


MODELS = {
    "soldier": soldier,
    "skitter": skitter,
    "gnasher": gnasher,
    "bulwark": bulwark,
    "dropship": dropship,
    "beacon": beacon,
    "turret": turret,
    "rock_a": lambda: rock(11, ROCK_A, 1.6),
    "rock_b": lambda: rock(23, ROCK_B, 2.4),
    "rock_c": lambda: rock(37, ROCK_C, 1.0),
    "ground": arena_floor,
}

# ---------------------------------------------------------------------------
# glTF writer


def srgb_to_linear(c):
    c = c / 255.0
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def fast64_material(name):
    """Material with the Fast64 extras Tiny3D's importer reads.

    1-cycle combiner, color = SHADE, alpha = 1. Values are raw RDP combiner
    inputs, same encoding Fast64 exports.
    """
    zero_cc = {"A": 8, "B": 8, "C": 16, "D": 4, "A_alpha": 7, "B_alpha": 7, "C_alpha": 7, "D_alpha": 6}
    pass_cc = {"A": 8, "B": 8, "C": 16, "D": 0, "A_alpha": 7, "B_alpha": 7, "C_alpha": 7, "D_alpha": 0}
    return {
        "name": name,
        "pbrMetallicRoughness": {"baseColorFactor": [1, 1, 1, 1], "metallicFactor": 0, "roughnessFactor": 1},
        "extras": {
            "f3d_mat": {
                "combiner1": zero_cc,
                "combiner2": pass_cc,
                "set_prim": 0,
                "set_env": 0,
                "set_blend": 0,
                "prim_color": [1.0, 1.0, 1.0, 1.0],
                "env_color": [0.0, 0.0, 0.0, 1.0],
                "blend_color": [0.0, 0.0, 0.0, 1.0],
                "draw_layer": {"oot": 0, "sm64": 1},
                "rdp_settings": {
                    "g_mdsft_cycletype": 0,   # 1-cycle
                    "g_cull_back": 1,
                    "g_cull_front": 0,
                    "g_fog": 0,
                    "g_mdsft_text_filt": 0,
                    "g_tex_gen": 0,
                    "set_rendermode": 0,      # opaque, via draw_layer
                },
            }
        },
    }


def build_glb(name, tris):
    positions, normals, colors = [], [], []
    for (p0, p1, p2), col in tris:
        n = v_norm(v_cross(v_sub(p1, p0), v_sub(p2, p0)))
        lin = [srgb_to_linear(c) for c in col] + [1.0]
        for p in (p0, p1, p2):
            positions.append(p)
            normals.append(n)
            colors.append(lin)
    count = len(positions)
    if count > 65535:
        raise ValueError(f"{name}: too many vertices ({count})")

    pos_bytes = b"".join(struct.pack("<3f", *p) for p in positions)
    nrm_bytes = b"".join(struct.pack("<3f", *n) for n in normals)
    col_bytes = b"".join(struct.pack("<4f", *c) for c in colors)
    idx_bytes = b"".join(struct.pack("<H", i) for i in range(count))
    if len(idx_bytes) % 4:
        idx_bytes += b"\0" * (4 - len(idx_bytes) % 4)

    views, offset = [], 0
    for blob, target in ((pos_bytes, 34962), (nrm_bytes, 34962), (col_bytes, 34962), (idx_bytes, 34963)):
        views.append({"buffer": 0, "byteOffset": offset, "byteLength": len(blob), "target": target})
        offset += len(blob)
    binary = pos_bytes + nrm_bytes + col_bytes + idx_bytes

    mins = [min(p[k] for p in positions) for k in range(3)]
    maxs = [max(p[k] for p in positions) for k in range(3)]
    gltf = {
        "asset": {"version": "2.0", "generator": "hivebreakers genmodels.py"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"name": name, "mesh": 0}],
        "meshes": [{"name": name, "primitives": [{
            "attributes": {"POSITION": 0, "NORMAL": 1, "COLOR_0": 2},
            "indices": 3,
            "material": 0,
        }]}],
        "materials": [fast64_material(name + "_mat")],
        "buffers": [{"byteLength": len(binary)}],
        "bufferViews": views,
        "accessors": [
            {"bufferView": 0, "componentType": 5126, "count": count, "type": "VEC3", "min": mins, "max": maxs},
            {"bufferView": 1, "componentType": 5126, "count": count, "type": "VEC3"},
            {"bufferView": 2, "componentType": 5126, "count": count, "type": "VEC4"},
            {"bufferView": 3, "componentType": 5123, "count": count, "type": "SCALAR"},
        ],
    }
    js = json.dumps(gltf, separators=(",", ":"), sort_keys=True).encode()
    js += b" " * ((4 - len(js) % 4) % 4)
    total = 12 + 8 + len(js) + 8 + len(binary)
    return (struct.pack("<III", 0x46546C67, 2, total)
            + struct.pack("<II", len(js), 0x4E4F534A) + js
            + struct.pack("<II", len(binary), 0x004E4942) + binary)


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 1
    out_dir = argv[1]
    names = argv[2:] or list(MODELS)
    os.makedirs(out_dir, exist_ok=True)
    for name in names:
        if name not in MODELS:
            print(f"unknown model '{name}'", file=sys.stderr)
            return 1
        tris = MODELS[name]()
        data = build_glb(name, tris)
        path = os.path.join(out_dir, name + ".glb")
        # Only touch the file when the content changes, so make stays incremental.
        try:
            with open(path, "rb") as f:
                if f.read() == data:
                    continue
        except FileNotFoundError:
            pass
        with open(path, "wb") as f:
            f.write(data)
        print(f"    [GEN] {path} ({len(tris)} tris)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
