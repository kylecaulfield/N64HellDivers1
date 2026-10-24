#include "camera.h"

#include <math.h>

static cam_extent_t compute_extent(void)
{
    // Per metre of camera distance: camera height h and horizontal offset
    // (south of the target) o. A ray at angle a below the horizon hits the
    // ground h / tan(a) in front of the camera.
    const float p = CAM_PITCH_RAD, hf = CAM_FOV_Y_RAD * 0.5f;
    float h = sinf(p), o = cosf(p);
    float bottom = h / tanf(p + hf);      // from the camera's ground point
    float top = h / tanf(p - hf);
    // Depth along the view axis of an edge's ground point, times the
    // horizontal half-FOV, gives the half-width there.
    float bottom_depth = (h / sinf(p + hf)) * cosf(hf);
    float top_depth = (h / sinf(p - hf)) * cosf(hf);
    float tan_hx = tanf(hf) * CAM_ASPECT;
    return (cam_extent_t){
        .below = o - bottom,
        .above = top - o,
        .half_w = bottom_depth * tan_hx,
        .half_w_top = top_depth * tan_hx,
    };
}

cam_extent_t camera_extent(void)
{
    // Depends only on constants; the trig is done once.
    static cam_extent_t cached;
    static bool ready;
    if (!ready) {
        cached = compute_extent();
        ready = true;
    }
    return cached;
}

// Desired target and distance to frame every living player.
static bool framing(const sim_t *s, vec2 *target, float *dist)
{
    vec2 mn = v2(0.0f, 0.0f), mx = v2(0.0f, 0.0f);
    bool any = false;
    for (int i = 0; i < SIM_MAX_PLAYERS; i++) {
        const player_t *p = &s->players[i];
        if (!sim_player_alive(p))
            continue;
        if (!any) {
            mn = mx = p->pos;
            any = true;
        } else {
            mn = v2(fminf(mn.x, p->pos.x), fminf(mn.y, p->pos.y));
            mx = v2(fmaxf(mx.x, p->pos.x), fmaxf(mx.y, p->pos.y));
        }
    }
    if (!any)
        return false;

    cam_extent_t e = camera_extent();
    *target = v2_scale(v2_add(mn, mx), 0.5f);
    // The view is narrower below the target than above it, so the vertical
    // fit uses the bottom extent on both sides (keeps the group centred).
    float need_v = ((mx.y - mn.y) * 0.5f + CAM_MARGIN) / e.below;
    float need_h = ((mx.x - mn.x) * 0.5f + CAM_MARGIN) / e.half_w;
    *dist = clampf(fmaxf(need_v, need_h), CAM_DIST_MIN, CAM_DIST_MAX);
    if (s->settings.force_max_zoom)
        *dist = CAM_DIST_MAX;
    return true;
}

// The leash is centred on the group itself, not on the (smoothed) camera,
// so it only limits how far apart players can get. Using the camera's
// lagging position would yank a player who just appeared far away.
static void update_leash(sim_t *s, vec2 center)
{
    // Players may roam the area visible at full zoom-out, minus the margin.
    cam_extent_t e = camera_extent();
    camera_t *c = &s->camera;
    float hw = CAM_DIST_MAX * e.half_w - CAM_MARGIN;
    float lo = CAM_DIST_MAX * e.below - CAM_MARGIN;
    c->leash_min = v2(center.x - hw, center.y - lo);
    c->leash_max = v2(center.x + hw, center.y + lo);
}

void camera_reset(sim_t *s)
{
    vec2 t;
    float d;
    if (framing(s, &t, &d)) {
        s->camera.target = t;
        s->camera.dist = d;
    } else {
        t = s->camera.target;
        if (s->camera.dist <= 0.0f)
            s->camera.dist = CAM_DIST_MIN;
    }
    update_leash(s, t);
}

void camera_update(sim_t *s)
{
    vec2 t;
    float d;
    if (framing(s, &t, &d)) {
        camera_t *c = &s->camera;
        c->target = v2_lerp(c->target, t, CAM_SMOOTH);
        c->dist += (d - c->dist) * CAM_SMOOTH;
    } else {
        t = s->camera.target;
    }
    update_leash(s, t);
}

void camera_update_leash(sim_t *s)
{
    vec2 t;
    float d;
    if (!framing(s, &t, &d))
        t = s->camera.target;
    update_leash(s, t);
}

void camera_view_rect(const camera_t *c, float pad, vec2 *min, vec2 *max)
{
    cam_extent_t e = camera_extent();
    // Use the wide (top) width so nothing visible is culled.
    float hw = c->dist * e.half_w_top + pad;
    *min = v2(c->target.x - hw, c->target.y - c->dist * e.below - pad);
    *max = v2(c->target.x + hw, c->target.y + c->dist * e.above + pad);
}
