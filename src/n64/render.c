#include "render.h"
#include "camera.h"
#include "fx.h"
#include "gpu_timing.h"

#include <libdragon.h>
#include <stdio.h>
#include <t3d/t3d.h>
#include <t3d/t3danim.h>
#include <t3d/t3dmodel.h>
#include <t3d/t3dskeleton.h>
#include <t3d/tpx.h>

// Draws the sim state: floor, rocks, players, bugs as Tiny3D models, and
// everything small (bullets, effects, aim markers) as tpx particles in a
// single RSP command.
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
    MDL_ROCK_A, MDL_ROCK_B, MDL_ROCK_C, MDL_GROUND, MDL_COUNT
};

static const char *const model_names[MDL_COUNT] = {
    "soldier", "skitter", "gnasher", "bulwark", "rock_a", "rock_b", "rock_c", "ground",
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
#define MAX_DYNAMIC (SIM_MAX_PLAYERS + SIM_MAX_ENEMIES)
#define MAX_PARTICLES (SIM_MAX_PROJECTILES * 2 + FX_MAX + SIM_MAX_PLAYERS * 3 + 2)
#define PARTICLE_PAIRS (MAX_PARTICLES / 2)
#define CULL_PAD 2.0f

static T3DModel *models[MDL_COUNT];
static rspq_block_t *blocks[MDL_COUNT];
// Per bug type: the material, when `blocks` holds geometry only. NULL for a
// model with several materials, whose block then carries them all.
static rspq_block_t *bug_material[ENEMY_TYPE_COUNT];
static T3DViewport viewport;
static int frame_idx;

// Uncached: the RSP DMAs these.
static T3DMat4FP *static_mats;    // [0] = floor, [1..] = rocks
static T3DMat4FP *dynamic_mats;   // [FB_COUNT][MAX_DYNAMIC]
static TPXParticleS16 *particles; // [FB_COUNT][PARTICLE_PAIRS]
static T3DMat4FP *particle_mat;   // identity: particles are already in world units
static uint8_t dyn_pose[MAX_DYNAMIC]; // pose slot per dynamic matrix (bugs only)
static uint8_t dyn_type[MAX_DYNAMIC]; // enemy type per dynamic matrix (bugs only)

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
        bool bug = i >= MDL_SKITTER && skinned;
        int objects = 0;
        T3DModelIter it = t3d_model_iter_create(models[i], T3D_CHUNK_TYPE_OBJECT);
        while (t3d_model_iter_next(&it))
            objects++;
        if (bug && objects == 1) {
            T3DModelState state = t3d_model_state_create();
            rspq_block_begin();
            it = t3d_model_iter_create(models[i], T3D_CHUNK_TYPE_OBJECT);
            while (t3d_model_iter_next(&it))
                if (it.object->material)
                    t3d_model_draw_material(it.object->material, &state);
            bug_material[i - MDL_SKITTER] = rspq_block_end();
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

static int build_particles(const sim_t *sim, TPXParticleS16 *buf, vec2 vmin, vec2 vmax)
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

    // Aim marker: three dots ahead of each living player (grey while reloading).
    for (int i = 0; i < SIM_MAX_PLAYERS; i++) {
        const player_t *p = &sim->players[i];
        if (!sim_player_alive(p) || p->state != PSTATE_NORMAL)
            continue;
        uint8_t v = p->reload_timer ? 120 : 255;
        for (int d = 0; d < 3; d++) {
            vec2 at = v2_add(p->pos, v2_scale(p->facing, 1.6f + 0.8f * d));
            pb_add(&pb, at.x, 0.15f, -at.y, 12, v, v, v);
        }
    }

    const fx_particle_t *fx = fx_particles();
    for (int i = 0; i < FX_MAX; i++) {
        const fx_particle_t *f = &fx[i];
        if (!f->life)
            continue;
        int size = f->size * f->life / f->life_max + 4;
        pb_add(&pb, f->x, f->y, f->z, size, f->rgb[0], f->rgb[1], f->rgb[2]);
    }

    // tpx draws pairs: pad an odd count with an invisible particle.
    if (pb.count & 1) {
        *tpx_buffer_s16_get_size(buf, pb.count) = 0;
        pb.count++;
    }
    return pb.count;
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

    // Camera from the sim: looking north, pitched down, `dist` from target.
    const camera_t *cam = &sim->camera;
    float h = fm_sinf(CAM_PITCH_RAD) * cam->dist, back = fm_cosf(CAM_PITCH_RAD) * cam->dist;
    T3DVec3 target = {{ cam->target.x * WORLD_SCALE, 0.0f, -cam->target.y * WORLD_SCALE }};
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
        if (!sim_player_alive(p))
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
    int npart = build_particles(sim, pbuf, vmin, vmax);
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
    for (int i = 0; i < first_bug; i++) {
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
    return wait_us;
}

const render_stats_t *render_stats(void)
{
    return &stats;
}

void render_end(void)
{
    gpu_timing_detach_show();
}
