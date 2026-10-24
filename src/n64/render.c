#include "render.h"
#include "camera.h"
#include "fx.h"
#include "gpu_timing.h"
#include "mission.h"
#include "palette.h"

#include <libdragon.h>
#include <stdio.h>
#include <t3d/t3d.h>
#include <t3d/t3danim.h>
#include <t3d/t3dmodel.h>
#include <t3d/t3dskeleton.h>
#include <t3d/tpx.h>

// Draws the sim state: floor, rocks, players, bugs and calldown props
// (beacons, drops, turrets, pickups) as Tiny3D models, and everything small
// (bullets, effects, beacon lights, telegraphs, aim markers) as tpx
// particles: one opaque pass, then one semi-transparent pass for explosion
// cores. Camera shake and the explosion flash come from fx.
//
// Bugs and soldiers are skinned. Bugs of one type share POSE_SLOTS
// skeletons; each bug just points the RSP at the slot it uses before its
// draw block runs (t3d_skeleton_use), so animating 200 bugs costs the same
// as animating 7. Bugs are drawn grouped by type: the type's material once,
// then a geometry-only block per bug (the pattern of Tiny3D's 17_culling
// example), so the RSP does not switch to rdpq mode changes per bug.

// Bug models must be in enemy_type_t order, right after the soldier.
enum {
    MDL_SOLDIER, MDL_SKITTER, MDL_GNASHER, MDL_BULWARK,
    MDL_ROCK_A, MDL_ROCK_B, MDL_ROCK_C, MDL_GROUND,
    MDL_BEACON, MDL_TURRET, MDL_CRATE, MDL_MGCASE, MDL_POD, MDL_DROPSHIP, MDL_COUNT
};

static const char *const model_names[MDL_COUNT] = {
    "soldier", "skitter", "gnasher", "bulwark", "rock_a", "rock_b", "rock_c", "ground",
    "beacon", "turret", "crate", "mgcase", "pod", "dropship",
};

// What falls from orbit for each calldown (-1: nothing, it is a strike), and
// the colour of its beacon light.
static const int8_t drop_model[CALLDOWN_COUNT] = {
    [CALLDOWN_REINFORCE] = MDL_POD, [CALLDOWN_RESUPPLY] = MDL_CRATE,
    [CALLDOWN_MACHINE_GUN] = MDL_MGCASE, [CALLDOWN_AIRSTRIKE] = -1,
    [CALLDOWN_BARRAGE] = -1, [CALLDOWN_SENTRY] = MDL_TURRET, [CALLDOWN_EXTRACTION] = -1,
};
static const uint8_t beacon_rgb[CALLDOWN_COUNT][3] = {
    [CALLDOWN_REINFORCE] = { 90, 220, 255 }, [CALLDOWN_RESUPPLY] = { 120, 255, 120 },
    [CALLDOWN_MACHINE_GUN] = { 255, 200, 60 }, [CALLDOWN_AIRSTRIKE] = { 255, 70, 50 },
    [CALLDOWN_BARRAGE] = { 255, 120, 40 }, [CALLDOWN_SENTRY] = { 255, 240, 90 },
    [CALLDOWN_EXTRACTION] = { 230, 240, 255 },
};

typedef struct {
    T3DSkeleton skel;
    T3DAnim anim;
} pose_slot_t;

static pose_slot_t bug_pose[ENEMY_TYPE_COUNT][POSE_SLOTS];

static struct {
    T3DSkeleton skel;
    T3DAnim run, idle;
    bool running;
} soldier_pose[SIM_MAX_PLAYERS];

static render_stats_t stats;

#define MODEL_SCALE (WORLD_SCALE / 64.0f)
#define MAX_PROPS (SIM_MAX_BEACONS * 2 + SIM_MAX_TURRETS + SIM_MAX_PICKUPS + SIM_MAX_PLAYERS + 1)
#define MAX_DYNAMIC (SIM_MAX_PLAYERS + SIM_MAX_ENEMIES + MAX_PROPS)
#define TELEGRAPH_DOTS 16
#define MAX_PARTICLES (SIM_MAX_PROJECTILES * 2 + FX_MAX + SIM_MAX_PLAYERS * 3 + \
                       SIM_MAX_BEACONS * (4 + TELEGRAPH_DOTS) + SIM_MAX_STRIKES * TELEGRAPH_DOTS + \
                       SIM_MAX_PICKUPS + (MISSION_ZONES + 1) * (TELEGRAPH_DOTS + 9) + 4)
#define PARTICLE_PAIRS (MAX_PARTICLES / 2)
#define CULL_PAD 2.0f

static T3DModel *models[MDL_COUNT];
static rspq_block_t *blocks[MDL_COUNT];
// Per bug type: the material, when `blocks` holds geometry only. NULL for a
// model with several materials, whose block then carries them all.
static rspq_block_t *bug_material[ENEMY_TYPE_COUNT];
// The soldier's material with the player-colour combiner, when `blocks`
// holds its geometry only (a single-material model); NULL otherwise, and
// the soldier is drawn untinted.
static rspq_block_t *soldier_material;

// Vertex alpha 0 marks the parts that take the player's colour (prim):
// cycle 1 picks white or prim by shade alpha, cycle 2 lights it.
#define TINT_COMBINER RDPQ_COMBINER2((ENV, PRIM, SHADE_ALPHA, PRIM), (0, 0, 0, 1), \
                                     (COMBINED, 0, SHADE, 0), (0, 0, 0, 1))
static T3DViewport viewport;
static int frame_idx;

// Uncached: the RSP DMAs these.
static T3DMat4FP *static_mats;    // [0] = floor, [1..] = rocks
static T3DMat4FP *dynamic_mats;   // [FB_COUNT][MAX_DYNAMIC]
static TPXParticleS16 *particles; // [FB_COUNT][PARTICLE_PAIRS]
static T3DMat4FP *particle_mat;   // identity: particles are already in world units
static uint8_t dyn_pose[MAX_DYNAMIC]; // pose slot per dynamic matrix (bugs only)
static uint8_t dyn_type[MAX_DYNAMIC]; // enemy type per dynamic matrix (bugs only)
static uint8_t prop_model[MAX_PROPS]; // model per prop matrix

// Model matrix for a unit standing at `pos` facing `facing` (sim space),
// lifted by `lift` metres and pitched forward by `pitch` radians around its
// own right axis. Built directly so the pitch follows the unit's facing.
static void unit_matrix(T3DMat4FP *out, vec2 pos, float lift, vec2 facing, float pitch, float scale)
{
    // Sim north (+y) is render -z.
    float fx = facing.x, fz = -facing.y;
    float c = fm_cosf(pitch), s = fm_sinf(pitch);
    float k = scale * MODEL_SCALE;
    T3DMat4 m = {{
        { fz * k, 0.0f, -fx * k, 0.0f },              // right
        { fx * s * k, c * k, fz * s * k, 0.0f },      // up
        { fx * c * k, -s * k, fz * c * k, 0.0f },     // forward
        { pos.x * WORLD_SCALE, lift * WORLD_SCALE, -pos.y * WORLD_SCALE, 1.0f },
    }};
    t3d_mat4_to_fixed(out, &m);
}

void render_init(void)
{
    tpx_init((TPXInitParams){});
    for (int i = 0; i < MDL_COUNT; i++) {
        char path[64];
        snprintf(path, sizeof(path), "rom:/models/%s.t3dm", model_names[i]);
        models[i] = t3d_model_load(path);
        bool skinned = i <= MDL_BULWARK;
        int objects = 0;
        T3DModelIter it = t3d_model_iter_create(models[i], T3D_CHUNK_TYPE_OBJECT);
        while (t3d_model_iter_next(&it))
            objects++;
        if (skinned && objects == 1) {
            T3DModelState state = t3d_model_state_create();
            rspq_block_begin();
            it = t3d_model_iter_create(models[i], T3D_CHUNK_TYPE_OBJECT);
            while (t3d_model_iter_next(&it)) {
                T3DMaterial *mat = it.object->material;
                if (!mat)
                    continue;
                if (i == MDL_SOLDIER)
                    mat->colorCombiner = TINT_COMBINER;
                t3d_model_draw_material(mat, &state);
            }
            if (i == MDL_SOLDIER) {
                rdpq_set_env_color(RGBA32(0xFF, 0xFF, 0xFF, 0xFF));
                soldier_material = rspq_block_end();
            } else {
                bug_material[i - MDL_SKITTER] = rspq_block_end();
            }
            // Buffered skeletons draw through the skeleton segment, so one
            // recorded block serves every pose slot.
            rspq_block_begin();
            it = t3d_model_iter_create(models[i], T3D_CHUNK_TYPE_OBJECT);
            while (t3d_model_iter_next(&it))
                t3d_model_draw_object(it.object, (const T3DMat4FP *)t3d_segment_placeholder(T3D_SEGMENT_SKELETON));
            blocks[i] = rspq_block_end();
        } else if (skinned) {
            T3DSkeleton tmp = t3d_skeleton_create_buffered(models[i], FB_COUNT);
            rspq_block_begin();
            t3d_model_draw_skinned(models[i], &tmp);
            blocks[i] = rspq_block_end();
            t3d_skeleton_destroy(&tmp);
        } else {
            rspq_block_begin();
            t3d_model_draw(models[i]);
            blocks[i] = rspq_block_end();
        }
    }

    for (int t = 0; t < ENEMY_TYPE_COUNT; t++) {
        const T3DModel *m = models[MDL_SKITTER + t];
        for (int k = 0; k < POSE_SLOTS; k++) {
            pose_slot_t *ps = &bug_pose[t][k];
            ps->skel = t3d_skeleton_create_buffered(m, FB_COUNT);
            bool walk = k < WALK_SLOTS;
            ps->anim = t3d_anim_create(m, walk ? "walk" : "attack");
            t3d_anim_attach(&ps->anim, &ps->skel);
            t3d_anim_set_looping(&ps->anim, true);
            if (walk) // spread the walk cycles so a crowd doesn't march in step
                t3d_anim_set_time(&ps->anim, ps->anim.animRef->duration * (float)k / WALK_SLOTS);
        }
    }
    for (int i = 0; i < SIM_MAX_PLAYERS; i++) {
        soldier_pose[i].skel = t3d_skeleton_create_buffered(models[MDL_SOLDIER], FB_COUNT);
        soldier_pose[i].run = t3d_anim_create(models[MDL_SOLDIER], "run");
        soldier_pose[i].idle = t3d_anim_create(models[MDL_SOLDIER], "idle");
        t3d_anim_attach(&soldier_pose[i].run, &soldier_pose[i].skel);
        t3d_anim_attach(&soldier_pose[i].idle, &soldier_pose[i].skel);
    }

    // Everything the frame loop touches is allocated here, once.
    static_mats = malloc_uncached(sizeof(T3DMat4FP) * (1 + SIM_MAX_OBSTACLES));
    dynamic_mats = malloc_uncached(sizeof(T3DMat4FP) * MAX_DYNAMIC * FB_COUNT);
    particles = malloc_uncached(sizeof(TPXParticleS16) * PARTICLE_PAIRS * FB_COUNT);
    particle_mat = malloc_uncached(sizeof(T3DMat4FP));
    t3d_mat4fp_identity(particle_mat);
    viewport = t3d_viewport_create_buffered(FB_COUNT);
    debugf("render: %u dynamic matrices, %u particles per frame\n",
           (unsigned)MAX_DYNAMIC, (unsigned)MAX_PARTICLES);
}

void render_bind_world(const sim_t *sim)
{
    unit_matrix(&static_mats[0], v2(0.0f, 0.0f), 0.0f, v2(0.0f, 1.0f), 0.0f, 1.0f);
    for (int i = 0; i < sim->arena.obstacle_count; i++) {
        const obstacle_t *o = &sim->arena.obstacles[i];
        vec2 f = v2(fm_sinf(o->yaw), fm_cosf(o->yaw));
        unit_matrix(&static_mats[1 + i], o->pos, 0.0f, f, 0.0f, 1.0f);
    }
}

static bool in_rect(vec2 p, vec2 mn, vec2 mx)
{
    return p.x >= mn.x && p.x <= mx.x && p.y >= mn.y && p.y <= mx.y;
}

// --- Particles ---------------------------------------------------------------

typedef struct {
    TPXParticleS16 *buf;
    int count;
} pbuild_t;

static void pb_add(pbuild_t *pb, float x, float y, float z, int size, uint8_t r, uint8_t g, uint8_t b)
{
    if (pb->count >= PARTICLE_PAIRS * 2 || size <= 0)
        return;
    int16_t *pos = tpx_buffer_s16_get_pos(pb->buf, pb->count);
    pos[0] = (int16_t)(x * WORLD_SCALE);
    pos[1] = (int16_t)(y * WORLD_SCALE);
    pos[2] = (int16_t)(z * WORLD_SCALE);
    *tpx_buffer_s16_get_size(pb->buf, pb->count) = (int8_t)(size > 127 ? 127 : size);
    uint8_t *col = tpx_buffer_s16_get_rgba(pb->buf, pb->count);
    col[0] = r; col[1] = g; col[2] = b; col[3] = 0xFF;
    pb->count++;
}

// tpx draws pairs: pad an odd count with an invisible particle.
static void pb_pad(pbuild_t *pb)
{
    if ((pb->count & 1) && pb->count < PARTICLE_PAIRS * 2) {
        *tpx_buffer_s16_get_size(pb->buf, pb->count) = 0;
        pb->count++;
    }
}

static void pb_ring(pbuild_t *pb, vec2 c, float radius, uint8_t r, uint8_t g, uint8_t b)
{
    for (int k = 0; k < TELEGRAPH_DOTS; k++) {
        float a = 6.2832f * (float)k / TELEGRAPH_DOTS;
        pb_add(pb, c.x + fm_cosf(a) * radius, 0.1f, -(c.y + fm_sinf(a) * radius), 14, r, g, b);
    }
}

// Calldown markers: a blinking light column over each beacon (faster near
// arrival) and where the strikes will land.
static void build_calldowns(const sim_t *sim, pbuild_t *pb)
{
    for (int i = 0; i < SIM_MAX_BEACONS; i++) {
        const beacon_t *b = &sim->beacons[i];
        if (!b->active)
            continue;
        const uint8_t *c = beacon_rgb[b->type];
        int period = b->timer > SECONDS(2.0f) ? 8 : 3;
        bool on = b->state == BEACON_FLYING || ((b->timer / period) & 1) == 0;
        if (on && b->state == BEACON_ARMED)
            for (int k = 0; k < 4; k++)
                pb_add(pb, b->pos.x, 0.6f + 0.8f * k, -b->pos.y, 28 - 4 * k, c[0], c[1], c[2]);
        if (b->state != BEACON_ARMED)
            continue;
        if (b->type == CALLDOWN_BARRAGE) {
            pb_ring(pb, b->pos, BARRAGE_AREA, c[0], c[1], c[2]);
        } else if (b->type == CALLDOWN_AIRSTRIKE) {
            for (int k = 0; k < AIRSTRIKE_BOMBS; k++) {
                float along = ((float)k - (AIRSTRIKE_BOMBS - 1) * 0.5f) * AIRSTRIKE_SPACING;
                vec2 at = v2_add(b->pos, v2_scale(b->dir, along));
                pb_add(pb, at.x, 0.1f, -at.y, 24, c[0], c[1], c[2]);
            }
        }
    }
    for (int i = 0; i < SIM_MAX_STRIKES; i++) {
        const strike_t *k = &sim->strikes[i];
        if (k->active && k->type == CALLDOWN_BARRAGE)
            pb_ring(pb, k->center, BARRAGE_AREA, 255, 70, 40);
    }
    for (int i = 0; i < SIM_MAX_PICKUPS; i++) {
        const pickup_t *k = &sim->pickups[i];
        if (k->active && ((sim->tick / 8) & 1))
            pb_add(pb, k->pos.x, 1.4f, -k->pos.y, 24, 255, 220, 120);
    }
}

// Site colours, matching the HUD's text styles for the zone names.
static const uint8_t site_rgb[MISSION_ZONES + 1][3] = {
    { 232, 150, 40 },  // zone A (STYLE_TITLE)
    { 80, 220, 255 },  // zone B (STYLE_ZONE_B)
    { 240, 240, 255 }, // LZ
};

// A light column over a site, and when the site is off screen a chevron at
// the edge of the visible ground pointing at it. Particles, not text: every
// rdpq_text call costs the RSP far more than a few particles.
static void site_marker(const sim_t *sim, pbuild_t *pb, vec2 pos, const uint8_t *c)
{
    for (int k = 0; k < 5; k++)
        pb_add(pb, pos.x, 0.8f + 0.8f * k, -pos.y, 30 - 4 * k, c[0], c[1], c[2]);

    // The visible ground is a trapezoid: use its narrow bottom width, and
    // keep clear of the HUD rows at the top and bottom of the screen.
    const camera_t *cam = &sim->camera;
    cam_extent_t e = camera_extent();
    vec2 mn = v2(cam->target.x - cam->dist * e.half_w + 1.5f, cam->target.y - cam->dist * e.below + 3.0f);
    vec2 mx = v2(cam->target.x + cam->dist * e.half_w - 1.5f, cam->target.y + cam->dist * e.above - 4.0f);
    if (in_rect(pos, mn, mx))
        return;
    vec2 at = v2(clampf(pos.x, mn.x, mx.x), clampf(pos.y, mn.y, mx.y));
    vec2 dir = v2_norm_or(v2_sub(pos, at), v2(0.0f, 1.0f)), side = v2(-dir.y, dir.x);
    vec2 pts[4] = {
        v2_add(at, v2_scale(dir, 0.7f)),
        v2_add(at, v2_scale(side, 0.6f)),
        v2_sub(at, v2_scale(side, 0.6f)),
        v2_sub(at, v2_scale(dir, 0.4f)),
    };
    for (int k = 0; k < 4; k++)
        pb_add(pb, pts[k].x, 0.5f, -pts[k].y, k == 3 ? 16 : 26, c[0], c[1], c[2]);
}

// Objective zones as rings that light up with their progress (green when
// captured, red while bugs hold them), and the LZ, blinking while it waits
// for the extraction call. Sites still to visit get markers.
static void build_mission(const sim_t *sim, pbuild_t *pb)
{
    const mission_t *m = &sim->mission;
    bool blink = (sim->tick / 8) & 1;
    if (m->phase <= MISSION_OBJECTIVE) {
        for (int z = 0; z < MISSION_ZONES; z++) {
            const zone_state_t *zs = &m->zones[z];
            vec2 c = sim->arena.zones[z];
            int lit = zs->held * TELEGRAPH_DOTS / mission_capture_ticks(sim);
            for (int k = 0; k < TELEGRAPH_DOTS; k++) {
                float a = 6.2832f * (float)k / TELEGRAPH_DOTS;
                uint8_t r = 150, g = 150, b = 150;
                if (zs->captured)
                    r = 90, g = 240, b = 110;
                else if (zs->players_in && zs->bugs_in)
                    r = blink ? 255 : 150, g = 60, b = 50;
                else if (k < lit)
                    r = 255, g = 190, b = 60;
                pb_add(pb, c.x + fm_cosf(a) * ZONE_RADIUS, 0.1f, -(c.y + fm_sinf(a) * ZONE_RADIUS), 16, r, g, b);
            }
            if (m->phase == MISSION_OBJECTIVE && !zs->captured)
                site_marker(sim, pb, c, site_rgb[z]);
        }
    }
    const uint8_t *lz = site_rgb[MISSION_ZONES];
    if (m->phase >= MISSION_EXTRACT && !(m->phase == MISSION_EXTRACT && blink))
        pb_ring(pb, sim->arena.lz, LZ_RADIUS, lz[0], lz[1], lz[2]);
    if (m->phase >= MISSION_EXTRACT && !mission_over(sim))
        site_marker(sim, pb, sim->arena.lz, lz);
}

// Opaque particles from index 0, then (from *glow_start, an even index) the
// semi-transparent ones. Returns the opaque count.
static int build_particles(const sim_t *sim, TPXParticleS16 *buf, vec2 vmin, vec2 vmax,
                           int *glow_start, int *glow_count)
{
    pbuild_t pb = { .buf = buf, .count = 0 };

    for (int i = 0; i < SIM_MAX_PROJECTILES; i++) {
        const projectile_t *p = &sim->projectiles[i];
        if (!p->active || !in_rect(p->pos, vmin, vmax))
            continue;
        bool shotgun = p->weapon == WEAPON_SHOTGUN;
        vec2 tail = v2_sub(p->pos, v2_scale(p->vel, SIM_DT * 0.5f));
        pb_add(&pb, p->pos.x, 1.0f, -p->pos.y, shotgun ? 16 : 20, 255, 240, 150);
        pb_add(&pb, tail.x, 1.0f, -tail.y, shotgun ? 10 : 14, 255, 170, 60);
    }

    // Aim marker: three dots ahead of each living player, in their colour
    // (grey while reloading).
    for (int i = 0; i < SIM_MAX_PLAYERS; i++) {
        const player_t *p = &sim->players[i];
        if (!sim_player_alive(p) || p->state != PSTATE_NORMAL)
            continue;
        const uint8_t *c = PLAYER_RGB[i];
        static const uint8_t grey[3] = { 120, 120, 120 };
        if (p->reload_timer)
            c = grey;
        for (int d = 0; d < 3; d++) {
            vec2 at = v2_add(p->pos, v2_scale(p->facing, 1.6f + 0.8f * d));
            pb_add(&pb, at.x, 0.15f, -at.y, 12, c[0], c[1], c[2]);
        }
    }

    build_calldowns(sim, &pb);
    build_mission(sim, &pb);

    const fx_particle_t *fx = fx_particles();
    for (int pass = 0; pass < 2; pass++) {
        if (pass == 1) {
            pb_pad(&pb);
            *glow_start = pb.count;
        }
        for (int i = 0; i < FX_MAX; i++) {
            const fx_particle_t *f = &fx[i];
            if (!f->life || f->glow != (pass == 1))
                continue;
            int size = f->size * f->life / f->life_max + 4;
            pb_add(&pb, f->x, f->y, f->z, size, f->rgb[0], f->rgb[1], f->rgb[2]);
        }
    }
    pb_pad(&pb);
    *glow_count = pb.count - *glow_start;
    return *glow_start;
}

// --- Props ---------------------------------------------------------------------

// Beacons (arcing while thrown), what falls on them in the last moments of
// the call-in, turrets and pickups. Appends matrices after `n`; returns the
// new count.
static int build_props(const sim_t *sim, T3DMat4FP *dyn, int n, vec2 vmin, vec2 vmax)
{
    int first = n;
    for (int i = 0; i < SIM_MAX_BEACONS; i++) {
        const beacon_t *b = &sim->beacons[i];
        if (!b->active || !in_rect(b->pos, vmin, vmax))
            continue;
        float lift = 0.0f;
        if (b->state == BEACON_FLYING) {
            float f = 1.0f - (float)b->timer / BEACON_FLIGHT_TICKS;
            lift = 0.9f * (1.0f - f) + 6.0f * f * (1.0f - f);
        }
        prop_model[n - first] = MDL_BEACON;
        unit_matrix(&dyn[n++], b->pos, lift, b->dir, 0.0f, 1.0f);
        if (b->state == BEACON_ARMED && drop_model[b->type] >= 0 && b->timer <= DROP_TICKS) {
            float k = (float)b->timer / DROP_TICKS;
            prop_model[n - first] = (uint8_t)drop_model[b->type];
            unit_matrix(&dyn[n++], b->pos, 25.0f * k * k, b->dir, 0.0f, 1.0f);
        }
    }
    for (int i = 0; i < SIM_MAX_TURRETS; i++) {
        const turret_t *t = &sim->turrets[i];
        if (!t->active || !in_rect(t->pos, vmin, vmax))
            continue;
        prop_model[n - first] = MDL_TURRET;
        unit_matrix(&dyn[n++], t->pos, 0.0f, t->facing, 0.0f, 1.0f);
    }
    for (int i = 0; i < SIM_MAX_PICKUPS; i++) {
        const pickup_t *k = &sim->pickups[i];
        if (!k->active || !in_rect(k->pos, vmin, vmax))
            continue;
        prop_model[n - first] = k->type == PICKUP_RESUPPLY ? MDL_CRATE : MDL_MGCASE;
        unit_matrix(&dyn[n++], k->pos, 0.0f, v2(0.0f, 1.0f), 0.0f, 1.0f);
    }
    // The squad's pods coming down at the start of the mission.
    for (int i = 0; i < SIM_MAX_PLAYERS; i++) {
        const player_t *p = &sim->players[i];
        if (!p->active || p->state != PSTATE_DROPPING)
            continue;
        float k = (float)p->state_timer / DROP_IN_TICKS;
        prop_model[n - first] = MDL_POD;
        unit_matrix(&dyn[n++], p->pos, 40.0f * k * k, v2(0.0f, 1.0f), 0.0f, 1.0f);
    }
    // The dropship: descending at the end of the hold, then down on the LZ.
    const mission_t *m = &sim->mission;
    bool landing = m->phase == MISSION_HOLD && m->timer <= DROPSHIP_DESCENT;
    if (landing || m->phase == MISSION_BOARDING || m->phase == MISSION_SUCCESS) {
        float k = landing ? (float)m->timer / DROPSHIP_DESCENT : 0.0f;
        prop_model[n - first] = MDL_DROPSHIP;
        unit_matrix(&dyn[n++], sim->arena.lz, 30.0f * k * k, v2(0.0f, 1.0f), 0.0f, 1.0f);
    }
    return n;
}

// --- Frame ---------------------------------------------------------------------

// Advance the pose slots some bug on screen uses, and each soldier. An unused
// slot just pauses: its skeleton keeps its last matrices.
static void update_poses(const sim_t *sim, const bool used[ENEMY_TYPE_COUNT][POSE_SLOTS], float dt)
{
    int updated = 0;
    for (int t = 0; t < ENEMY_TYPE_COUNT; t++) {
        for (int k = 0; k < POSE_SLOTS; k++) {
            if (!used[t][k])
                continue;
            t3d_anim_update(&bug_pose[t][k].anim, dt);
            t3d_skeleton_update(&bug_pose[t][k].skel);
            updated++;
        }
    }
    for (int i = 0; i < SIM_MAX_PLAYERS; i++) {
        const player_t *p = &sim->players[i];
        if (!sim_player_alive(p))
            continue;
        if (p->state != PSTATE_NORMAL) // diving / prone: hold the pose
            continue;
        bool moving = v2_len2(p->vel) > 0.25f;
        if (moving != soldier_pose[i].running) {
            soldier_pose[i].running = moving;
            t3d_skeleton_reset(&soldier_pose[i].skel); // idle leaves the legs alone
        }
        t3d_anim_update(moving ? &soldier_pose[i].run : &soldier_pose[i].idle, dt);
        t3d_skeleton_update(&soldier_pose[i].skel);
        updated++;
    }
    stats.skeletons = (uint16_t)updated;
}

uint32_t render_scene(const sim_t *sim, float anim_dt)
{
    uint64_t t0 = get_ticks();
    frame_idx = (frame_idx + 1) % FB_COUNT;
    T3DMat4FP *dyn = &dynamic_mats[frame_idx * MAX_DYNAMIC];
    TPXParticleS16 *pbuf = &particles[frame_idx * PARTICLE_PAIRS];

    // Camera from the sim: looking north, pitched down, `dist` from target,
    // plus any explosion shake (visual only, the sim never sees it).
    const camera_t *cam = &sim->camera;
    float h = fm_sinf(CAM_PITCH_RAD) * cam->dist, back = fm_cosf(CAM_PITCH_RAD) * cam->dist;
    vec2 look = v2_add(cam->target, fx_shake());
    T3DVec3 target = {{ look.x * WORLD_SCALE, 0.0f, -look.y * WORLD_SCALE }};
    T3DVec3 eye = {{ target.v[0], h * WORLD_SCALE, target.v[2] + back * WORLD_SCALE }};
    t3d_viewport_set_projection(&viewport, CAM_FOV_Y_RAD, 2.0f * WORLD_SCALE, 70.0f * WORLD_SCALE);
    t3d_viewport_look_at(&viewport, &eye, &target, &(T3DVec3){{ 0.0f, 1.0f, 0.0f }});

    vec2 vmin, vmax;
    camera_view_rect(cam, CULL_PAD, &vmin, &vmax);

    // Unit matrices (CPU) before waiting for a framebuffer.
    int ndyn = 0;
    int player_idx[SIM_MAX_PLAYERS];
    for (int i = 0; i < SIM_MAX_PLAYERS; i++) {
        const player_t *p = &sim->players[i];
        if (!sim_player_alive(p) || p->state == PSTATE_DROPPING) // pods are props
            continue;
        float pitch = 0.0f, lift = 0.0f;
        if (p->state == PSTATE_DIVING) {
            pitch = 1.2f;
            lift = 0.25f;
        } else if (p->state == PSTATE_PRONE) {
            pitch = 1.45f;
            lift = 0.1f;
        }
        player_idx[ndyn] = i;
        unit_matrix(&dyn[ndyn++], p->pos, lift, p->facing, pitch, 1.0f);
    }
    int first_bug = ndyn;
    bool used[ENEMY_TYPE_COUNT][POSE_SLOTS] = { { false } };
    for (int n = 0; n < sim->active_count; n++) {
        int i = sim->active[n];
        const enemy_t *e = &sim->enemies[i];
        if (!e->active || !in_rect(e->pos, vmin, vmax))
            continue;
        uint8_t slot = e->state == ESTATE_CHASE ? (uint8_t)(i % WALK_SLOTS) : WALK_SLOTS;
        used[e->type][slot] = true;
        dyn_type[ndyn] = e->type;
        dyn_pose[ndyn] = slot;
        // The windup still rears up a little on top of the attack pose, so
        // the telegraph reads even though attack poses are shared.
        float pitch = e->state == ESTATE_WINDUP ? -0.15f : 0.0f;
        unit_matrix(&dyn[ndyn++], e->pos, 0.0f, e->facing, pitch, 1.0f);
    }
    stats.drawn_bugs = (uint16_t)(ndyn - first_bug);
    int first_prop = ndyn;
    ndyn = build_props(sim, dyn, ndyn, vmin, vmax);
    int glow_start = 0, nglow = 0;
    int npart = build_particles(sim, pbuf, vmin, vmax, &glow_start, &nglow);
    uint64_t t1 = get_ticks();
    update_poses(sim, used, anim_dt);
    uint64_t t2 = get_ticks();
    stats.build_us = (uint32_t)TICKS_TO_US(t1 - t0);
    stats.anim_us = (uint32_t)TICKS_TO_US(t2 - t1);

    uint64_t wait_start = get_ticks();
    surface_t *fb = display_get();
    uint32_t wait_us = (uint32_t)TICKS_TO_US(get_ticks() - wait_start);

    rdpq_attach(fb, display_get_zbuf());
    gpu_timing_submit_begin();
    t3d_frame_start();
    t3d_viewport_attach(&viewport);
    t3d_screen_clear_color(RGBA32(24, 22, 26, 0xFF));
    t3d_screen_clear_depth();

    uint8_t ambient[4] = { 90, 88, 100, 0xFF };
    uint8_t sun[4] = { 0xFF, 0xF0, 0xDC, 0xFF };
    T3DVec3 sun_dir = {{ -0.5f, 1.0f, 0.4f }};
    t3d_vec3_norm(&sun_dir);
    t3d_light_set_ambient(ambient);
    t3d_light_set_directional(0, sun, &sun_dir);
    t3d_light_set_count(1);

    t3d_matrix_push_pos(1);
    t3d_matrix_set(&static_mats[0], true);
    rspq_block_run(blocks[MDL_GROUND]);
    for (int i = 0; i < sim->arena.obstacle_count; i++) {
        const obstacle_t *o = &sim->arena.obstacles[i];
        if (!in_rect(o->pos, vmin, vmax))
            continue;
        t3d_matrix_set(&static_mats[1 + i], true);
        rspq_block_run(blocks[MDL_ROCK_A + o->kind]);
    }
    if (first_bug && soldier_material)
        rspq_block_run(soldier_material);
    for (int i = 0; i < first_bug; i++) {
        const uint8_t *c = PLAYER_RGB[player_idx[i]];
        rdpq_set_prim_color(RGBA32(c[0], c[1], c[2], 0xFF));
        t3d_skeleton_use(&soldier_pose[player_idx[i]].skel);
        t3d_matrix_set(&dyn[i], true);
        rspq_block_run(blocks[MDL_SOLDIER]);
    }
    for (int t = 0; t < ENEMY_TYPE_COUNT; t++) {
        bool material_set = false;
        for (int i = first_bug; i < ndyn; i++) {
            if (dyn_type[i] != t)
                continue;
            if (!material_set && bug_material[t])
                rspq_block_run(bug_material[t]);
            material_set = true;
            t3d_skeleton_use(&bug_pose[t][dyn_pose[i]].skel);
            t3d_matrix_set(&dyn[i], true);
            rspq_block_run(blocks[MDL_SKITTER + t]);
        }
    }
    for (int i = first_prop; i < ndyn; i++) {
        t3d_matrix_set(&dyn[i], true);
        rspq_block_run(blocks[prop_model[i - first_prop]]);
    }
    t3d_matrix_pop(1);

    if (npart) {
        // tpx emits screen-space rectangles colored by prim; this is the
        // setup from Tiny3D's 18_particles example.
        rdpq_set_mode_standard();
        rdpq_mode_zbuf(true, true);
        rdpq_mode_zoverride(true, 0, 0);
        rdpq_mode_combiner(RDPQ_COMBINER1((PRIM, 0, ENV, 0), (0, 0, 0, 1)));
        rdpq_set_env_color(RGBA32(0xFF, 0xFF, 0xFF, 0xFF));
        tpx_state_from_t3d();
        tpx_matrix_push(particle_mat);
        tpx_state_set_base_size(64);
        tpx_state_set_scale(1.0f, 1.0f);
        tpx_particle_draw_s16(pbuf, (uint32_t)npart);
        tpx_matrix_pop(1);
    }
    if (nglow) {
        // Explosion cores: semi-transparent with a constant alpha. (The RDP's
        // additive blend wraps to black on overflow, see rdpq_mode.h.) They
        // test depth but don't write it, so they never hide each other.
        rdpq_set_mode_standard();
        rdpq_mode_zbuf(true, false);
        rdpq_mode_zoverride(true, 0, 0);
        rdpq_mode_combiner(RDPQ_COMBINER1((PRIM, 0, ENV, 0), (0, 0, 0, 1)));
        rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY_CONST);
        rdpq_set_fog_color(RGBA32(0, 0, 0, 150));
        rdpq_set_env_color(RGBA32(0xFF, 0xFF, 0xFF, 0xFF));
        tpx_state_from_t3d();
        tpx_matrix_push(particle_mat);
        tpx_state_set_base_size(64);
        tpx_state_set_scale(1.0f, 1.0f);
        tpx_particle_draw_s16(&pbuf[glow_start / 2], (uint32_t)nglow);
        tpx_matrix_pop(1);
    }
    float flash = fx_flash();
    if (flash > 0.02f) {
        // Explosion flash: one blended full-screen rectangle, a frame or two.
        rdpq_set_mode_standard();
        rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
        rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
        rdpq_set_prim_color(RGBA32(255, 230, 190, (uint8_t)(flash * 160.0f)));
        rdpq_fill_rectangle(0, 0, 320, 240);
    }
    return wait_us;
}

const render_stats_t *render_stats(void)
{
    return &stats;
}

bool render_project(vec2 pos, float height, int *x, int *y)
{
    T3DVec3 world = {{ pos.x * WORLD_SCALE, height * WORLD_SCALE, -pos.y * WORLD_SCALE }}, screen;
    t3d_viewport_calc_viewspace_pos(&viewport, &screen, &world);
    if (screen.v[2] >= 1.0f || screen.v[0] < 0.0f || screen.v[0] >= 320.0f ||
        screen.v[1] < 0.0f || screen.v[1] >= 240.0f)
        return false;
    *x = (int)screen.v[0];
    *y = (int)screen.v[1];
    return true;
}

void render_end(void)
{
    gpu_timing_detach_show();
}
