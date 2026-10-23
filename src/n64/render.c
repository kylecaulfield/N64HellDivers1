#include "render.h"
#include "gpu_timing.h"
#include "version.h"

#include <libdragon.h>
#include <stdio.h>
#include <t3d/t3d.h>
#include <t3d/t3dmodel.h>

// M0 scene: every generated model on a turntable, seen from the fixed
// gameplay camera angle. It proves the asset pipeline and gives the RDP and
// RSP something representative to chew on.

enum {
    MDL_SOLDIER, MDL_SKITTER, MDL_GNASHER, MDL_BULWARK, MDL_DROPSHIP, MDL_BEACON,
    MDL_TURRET, MDL_ROCK_A, MDL_ROCK_B, MDL_ROCK_C, MDL_GROUND, MDL_COUNT
};

static const char *const model_names[MDL_COUNT] = {
    "soldier", "skitter", "gnasher", "bulwark", "dropship", "beacon",
    "turret", "rock_a", "rock_b", "rock_c", "ground",
};

typedef struct {
    int model;
    float x, z;       // metres; +z is towards the camera
    float scale;      // extra scale on top of the import scale
    float spin;       // radians per second
    bool label;
} instance_t;

static const instance_t instances[] = {
    { MDL_GROUND,   0.0f,   0.0f, 10.0f, 0.0f,  false },
    { MDL_DROPSHIP, 0.0f, -10.0f, 1.0f,  0.25f, true  },
    { MDL_BULWARK, -6.5f,  -1.5f, 1.0f,  0.6f,  true  },
    { MDL_GNASHER, -3.9f,  -1.5f, 1.0f,  0.6f,  true  },
    { MDL_SKITTER, -1.3f,  -1.5f, 1.0f,  0.6f,  true  },
    { MDL_SOLDIER,  1.3f,  -1.5f, 1.0f,  0.6f,  true  },
    { MDL_TURRET,   3.9f,  -1.5f, 1.0f,  0.6f,  true  },
    { MDL_BEACON,   6.5f,  -1.5f, 1.0f,  0.6f,  true  },
    { MDL_ROCK_A,  -4.5f,   3.0f, 1.0f,  0.3f,  true  },
    { MDL_ROCK_B,   0.0f,   3.0f, 1.0f,  0.3f,  true  },
    { MDL_ROCK_C,   4.5f,   3.0f, 1.0f,  0.3f,  true  },
};
#define INSTANCE_COUNT (sizeof(instances) / sizeof(instances[0]))

// Fixed gameplay camera: pitched down, looking north (-z).
#define CAM_PITCH_DEG  55.0f
#define CAM_DIST_M     16.0f
#define CAM_FOV_DEG    45.0f

static T3DModel *models[MDL_COUNT];
static rspq_block_t *blocks[MDL_COUNT];
static T3DMat4FP *matrices; // [INSTANCE_COUNT][FB_COUNT], uncached
static T3DViewport viewport;
static int frame_idx;

void render_init(void)
{
    for (int i = 0; i < MDL_COUNT; i++) {
        char path[64];
        snprintf(path, sizeof(path), "rom:/models/%s.t3dm", model_names[i]);
        models[i] = t3d_model_load(path);

        rspq_block_begin();
        t3d_model_draw(models[i]);
        blocks[i] = rspq_block_end();
    }

    // DMA'd by the RSP, so allocated uncached. All of it up front: nothing in
    // the frame loop allocates.
    matrices = malloc_uncached(sizeof(T3DMat4FP) * INSTANCE_COUNT * FB_COUNT);
    viewport = t3d_viewport_create_buffered(FB_COUNT);
}

uint32_t render_scene(const sim_t *sim)
{
    frame_idx = (frame_idx + 1) % FB_COUNT;
    float t = (float)sim->tick * (1.0f / 30.0f);

    for (unsigned i = 0; i < INSTANCE_COUNT; i++) {
        const instance_t *in = &instances[i];
        float s = in->scale * (WORLD_SCALE / 64.0f);
        t3d_mat4fp_from_srt_euler(&matrices[i * FB_COUNT + frame_idx],
            (float[3]){ s, s, s },
            (float[3]){ 0.0f, t * in->spin, 0.0f },
            (float[3]){ in->x * WORLD_SCALE, 0.0f, in->z * WORLD_SCALE });
    }

    const float pitch = T3D_DEG_TO_RAD(CAM_PITCH_DEG);
    T3DVec3 target = {{ 0.0f, 0.0f, 0.0f }};
    T3DVec3 eye = {{ 0.0f, fm_sinf(pitch) * CAM_DIST_M * WORLD_SCALE, fm_cosf(pitch) * CAM_DIST_M * WORLD_SCALE }};
    t3d_viewport_set_projection(&viewport, T3D_DEG_TO_RAD(CAM_FOV_DEG), 2.0f * WORLD_SCALE, 120.0f * WORLD_SCALE);
    t3d_viewport_look_at(&viewport, &eye, &target, &(T3DVec3){{ 0.0f, 1.0f, 0.0f }});

    uint64_t wait_start = get_ticks();
    surface_t *fb = display_get();
    uint32_t wait_us = (uint32_t)TICKS_TO_US(get_ticks() - wait_start);

    rdpq_attach(fb, display_get_zbuf());
    gpu_timing_submit_begin();
    t3d_frame_start();
    t3d_viewport_attach(&viewport);
    t3d_screen_clear_color(RGBA32(40, 44, 52, 0xFF));
    t3d_screen_clear_depth();

    uint8_t ambient[4] = { 70, 70, 84, 0xFF };
    uint8_t sun[4] = { 0xFF, 0xF0, 0xDC, 0xFF };
    T3DVec3 sun_dir = {{ -0.5f, 1.0f, 0.4f }};
    t3d_vec3_norm(&sun_dir);
    t3d_light_set_ambient(ambient);
    t3d_light_set_directional(0, sun, &sun_dir);
    t3d_light_set_count(1);

    t3d_matrix_push_pos(1);
    for (unsigned i = 0; i < INSTANCE_COUNT; i++) {
        t3d_matrix_set(&matrices[i * FB_COUNT + frame_idx], true);
        rspq_block_run(blocks[instances[i].model]);
    }
    t3d_matrix_pop(1);

    // Name tags under each model, so each one can be checked on hardware.
    for (unsigned i = 0; i < INSTANCE_COUNT; i++) {
        const instance_t *in = &instances[i];
        if (!in->label)
            continue;
        T3DVec3 world = {{ in->x * WORLD_SCALE, 0.0f, (in->z + 1.2f) * WORLD_SCALE }};
        T3DVec3 screen;
        t3d_viewport_calc_viewspace_pos(&viewport, &screen, &world);
        // Whole-pixel positions: glyphs at fractional offsets get filtered to mush.
        rdpq_text_printf(&(rdpq_textparms_t){ .width = 80, .align = ALIGN_CENTER },
                         FONT_BUILTIN_DEBUG_MONO, (int)screen.v[0] - 40, (int)screen.v[1],
                         "%s", model_names[in->model]);
    }
    return wait_us;
}

void render_hud(void)
{
    rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 12, 230,
                     "HIVEBREAKERS M0 %s  L+DUP: debug", GAME_VERSION);
}

void render_end(void)
{
    gpu_timing_detach_show();
}
