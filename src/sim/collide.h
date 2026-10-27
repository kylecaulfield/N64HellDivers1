#ifndef SIM_COLLIDE_H
#define SIM_COLLIDE_H

#include <stdbool.h>

#include "vec2.h"

// First intersection of the segment p0->p1 with a circle, as a fraction of
// the segment in [0, 1]. A segment starting inside the circle hits at 0.
static inline bool segment_circle(vec2 p0, vec2 p1, vec2 c, float r, float *t_out)
{
    vec2 d = v2_sub(p1, p0);
    vec2 f = v2_sub(p0, c);
    float cc = v2_dot(f, f) - r * r;
    if (cc <= 0.0f) {
        *t_out = 0.0f;
        return true;
    }
    float a = v2_dot(d, d);
    if (a < 1e-12f)
        return false;
    float b = 2.0f * v2_dot(f, d);
    float disc = b * b - 4.0f * a * cc;
    if (disc < 0.0f)
        return false;
    float t = (-b - sqrtf(disc)) / (2.0f * a);
    if (t < 0.0f || t > 1.0f)
        return false;
    *t_out = t;
    return true;
}

// Push a circle at *pos (radius r) out of a static circle. True if it moved.
static inline bool push_out_circle(vec2 *pos, float r, vec2 c, float cr)
{
    vec2 d = v2_sub(*pos, c);
    float min = r + cr;
    float d2 = v2_len2(d);
    if (d2 >= min * min)
        return false;
    float len = sqrtf(d2);
    vec2 n = len > 1e-6f ? v2_scale(d, 1.0f / len) : v2(1.0f, 0.0f);
    *pos = v2_add(c, v2_scale(n, min));
    return true;
}

#endif
