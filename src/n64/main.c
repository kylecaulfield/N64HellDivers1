// HIVEBREAKERS - N64 entry point.
//
// Frame loop: the sim ticks at a fixed rate locked to the video interrupt
// (one tick per 2 vblanks = ~29.91 Hz on NTSC). Each loop iteration waits
// until at least one tick is due, runs the due ticks (max 2), then renders
// the latest state once.

#include <libdragon.h>
#include <t3d/t3d.h>

#include "boot.h"
#include "gpu_timing.h"
#include "input.h"
#include "overlay.h"
#include "render.h"
#include "sim.h"
#include "tickclock.h"
#include "version.h"

#define VBLANKS_PER_TICK    2
#define MAX_TICKS_PER_FRAME 2
#define HEAP_BASELINE_FRAME 60 // let lazy allocations (e.g. the Z-buffer) settle first

static volatile uint32_t vblank_count;
static sim_t sim;

static void on_vblank(void *arg)
{
    (void)arg;
    vblank_count++;
}

int main(void)
{
    debug_init_usblog();
    debug_init_isviewer();
    debugf("HIVEBREAKERS %s (built %s) booting\n", GAME_VERSION, GAME_BUILD_DATE);

    boot_require_expansion_pak();
    debugf("RDRAM: %d KiB\n", get_memory_size() / 1024);

    dfs_init(DFS_DEFAULT_LOCATION);
    display_init(RESOLUTION_320x240, DEPTH_16_BPP, FB_COUNT, GAMMA_NONE, FILTERS_RESAMPLE_ANTIALIAS_DEDITHER);
    display_set_fps_limit(display_get_refresh_rate() / VBLANKS_PER_TICK);
    rdpq_init();
    t3d_init((T3DInitParams){});

    input_init();
    overlay_init();
    gpu_timing_init();
    render_init();
    sim_init(&sim, 0xC0FFEE);

    vi_install_vblank_handler(on_vblank, NULL);
    tickclock_t clock;
    tickclock_init(&clock, VBLANKS_PER_TICK, MAX_TICKS_PER_FRAME, vblank_count);
    debugf("init done, refresh %.3f Hz\n", (double)display_get_refresh_rate());

    input_frame_t input;
    uint64_t last_frame = get_ticks();
    uint32_t frames = 0;

    for (;;) {
        uint32_t ticks;
        while ((ticks = tickclock_advance(&clock, vblank_count)) == 0)
            vi_wait_vblank();

        uint64_t t_start = get_ticks();
        uint32_t frame_us = (uint32_t)TICKS_TO_US(t_start - last_frame);
        last_frame = t_start;

        input_poll(&input);
        if (input.toggle_overlay)
            overlay_toggle();

        for (uint32_t i = 0; i < ticks; i++) {
            sim_step(&sim, input.ports);
            // Button edges belong to the first tick only.
            for (int p = 0; p < SIM_MAX_PLAYERS; p++)
                input.ports[p].pressed = 0;
        }
        uint64_t t_sim = get_ticks();

        uint32_t wait_us = render_scene(&sim);
        render_hud();
        overlay_draw();
        render_end();
        gpu_timing_frame();
        uint64_t t_end = get_ticks();

        uint32_t sim_us = (uint32_t)TICKS_TO_US(t_sim - t_start);
        uint32_t total_us = (uint32_t)TICKS_TO_US(t_end - t_start);
        overlay_record(&(frame_timing_t){
            .frame_us = frame_us,
            .cpu_us = total_us - wait_us,
            .sim_us = sim_us,
        }, &sim, &clock);

        if (++frames == HEAP_BASELINE_FRAME)
            overlay_set_heap_baseline();
    }
}
