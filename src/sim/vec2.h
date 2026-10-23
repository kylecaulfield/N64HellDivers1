#ifndef SIM_VEC2_H
#define SIM_VEC2_H

// 2D vector math for the sim. The sim lives on the ground plane:
// x = east (screen right), y = north (screen up). Units are metres.

#include <math.h>

typedef struct {
    float x, y;
} vec2;

static inline vec2 v2(float x, float y) { return (vec2){ x, y }; }
static inline vec2 v2_add(vec2 a, vec2 b) { return v2(a.x + b.x, a.y + b.y); }
static inline vec2 v2_sub(vec2 a, vec2 b) { return v2(a.x - b.x, a.y - b.y); }
static inline vec2 v2_scale(vec2 a, float s) { return v2(a.x * s, a.y * s); }
static inline float v2_dot(vec2 a, vec2 b) { return a.x * b.x + a.y * b.y; }
static inline float v2_cross(vec2 a, vec2 b) { return a.x * b.y - a.y * b.x; }
static inline float v2_len2(vec2 a) { return a.x * a.x + a.y * a.y; }
static inline float v2_len(vec2 a) { return sqrtf(v2_len2(a)); }
static inline float v2_dist2(vec2 a, vec2 b) { return v2_len2(v2_sub(a, b)); }

// Unit vector in the direction of `a`, or `fallback` if `a` is ~zero.
static inline vec2 v2_norm_or(vec2 a, vec2 fallback)
{
    float l2 = v2_len2(a);
    if (l2 < 1e-12f)
        return fallback;
    return v2_scale(a, 1.0f / sqrtf(l2));
}

static inline vec2 v2_lerp(vec2 a, vec2 b, float t)
{
    return v2(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t);
}

// Rotate counter-clockwise by `rad`.
static inline vec2 v2_rotate(vec2 a, float rad)
{
    float c = cosf(rad), s = sinf(rad);
    return v2(a.x * c - a.y * s, a.x * s + a.y * c);
}

static inline float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

#endif
