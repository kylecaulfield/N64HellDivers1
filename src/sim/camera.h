#ifndef SIM_CAMERA_H
#define SIM_CAMERA_H

// Shared camera at a fixed top-down angle, looking north. It frames all
// living players, zooming out (moving back along its view direction) up to
// CAM_DIST_MAX. Past that, players are leashed to the visible ground.
//
// The visible ground is a trapezoid (wider at the top of the screen). All
// extents are linear in the camera distance, so they are computed as
// distance * constant. The leash uses the narrowest width (the screen's
// bottom edge), so a leashed player is always fully on screen.

#include "sim.h"

typedef struct {
    float below;  // ground visible below (south of) the target, per metre of distance
    float above;  // ground visible above (north of) the target
    float half_w; // half-width at the bottom screen edge (the narrowest)
    float half_w_top; // half-width at the top screen edge (the widest)
} cam_extent_t;

cam_extent_t camera_extent(void);

// Snap the camera to frame the living players (used at spawn).
void camera_reset(sim_t *s);
// Ease towards the framing target and recompute the leash rectangle.
void camera_update(sim_t *s);
// Recompute the leash from where the living players are right now. Called
// before players move, so the one walking away hits the edge and the others
// are not dragged along.
void camera_update_leash(sim_t *s);
// Visible ground rectangle (conservative) at the camera's current distance,
// grown by `pad`. The renderer uses it for culling.
void camera_view_rect(const camera_t *c, float pad, vec2 *min, vec2 *max);

#endif
