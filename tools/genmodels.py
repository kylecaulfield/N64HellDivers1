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
  * Flat shaded by default. Parts added with smooth=True share averaged
    normals, so the importer can share their vertices (cheaper on the RSP).
  * Triangle budgets (everything is on screen at once, up to the enemy cap):
    skitter ~45, gnasher ~70, bulwark ~100, soldier ~100. Faces the fixed
    overhead camera can never see (most bottoms) are left out.
  * Materials carry Fast64 "f3d_mat" extras, because Tiny3D's importer refuses
    materials without them. The combiner is shade-only (vertex color x light);
    the game swaps the soldier's for one that puts the player's colour on
    vertices with alpha 0.
"""

import json
import math
import os
import random
import struct
import sys

# ---------------------------------------------------------------------------
# Tiny geometry kit. A mesh is a list of triangles: ((p0, p1, p2), rgb), with
# counter-clockwise winding seen from outside. Colors are sRGB 0-255, with an
# optional fourth value: vertex alpha 0-255 (255 when left out).

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


def box(cx, cy, cz, sx, sy, sz, color, top=None, bottom=None, top_face=True, bottom_face=True):
    """Axis-aligned box centred at (cx, cy, cz) with full sizes (sx, sy, sz)."""
    x0, x1 = cx - sx / 2, cx + sx / 2
    y0, y1 = cy - sy / 2, cy + sy / 2
    z0, z1 = cz - sz / 2, cz + sz / 2
    top = top or color
    bottom = bottom or color
    side = color
    tris = []
    if top_face:
        tris += quad((x0, y1, z1), (x1, y1, z1), (x1, y1, z0), (x0, y1, z0), top)     # +Y
    if bottom_face:
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


def bipyramid(cx, cy, cz, rx, ry, rz, color, sides=4, top=None, bottom_face=True):
    """Two pyramids base to base around a horizontal ring of `sides` points
    (the first at +Z): 2 * sides triangles, sides + 2 vertices. sides=4 is
    a stretched octahedron. Upper faces in `top`."""
    top = top or color
    ring = [(cx + math.sin(2 * math.pi * i / sides) * rx, cy,
             cz + math.cos(2 * math.pi * i / sides) * rz) for i in range(sides)]
    up, down = (cx, cy + ry, cz), (cx, cy - ry, cz)
    tris = []
    for i in range(sides):
        a, b = ring[i], ring[(i + 1) % sides]
        tris.append(((up, a, b), top))
        if bottom_face:
            tris.append(((down, b, a), color))
    return tris


def spike(base, tip, radius, color):
    """Three-sided spike from a small triangle around `base` to `tip`, open
    at the base (it sits inside whatever it grows from): 3 triangles."""
    d = v_norm(v_sub(tip, base))
    helper = (0.0, 1.0, 0.0) if abs(d[1]) < 0.9 else (1.0, 0.0, 0.0)
    u = v_norm(v_cross(d, helper))
    w = v_cross(d, u)
    pts = [v_add(base, v_add(v_scale(u, radius * math.cos(a)), v_scale(w, radius * math.sin(a))))
           for a in (0.0, 2 * math.pi / 3, 4 * math.pi / 3)]
    tris = []
    for i in range(3):
        a, b = pts[i], pts[(i + 1) % 3]
        n = v_cross(v_sub(b, a), v_sub(tip, a))
        centre = v_scale(v_add(v_add(a, b), tip), 1 / 3)
        axis = v_add(base, v_scale(d, sum(x * y for x, y in zip(v_sub(centre, base), d))))
        outward = v_sub(centre, axis)
        tris.append(((a, b, tip) if sum(x * y for x, y in zip(n, outward)) > 0 else (b, a, tip), color))
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
# Rigging. A Model holds triangles tagged with a bone, a skeleton and
# keyframed animations. Bones are listed in depth-first order (Tiny3D's
# importer numbers them that way) and every triangle follows exactly one bone.

class Model:
    def __init__(self):
        self.tris = []    # ((p0, p1, p2), rgb, bone, smooth group or -1)
        self.bones = []   # (name, parent index or -1, pivot in model space)
        self.anims = {}   # name -> (duration, [(bone, "rotation"|"translation", [(t, value)])])
        self.groups = 0

    def bone(self, name, parent, pivot):
        assert parent < len(self.bones)
        self.bones.append((name, parent, pivot))
        return len(self.bones) - 1

    def add(self, tris, bone=0, smooth=False):
        """smooth: the triangles of this call share averaged normals."""
        group = -1
        if smooth:
            group = self.groups
            self.groups += 1
        self.tris += [(pts, col, bone, group) for pts, col in tris]


def q_axis(axis, angle):
    s = math.sin(angle * 0.5)
    return (axis[0] * s, axis[1] * s, axis[2] * s, math.cos(angle * 0.5))


def q_mul(a, b):
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return (aw * bx + ax * bw + ay * bz - az * by,
            aw * by - ax * bz + ay * bw + az * bx,
            aw * bz + ax * by - ay * bx + az * bw,
            aw * bw - ax * bx - ay * by - az * bz)


def sample_loop(duration, steps, fn):
    """Keyframes [(t, fn(phase))] over one loop; the last equals the first."""
    return [(duration * k / steps, fn(2 * math.pi * k / steps)) for k in range(steps + 1)]


def sample_curve(duration, points, fn):
    """Keyframes at fractions of the duration: points = [(fraction, x)]."""
    return [(duration * f, fn(x)) for f, x in points]


def leg_rot(side, forward, lift):
    """Leg rotation about its hip: `forward` swings the foot towards +Z,
    `lift` raises it. Legs stick out sideways, `side` is -1 (left) or +1."""
    return q_mul(q_axis((0, 1, 0), -side * forward), q_axis((0, 0, 1), side * lift))


def add_walk(m, name, duration, root, root_pivot, legs, swing, lift, bob, extra=()):
    """Alternating gait: legs = [(bone, side, group)] with group 0 or 1."""
    ch = []
    for bone, side, group in legs:
        sgn = 1.0 if group == 0 else -1.0
        ch.append((bone, "rotation", sample_loop(duration, 12, lambda ph, side=side, sgn=sgn: leg_rot(
            side, swing * sgn * math.sin(ph), lift * max(0.0, sgn * math.cos(ph))))))
    ch.append((root, "translation", sample_loop(duration, 12, lambda ph: (
        root_pivot[0], root_pivot[1] + bob * (0.5 + 0.5 * math.cos(2 * ph)), root_pivot[2]))))
    ch += list(extra)
    m.anims[name] = (duration, ch)


def add_attack(m, name, duration, root, rear, strike, extra=()):
    """Rear up (nose up), strike down, settle. Fractions match the sim's
    windup -> hit -> recover rhythm roughly."""
    pts = [(0.0, 0.0), (0.55, rear), (0.75, strike), (1.0, 0.0)]
    ch = [(root, "rotation", sample_curve(duration, pts, lambda a: q_axis((1, 0, 0), a)))]
    ch += list(extra)
    m.anims[name] = (duration, ch)


# ---------------------------------------------------------------------------
# Palette (sRGB). Original designs: the player corps wears slate armor with
# accents in each trooper's colour, and amber hazard markings on its gear; the
# swarm ("the Gnash") is chitin-violet with acid-green glands.

ARMOR      = (88, 98, 112)
ARMOR_DARK = (52, 58, 68)
AMBER      = (232, 150, 40)
# The trooper's accents take the player's colour in game: white with vertex
# alpha 0 marks them (the renderer's combiner puts the colour there).
PLAYER_COLOR = (255, 255, 255, 0)
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
SHADOW     = (58, 46, 32)   # a blob under each unit: the lit ground, darkened
SOIL_DARK  = (92, 74, 52)
SAND       = (142, 122, 86)
RUST       = (132, 92, 62)
TUFT       = (96, 98, 48)
PEBBLE     = (104, 96, 88)


def shadow_disc(radius, y=0.03, sides=6):
    """A flat dark polygon on the ground under a unit: a fan from one corner,
    sides - 2 triangles, facing up."""
    pts = [(math.sin(2 * math.pi * k / sides) * radius, y, math.cos(2 * math.pi * k / sides) * radius)
           for k in range(sides)]
    return [((pts[0], pts[k], pts[k + 1]), SHADOW) for k in range(1, sides - 1)]


def shadow():
    """Unit blob shadow (radius 1 m) for things that tilt or fly: troopers
    (who pitch flat when diving), falling pods, the dropship. Scaled per use."""
    return shadow_disc(1.0, y=0.0)


def soldier():
    """Trooper, ~1.8 m, faces +Z. Rig: hips (root, carries torso, arms,
    rifle, pack, helmet) and two legs. Animations: run, idle."""
    m = Model()
    hips = m.bone("hips", -1, (0.0, 0.9, 0.0))
    legs = [m.bone("leg_l", hips, (-0.13, 0.84, 0.0)), m.bone("leg_r", hips, (0.13, 0.84, 0.0))]
    for leg, side in zip(legs, (-1, 1)):
        m.add(box(0.13 * side, 0.42, 0.0, 0.18, 0.84, 0.22, ARMOR_DARK, top_face=False, bottom_face=False), leg)
    t = []
    # Torso with a chest stripe in the player's colour
    t += box(0, 1.15, 0, 0.5, 0.62, 0.3, ARMOR, top=ARMOR_DARK, bottom_face=False)
    t += quad((-0.18, 1.16, 0.152), (0.18, 1.16, 0.152), (0.18, 1.28, 0.152), (-0.18, 1.28, 0.152), PLAYER_COLOR)
    for side in (-1, 1):
        t += box(0.33 * side, 1.38, 0, 0.18, 0.12, 0.3, PLAYER_COLOR, bottom_face=False)  # shoulder pads
        t += box(0.32 * side, 1.12, 0.12, 0.14, 0.42, 0.14, ARMOR_DARK,
                 top_face=False, bottom_face=False)                                      # arms
    t += box(0.0, 1.0, 0.45, 0.08, 0.1, 0.7, GUN, bottom_face=False)                     # rifle
    t += box(0, 1.15, -0.24, 0.4, 0.5, 0.18, ARMOR_DARK, top=PLAYER_COLOR, bottom_face=False)  # backpack
    t += quad((-0.12, 1.57, 0.165), (0.12, 1.57, 0.165), (0.12, 1.65, 0.165), (-0.12, 1.65, 0.165), VISOR)
    m.add(t, hips)
    m.add(bipyramid(0, 1.62, 0, 0.17, 0.17, 0.18, ARMOR, sides=6), hips, smooth=True)    # helmet

    hp = m.bones[hips][2]
    run_legs = [(leg, "rotation", sample_loop(0.6, 12, lambda ph, s=s: q_axis((1, 0, 0), -0.6 * s * math.sin(ph))))
                for leg, s in zip(legs, (1.0, -1.0))]
    m.anims["run"] = (0.6, run_legs + [
        (hips, "translation", sample_loop(0.6, 12, lambda ph: (hp[0], hp[1] + 0.05 * abs(math.sin(ph)), hp[2]))),
        (hips, "rotation", sample_loop(0.6, 12, lambda ph: q_axis((1, 0, 0), 0.12))),
    ])
    m.anims["idle"] = (2.0, [
        (hips, "translation", sample_loop(2.0, 8, lambda ph: (hp[0], hp[1] + 0.012 * math.sin(ph), hp[2]))),
    ])
    return m


def bug_legs(m, root, count, body_y, spread, length, color, thickness=0.06, splay=0.6, knee=True):
    """`count` pairs of legs along the body, each a spike from hip to foot
    (two, hip-knee-foot, with knee=True). Legs that move together (same side,
    same gait group) share one bone pivoting at their mean hip: every bone is
    an RSP matrix load per bug drawn. Returns [(bone, side, gait group)];
    the groups are alternating tripods (diagonal pairs for four legs)."""
    groups = {}
    for i in range(count):
        z = (i - (count - 1) / 2) * spread
        for side in (-1, 1):
            groups.setdefault((side, (i + (side > 0)) % 2), []).append(z)
    out = []
    for (side, group), zs in sorted(groups.items()):
        name = f"legs_{'l' if side < 0 else 'r'}{group}"
        bone = m.bone(name, root, (side * 0.12, body_y, sum(zs) / len(zs)))
        for z in zs:
            hip = (side * 0.12, body_y, z)
            foot = (side * (0.12 + length * splay), 0.0, z * 1.4)
            if knee:
                kn = (side * (0.12 + length * 0.4), body_y * 1.2, z * 1.2)
                m.add(spike(hip, kn, thickness, color), bone, smooth=True)
                m.add(spike(kn, foot, thickness * 0.8, color), bone, smooth=True)
            else:
                m.add(spike(hip, foot, thickness, color), bone, smooth=True)
        out.append((bone, side, group))
    return out


def skitter():
    """Fast swarmer, ~0.7 m long, low and spindly. Animations: walk, attack."""
    m = Model()
    pivot = (0.0, 0.28, 0.0)
    root = m.bone("body", -1, pivot)
    m.add(bipyramid(0, 0.28, -0.12, 0.16, 0.12, 0.26, CHITIN), root, smooth=True)     # abdomen
    m.add(bipyramid(0, 0.3, 0.16, 0.11, 0.09, 0.14, CHITIN_LT), root, smooth=True)    # thorax
    m.add(spike((0, 0.3, 0.26), (0, 0.27, 0.44), 0.07, CHITIN_DK), root, smooth=True)     # head
    m.add(spike((0, 0.36, -0.12), (0, 0.46, -0.28), 0.06, GLAND), root, smooth=True)      # gland sac
    for side in (-1, 1):
        m.add(spike((0.04 * side, 0.27, 0.36), (0.08 * side, 0.25, 0.52), 0.025, CLAW), root,
              smooth=True)                                                               # claws
    legs = bug_legs(m, root, 3, 0.28, 0.16, 0.42, CHITIN_DK, thickness=0.035, knee=False)
    m.add(shadow_disc(0.42), root)
    add_walk(m, "walk", 0.36, root, pivot, legs, swing=0.45, lift=0.35, bob=0.02)
    add_attack(m, "attack", 0.4, root, rear=-0.35, strike=0.3)
    return m


def gnasher():
    """Standard warrior, ~1.3 m tall, scythe forelimbs. Animations: walk, attack."""
    m = Model()
    pivot = (0.0, 0.7, 0.0)
    root = m.bone("body", -1, pivot)
    m.add(bipyramid(0, 0.62, -0.35, 0.3, 0.26, 0.42, CHITIN), root, smooth=True)      # abdomen
    m.add(bipyramid(0, 0.8, 0.1, 0.22, 0.24, 0.24, CHITIN_LT), root, smooth=True)     # thorax
    m.add(bipyramid(0, 1.0, 0.36, 0.15, 0.13, 0.16, CHITIN_DK), root, smooth=True)    # head
    m.add(spike((0, 0.8, -0.4), (0, 0.98, -0.62), 0.1, GLAND), root, smooth=True)         # gland sac
    for side in (-1, 1):
        m.add(spike((0.07 * side, 0.95, 0.46), (0.03 * side, 0.92, 0.62), 0.03, CLAW), root,
              smooth=True)                                                               # mandibles
    # Both scythes on one bone: they sway and strike together.
    arms = m.bone("arms", root, (0.0, 1.0, 0.22))
    for side in (-1, 1):
        elbow = (0.26 * side, 1.18, 0.5)
        m.add(spike((0.24 * side, 1.0, 0.22), elbow, 0.05, CHITIN_DK), arms, smooth=True)
        m.add(spike(elbow, (0.28 * side, 0.95, 0.92), 0.045, CLAW), arms, smooth=True)
    legs = bug_legs(m, root, 2, 0.6, 0.34, 0.7, CHITIN_DK, thickness=0.07, knee=False)
    m.add(shadow_disc(0.62), root)
    sway = [(arms, "rotation", sample_loop(0.6, 12, lambda ph: q_axis((1, 0, 0), 0.12 * math.sin(ph))))]
    add_walk(m, "walk", 0.6, root, pivot, legs, swing=0.4, lift=0.3, bob=0.03, extra=sway)
    raise_strike = [(arms, "rotation", sample_curve(0.6, [(0.0, 0.0), (0.55, -1.0), (0.75, 0.5), (1.0, 0.0)],
                                                    lambda x: q_axis((1, 0, 0), x)))]
    add_attack(m, "attack", 0.6, root, rear=-0.15, strike=0.15, extra=raise_strike)
    return m


def tent(cx, cy, cz, w, h, length, color):
    """Two sloped roof faces, eaves at cy, ridge h above along Z: 4 triangles."""
    x0, x1, z0, z1, top = cx - w / 2, cx + w / 2, cz - length / 2, cz + length / 2, cy + h
    return (quad((x1, cy, z1), (x1, cy, z0), (cx, top, z0), (cx, top, z1), color) +
            quad((x0, cy, z0), (x0, cy, z1), (cx, top, z1), (cx, top, z0), color))


def bulwark():
    """Slow armored brute, ~2.2 m tall, heavy frontal plating. Animations: walk, attack."""
    m = Model()
    pivot = (0.0, 1.1, 0.0)
    root = m.bone("body", -1, pivot)
    m.add(bipyramid(0, 1.0, -0.6, 0.6, 0.52, 0.8, CHITIN, sides=6), root, smooth=True)
    m.add(bipyramid(0, 1.25, 0.3, 0.52, 0.5, 0.5, CHITIN_LT), root, smooth=True)
    m.add(bipyramid(0, 1.2, 0.8, 0.28, 0.24, 0.24, CHITIN_DK), root, smooth=True)
    t = []
    # Armor plates: a frontal shield and overlapping back plates
    t += transform(box(0, 0, 0, 1.2, 0.9, 0.12, PLATE, top=CHITIN_DK, bottom_face=False), rot_x=-0.35,
                   offset=(0, 1.45, 0.75))
    m.add(t, root)
    for k in range(3):
        m.add(tent(0, 1.45 - k * 0.1, -0.15 - k * 0.42, 1.0 - k * 0.12, 0.22, 0.45, PLATE), root, smooth=True)
    for side in (-1, 1):
        m.add(spike((0.36 * side, 1.4, -0.6), (0.46 * side, 1.75, -0.7), 0.1, GLAND), root, smooth=True)
        m.add(spike((0.2 * side, 1.0, 0.9), (0.24 * side, 0.85, 1.4), 0.07, CLAW), root, smooth=True)
    legs = bug_legs(m, root, 3, 0.95, 0.5, 1.0, CHITIN_DK, thickness=0.14, splay=0.5)
    m.add(shadow_disc(0.95), root)
    add_walk(m, "walk", 1.1, root, pivot, legs, swing=0.3, lift=0.25, bob=0.05)
    add_attack(m, "attack", 1.0, root, rear=-0.35, strike=0.2)
    return m


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


def crate():
    """Resupply crate, ~0.9 m: amber-banded box with a lid."""
    t = []
    t += box(0, 0.35, 0, 0.9, 0.7, 0.7, ARMOR_DARK, top=ARMOR, bottom_face=False)
    for x in (-0.3, 0.3):
        t += box(x, 0.36, 0, 0.12, 0.72, 0.72, AMBER, bottom_face=False)    # straps
    t += box(0, 0.74, 0, 0.5, 0.06, 0.4, AMBER, bottom_face=False)          # lid handle
    return t


def mgcase():
    """Machine-gun case, ~1.3 m long: flat dark case with a visor-blue stripe."""
    t = []
    t += box(0, 0.18, 0, 0.5, 0.36, 1.3, GUN, top=ARMOR_DARK, bottom_face=False)
    t += box(0, 0.37, 0, 0.12, 0.03, 1.1, VISOR, bottom_face=False)
    for z in (-0.4, 0.4):
        t += box(0, 0.2, z, 0.54, 0.1, 0.1, AMBER, bottom_face=False)       # latches
    return t


def pod():
    """Reinforcement drop pod, ~2.2 m tall: hull, amber nose cone, fins."""
    t = []
    t += cylinder(0, 0.0, 0, 0.55, 1.4, 6, HULL, cap_color=HULL_DARK)
    t += cylinder(0, 1.4, 0, 0.55, 0.8, 6, AMBER, radius_top=0.0)
    for k in range(3):
        a = 2 * math.pi * k / 3
        fin = box(0, 0, 0, 0.08, 0.7, 0.45, HULL_DARK, bottom_face=False)
        t += transform(fin, rot_y=a, offset=(math.sin(a) * 0.6, 0.35, math.cos(a) * 0.6))
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
    """The whole 64x64 m arena floor: a 4 m grid of shared vertices with
    smooth colour variation (darker soil, sand and rusty patches) so movement
    reads on screen, a darker outer ring marking the edge, and scattered
    grass tufts and pebbles. Matches ARENA_HALF_SIZE in src/sim/tuning.h."""
    rnd = random.Random(99)
    half, tile = 32.0, 4.0
    n = int(2 * half / tile)
    # Patch "noise": a few soft blobs of each colour.
    blobs = [(rnd.uniform(-half, half), rnd.uniform(-half, half), rnd.uniform(5, 11), c)
             for c in [SOIL_DARK] * 7 + [SAND] * 6 + [RUST] * 4]

    def colour(x, z):
        c = list(GROUND)
        for bx, bz, r, bc in blobs:
            w = max(0.0, 1.0 - math.hypot(x - bx, z - bz) / r) ** 2
            c = [a + (b - a) * w for a, b in zip(c, bc)]
        k = 0.94 + 0.12 * rnd.random()
        if abs(x) >= half - 0.01 or abs(z) >= half - 0.01:
            k *= 0.62
        elif abs(x) >= half - tile - 0.01 or abs(z) >= half - tile - 0.01:
            k *= 0.8
        return tuple(min(255, int(v * k)) for v in c)

    corner = {}
    for iz in range(n + 1):
        for ix in range(n + 1):
            x, z = -half + ix * tile, -half + iz * tile
            corner[(ix, iz)] = ((x, 0.0, z), colour(x, z))
    tris = []
    for iz in range(n):
        for ix in range(n):
            a, b = corner[(ix, iz + 1)], corner[(ix + 1, iz + 1)]
            c, d = corner[(ix + 1, iz)], corner[(ix, iz)]
            tris.append(((a[0], b[0], c[0]), (a[1], b[1], c[1])))
            tris.append(((a[0], c[0], d[0]), (a[1], c[1], d[1])))
    # Decoration: tiny, so it costs little, and away from the edge ring.
    for _ in range(90):
        x, z = rnd.uniform(-half + 5, half - 5), rnd.uniform(-half + 5, half - 5)
        a = rnd.uniform(0, math.pi)
        dx, dz = math.cos(a) * 0.18, math.sin(a) * 0.18
        h = rnd.uniform(0.18, 0.32)
        tris.append((((x - dx, 0.0, z - dz), (x + dx, 0.0, z + dz), (x, h, z)), TUFT))
        tris.append((((x + dx, 0.0, z + dz), (x - dx, 0.0, z - dz), (x + dz * 0.5, h * 0.8, z - dx * 0.5)), TUFT))
    for _ in range(50):
        x, z = rnd.uniform(-half + 5, half - 5), rnd.uniform(-half + 5, half - 5)
        r = rnd.uniform(0.12, 0.25)
        top = (x, r * 0.8, z)
        base = [(x + math.sin(k * 2.1) * r, 0.0, z + math.cos(k * 2.1) * r) for k in range(3)]
        shade = tuple(int(v * rnd.uniform(0.85, 1.1)) for v in PEBBLE)
        for k in range(3):
            tris.append(((base[k], base[(k + 1) % 3], top), shade))
    return tris


MODELS = {
    "soldier": soldier,
    "skitter": skitter,
    "gnasher": gnasher,
    "bulwark": bulwark,
    "dropship": dropship,
    "beacon": beacon,
    "turret": turret,
    "crate": crate,
    "mgcase": mgcase,
    "pod": pod,
    "rock_a": lambda: rock(11, ROCK_A, 1.6),
    "rock_b": lambda: rock(23, ROCK_B, 2.4),
    "rock_c": lambda: rock(37, ROCK_C, 1.0),
    "ground": arena_floor,
    "shadow": shadow,
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


class GlbWriter:
    """Accumulates buffer views and accessors for one binary glTF."""

    def __init__(self):
        self.blob = b""
        self.views = []
        self.accessors = []

    def accessor(self, data, component, count, kind, target=None, minv=None, maxv=None):
        pad = (4 - len(self.blob) % 4) % 4
        self.blob += b"\0" * pad
        view = {"buffer": 0, "byteOffset": len(self.blob), "byteLength": len(data)}
        if target:
            view["target"] = target
        self.blob += data
        self.views.append(view)
        acc = {"bufferView": len(self.views) - 1, "componentType": component, "count": count, "type": kind}
        if minv is not None:
            acc["min"], acc["max"] = minv, maxv
        self.accessors.append(acc)
        return len(self.accessors) - 1


def build_glb(name, model):
    tris, bones = model.tris, model.bones

    def key(p, group):
        return (group, round(p[0], 5), round(p[1], 5), round(p[2], 5))

    # Smooth groups: area-weighted face normals summed per shared position.
    smooth = {}
    for (p0, p1, p2), col, bone, group in tris:
        if group < 0:
            continue
        n = v_cross(v_sub(p1, p0), v_sub(p2, p0))
        for p in (p0, p1, p2):
            smooth[key(p, group)] = v_add(smooth.get(key(p, group), (0.0, 0.0, 0.0)), n)

    positions, normals, colors, joints = [], [], [], []
    for (p0, p1, p2), col, bone, group in tris:
        face = v_norm(v_cross(v_sub(p1, p0), v_sub(p2, p0)))
        # A colour per triangle, or (c0, c1, c2) per vertex.
        per_vertex = isinstance(col[0], (tuple, list))
        for k, p in enumerate((p0, p1, p2)):
            c = col[k] if per_vertex else col
            positions.append(p)
            normals.append(v_norm(smooth[key(p, group)]) if group >= 0 else face)
            colors.append([srgb_to_linear(v) for v in c[:3]] + [c[3] / 255.0 if len(c) > 3 else 1.0])
            joints.append(bone)
    count = len(positions)
    if count > 65535:
        raise ValueError(f"{name}: too many vertices ({count})")

    w = GlbWriter()
    VEC_ARRAY, INDEX_ARRAY, FLOAT, UBYTE, USHORT = 34962, 34963, 5126, 5121, 5123
    attrs = {
        "POSITION": w.accessor(b"".join(struct.pack("<3f", *p) for p in positions), FLOAT, count, "VEC3",
                               VEC_ARRAY, [min(p[k] for p in positions) for k in range(3)],
                               [max(p[k] for p in positions) for k in range(3)]),
        "NORMAL": w.accessor(b"".join(struct.pack("<3f", *n) for n in normals), FLOAT, count, "VEC3", VEC_ARRAY),
        "COLOR_0": w.accessor(b"".join(struct.pack("<4f", *c) for c in colors), FLOAT, count, "VEC4", VEC_ARRAY),
    }
    if bones:
        attrs["JOINTS_0"] = w.accessor(b"".join(struct.pack("<4B", j, 0, 0, 0) for j in joints),
                                       UBYTE, count, "VEC4", VEC_ARRAY)
        attrs["WEIGHTS_0"] = w.accessor(struct.pack("<4f", 1, 0, 0, 0) * count, FLOAT, count, "VEC4", VEC_ARRAY)
    indices = w.accessor(b"".join(struct.pack("<H", i) for i in range(count)), USHORT, count, "SCALAR", INDEX_ARRAY)

    nodes = [{"name": name, "mesh": 0}]
    scene_nodes = [0]
    gltf = {
        "asset": {"version": "2.0", "generator": "hivebreakers genmodels.py"},
        "scene": 0,
        "meshes": [{"name": name, "primitives": [{"attributes": attrs, "indices": indices, "material": 0}]}],
        "materials": [fast64_material(name + "_mat")],
    }

    if bones:
        # Bone nodes follow the mesh node, in the same (depth-first) order.
        for i, (bname, parent, pivot) in enumerate(bones):
            ppos = bones[parent][2] if parent >= 0 else (0.0, 0.0, 0.0)
            nodes.append({"name": bname, "translation": list(v_sub(pivot, ppos))})
        for i, (bname, parent, pivot) in enumerate(bones):
            if parent >= 0:
                nodes[1 + parent].setdefault("children", []).append(1 + i)
        ibm = b"".join(struct.pack("<16f", 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0,
                                   -piv[0], -piv[1], -piv[2], 1) for _, _, piv in bones)
        gltf["skins"] = [{"joints": [1 + i for i in range(len(bones))], "skeleton": 1,
                          "inverseBindMatrices": w.accessor(ibm, FLOAT, len(bones), "MAT4")}]
        nodes[0]["skin"] = 0
        scene_nodes.append(1)

        anims = []
        for aname, (duration, channels) in sorted(model.anims.items()):
            samplers, chans = [], []
            for bone, path, keys in channels:
                times = [k[0] for k in keys]
                fmt = "<4f" if path == "rotation" else "<3f"
                t_acc = w.accessor(b"".join(struct.pack("<f", t) for t in times), FLOAT, len(times),
                                   "SCALAR", None, [min(times)], [max(times)])
                v_acc = w.accessor(b"".join(struct.pack(fmt, *k[1]) for k in keys), FLOAT, len(keys),
                                   "VEC4" if path == "rotation" else "VEC3")
                samplers.append({"input": t_acc, "output": v_acc, "interpolation": "LINEAR"})
                chans.append({"sampler": len(samplers) - 1, "target": {"node": 1 + bone, "path": path}})
            anims.append({"name": aname, "samplers": samplers, "channels": chans})
        gltf["animations"] = anims

    gltf["nodes"] = nodes
    gltf["scenes"] = [{"nodes": scene_nodes}]
    gltf["buffers"] = [{"byteLength": len(w.blob)}]
    gltf["bufferViews"] = w.views
    gltf["accessors"] = w.accessors
    js = json.dumps(gltf, separators=(",", ":"), sort_keys=True).encode()
    js += b" " * ((4 - len(js) % 4) % 4)
    binary = w.blob + b"\0" * ((4 - len(w.blob) % 4) % 4)
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
        built = MODELS[name]()
        if not isinstance(built, Model):  # static model: one implicit bone-less mesh
            m = Model()
            m.add(built, 0)
            built = m
        data = build_glb(name, built)
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
        rig = f", {len(built.bones)} bones, anims {sorted(built.anims)}" if built.bones else ""
        print(f"    [GEN] {path} ({len(built.tris)} tris{rig})")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
