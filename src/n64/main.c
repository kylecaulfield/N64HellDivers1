// HIVEBREAKERS - N64 entry point.
//
// Frame loop: the sim ticks at a fixed rate locked to the video interrupt
// (one tick per 2 vblanks = ~29.91 Hz on NTSC). Each loop iteration waits
// until at least one tick is due, runs the due ticks (max 2), then renders
// the latest state once.

#include <libdragon.h>
#include <t3d/t3d.h>

#include "autotest.h"
#include "bench.h"
#include "boot.h"
#include "fx.h"
#include "gpu_timing.h"
#include "hud.h"
#include "input.h"
#include "menu.h"
#include "mission.h"
#include "overlay.h"
#include "render.h"
#include "results.h"
#include "sim.h"
#include "tickclock.h"
#include "version.h"

#define VBLANKS_PER_TICK    2
#define MAX_TICKS_PER_FRAME 2
#define HEAP_BASELINE_FRAME 60 // let lazy allocations (e.g. the Z-buffer) settle first
#define LEAVE_FRAMES        30 // a controller unplugged this long takes its player out

static volatile uint32_t vblank_count;
static sim_t sim;

// Per-phase sim time for the current frame, summed over its ticks.
static uint32_t phase_ticks[TIMED_PHASES];
static uint32_t phase_start;
static sim_phase_t phase_cur;

static void on_vblank(void *arg)
{
    (void)arg;
    vblank_count++;
}

// Spin until the VI interrupt has counted another vblank. Not
// vi_wait_vblank: that polls for the one scanline the VI interrupt fires on,
// so whenever the interrupt handlers outlast that line it misses every
// vblank and never returns (seen in ares; whether it happens depends on
// cache layout).
static void wait_vblank(void)
{
    uint32_t v = vblank_count;
    while (vblank_count == v) {}
}

// A player whose controller has been unplugged for LEAVE_FRAMES leaves
// (player 1 never does): a bot takes over if bots are on.
static void update_leavers(uint8_t connected)
{
    static uint8_t gone[SIM_MAX_PLAYERS]; // frames without a controller
    for (int p = 1; p < SIM_MAX_PLAYERS; p++) {
        bool playing = sim.humans & (1u << p);
        if (!playing || (connected & (1u << p))) {
            gone[p] = 0;
        } else if (++gone[p] == LEAVE_FRAMES) {
            sim_player_leave(&sim, p);
            debugf("P%d left (controller unplugged)\n", p + 1);
        }
    }
}

// sim_step calls this at the start of each phase (and once at the end).
static void on_sim_mark(void *ctx, sim_phase_t phase)
{
    (void)ctx;
    uint32_t now = TICKS_READ();
    if (phase != SIM_PHASE_PLAYERS)
        phase_ticks[phase_cur] += now - phase_start;
    phase_cur = phase;
    phase_start = now;
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
    fx_init();
    sim_init(&sim, 0xC0FFEE);
    sim.mark = on_sim_mark;
    render_bind_world(&sim);

    vi_install_vblank_handler(on_vblank, NULL);
    tickclock_t clock;
    tickclock_init(&clock, VBLANKS_PER_TICK, MAX_TICKS_PER_FRAME, vblank_count);
    debugf("init done, refresh %.3f Hz\n", (double)display_get_refresh_rate());

    input_frame_t input;
    uint64_t last_frame = get_ticks();
    uint32_t frames = 0;
    bool paused = false;
    bool was_over = false;

    for (;;) {
        uint32_t ticks;
        while ((ticks = tickclock_advance(&clock, vblank_count)) == 0)
            wait_vblank();

        uint64_t t_start = get_ticks();
        uint32_t frame_us = (uint32_t)TICKS_TO_US(t_start - last_frame);
        last_frame = t_start;

        input_poll(&input);
#ifdef AUTOTEST
        autotest_input(&input, frames, &sim);
#endif
        uint8_t connected = 0;
        for (int p = 0; p < SIM_MAX_PLAYERS; p++)
            connected |= (uint8_t)(input.ports[p].connected << p);
        update_leavers(connected);

        // Once the mission is over the results screen is up; START there
        // starts a new mission with the same settings and players.
        bool over = mission_over(&sim);
        if (input.toggle_overlay)
            overlay_toggle();
        if (input.toggle_menu) {
            menu_toggle();
        } else if (menu_is_open()) {
            menu_update(input.pressed_any, &sim);
        } else {
            // START on a controller nobody is playing joins in; from someone
            // playing it pauses, or on the results screen starts the next
            // mission (joining there counts too).
            bool start = false;
            for (int p = 0; p < SIM_MAX_PLAYERS; p++) {
                if (!(input.ports[p].pressed & SIM_BTN_START))
                    continue;
                if (!(sim.humans & (1u << p))) {
                    bool now = sim_player_join(&sim, p);
                    debugf("P%d joined%s\n", p + 1, now ? "" : " for the next mission");
                    if (!over)
                        continue;
                }
                start = true;
            }
            if (start && over) {
                sim_restart(&sim, sim.rng.state ^ frames);
                render_bind_world(&sim);
                fx_init();
                over = was_over = false;
                // Building the new arena takes a few frames' time; that is
                // loading, not dropped gameplay ticks.
                tickclock_init(&clock, VBLANKS_PER_TICK, MAX_TICKS_PER_FRAME, vblank_count);
                debugf("new mission\n");
            } else if (start) {
                paused = !paused;
            }
            // On the pause screen, A switches that player's control scheme.
            for (int p = 0; paused && p < SIM_MAX_PLAYERS; p++) {
                if (!(sim.humans & (1u << p)) || !(input.ports[p].pressed & SIM_BTN_A))
                    continue;
                uint8_t *scheme = &sim.settings.scheme[p];
                *scheme = (uint8_t)((*scheme + 1) % SCHEME_COUNT);
                debugf("P%d control scheme %c\n", p + 1, *scheme == SCHEME_A ? 'A' : 'B');
            }
        }

        // The sim only runs while nothing is covering the game. Effects
        // freeze along with it.
        uint32_t run = (paused || menu_is_open() || over) ? 0 : ticks;
        for (int i = 0; i < TIMED_PHASES; i++)
            phase_ticks[i] = 0;
        for (uint32_t i = 0; i < run; i++) {
            sim_step(&sim, input.ports);
            // Button edges belong to the first tick only.
            for (int p = 0; p < SIM_MAX_PLAYERS; p++)
                input.ports[p].pressed = 0;
        }
#ifdef AUTOTEST
        autotest_observe(&sim);
#endif
        fx_spawn_from_events(&sim);
        sim_clear_events(&sim);
        fx_update(run);
        sim.counts.particles = (uint16_t)fx_count();
        uint64_t t_sim = get_ticks();

        if (mission_over(&sim) && !was_over) {
            was_over = true;
            results_log(&sim);
        }

        uint32_t wait_us = render_scene(&sim, (float)run * SIM_DT);
        uint64_t t_hud = get_ticks();
        hud_draw(&sim, paused && !menu_is_open(), connected);
        if (was_over)
            results_draw(&sim);
        menu_draw(&sim);
        overlay_draw();
        uint32_t hud_us = (uint32_t)TICKS_TO_US(get_ticks() - t_hud);
        render_end();
        gpu_timing_frame();
        uint64_t t_end = get_ticks();

        uint32_t sim_us = (uint32_t)TICKS_TO_US(t_sim - t_start);
        uint32_t total_us = (uint32_t)TICKS_TO_US(t_end - t_start);
        uint32_t render_us = (uint32_t)TICKS_TO_US(t_end - t_sim);
        const render_stats_t *rs = render_stats();
        // What is left of the render time once the framebuffer wait and the
        // CPU-side work are taken out is command issue, including any time
        // blocked on a full RSP queue.
        uint32_t cpu_side = wait_us + rs->anim_us + rs->build_us + hud_us;
        uint32_t submit_us = render_us > cpu_side ? render_us - cpu_side : 0;
        frame_timing_t timing = {
            .frame_us = frame_us,
            .cpu_us = total_us - wait_us,
            .sim_us = sim_us,
            .anim_us = rs->anim_us,
            .build_us = rs->build_us,
            .submit_us = submit_us,
            .hud_us = hud_us,
            .drawn_bugs = rs->drawn_bugs,
            .skeletons = rs->skeletons,
        };
        for (int i = 0; i < TIMED_PHASES; i++)
            timing.phase_us[i] = (uint32_t)TICKS_TO_US(phase_ticks[i]);
        overlay_record(&timing, &sim, &clock);
        bench_frame(&sim, &timing, &clock);

        if (++frames == HEAP_BASELINE_FRAME) {
            overlay_set_heap_baseline();
#ifdef BENCH_AT_BOOT
            bench_start(&sim);
#endif
        }
    }
}
