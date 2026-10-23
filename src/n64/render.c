#include "render.h"
#include "camera.h"
#include "fx.h"
#include "gpu_timing.h"

#include <libdragon.h>
#include <stdio.h>
#include <t3d/t3d.h>
#include <t3d/t3dmodel.h>
#include <t3d/tpx.h>

// Draws the sim state: floor, rocks, players, bugs as Tiny3D models, and
// everything small (bullets, effects, aim markers) as tpx particles in a
// single RSP command.

enum {
    MDL_SOLDIER, MDL_GNASHER, MDL_ROCK_A, MDL_ROCK_B, MDL_ROCK_C, MDL_GROUND, MDL_COUNT
};

static const char *const model_names[MDL_COUNT] = {
    "soldier", "gnasher", "rock_a", "rock_b", "rock_c", "ground",
};

#define MODEL_SCALE (WORLD_SCALE / 64.0f)
#define MAX_DYNAMIC (SIM_MAX_PLAYERS + SIM_MAX_ENEMIES)
#define MAX_PARTICLES (SIM_MAX_PROJECTILES * 2 + FX_MAX + SIM_MAX_PLAYERS * 3 + 2)
#define PARTICLE_PAIRS (MAX_PARTICLES / 2)
#define CULL_PAD 2.0f

static T3DModel *models[MDL_COUNT];
static rspq_block_t *blocks[MDL_COUNT];
static T3DViewport viewport;
static int frame_idx;

// Uncached: the RSP DMAs these.
static T3DMat4FP *static_mats;    // [0] = floor, [1..] = rocks
static T3DMat4FP *dynamic_mats;   // [FB_COUNT][MAX_DYNAMIC]
static TPXParticleS16 *particles; // [FB_COUNT][PARTICLE_PAIRS]
static T3DMat4FP *particle_mat;   // identity: particles are already in world units

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
        rspq_block_begin();
        t3d_model_draw(models[i]);
        blocks[i] = rspq_block_end();
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

uint32_t render_scene(const sim_t *sim)
{
    frame_idx = (frame_idx + 1) % FB_COUNT;
    T3DMat4FP *dyn = &dynamic_mats[frame_idx * MAX_DYNAMIC];
    TPXParticleS16 *pbuf = &particles[frame_idx * PARTICLE_PAIRS];
    float t = (float)sim->tick * SIM_DT;

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
        unit_matrix(&dyn[ndyn++], p->pos, lift, p->facing, pitch, 1.0f);
    }
    int first_enemy = ndyn;
    for (int i = 0; i < SIM_MAX_ENEMIES; i++) {
        const enemy_t *e = &sim->enemies[i];
        if (!e->active || !in_rect(e->pos, vmin, vmax))
            continue;
        float pitch = 0.0f, lift = 0.0f;
        if (e->state == ESTATE_WINDUP)
            pitch = -0.35f; // rear up before the strike
        else if (e->state == ESTATE_RECOVER)
            pitch = 0.25f;  // follow-through
        else
            lift = 0.05f * fabsf(fm_sinf(t * 14.0f + (float)i)); // scuttle
        unit_matrix(&dyn[ndyn++], e->pos, lift, e->facing, pitch, 1.0f);
    }
    int npart = build_particles(sim, pbuf, vmin, vmax);

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
    for (int i = 0; i < first_enemy; i++) {
        t3d_matrix_set(&dyn[i], true);
        rspq_block_run(blocks[MDL_SOLDIER]);
    }
    for (int i = first_enemy; i < ndyn; i++) {
        t3d_matrix_set(&dyn[i], true);
        rspq_block_run(blocks[MDL_GNASHER]);
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

void render_end(void)
{
    gpu_timing_detach_show();
}
