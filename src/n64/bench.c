#include "bench.h"
#include "gpu_timing.h"
#include "version.h"

#include <libdragon.h>
#include <stdio.h>

static const uint16_t LEVELS[] = { 16, 32, 48, 64, 80, 96, 112, 128, 160, 192, 224, 256 };
#define LEVEL_COUNT  (int)(sizeof(LEVELS) / sizeof(LEVELS[0]))
#define SETTLE       60   // frames before measuring each level
#define MEASURE      120  // frames measured per level
#define TOPUP_MAX    8    // bugs spawned per frame while filling up

static struct {
    bool active;
    int level;
    int frame;           // frames into the current level
    uint32_t drop_start; // dropped-tick counter when measuring began
    // Sums (and maxima) over the measured frames.
    uint64_t frame_us, cpu_us, sim_us, anim_us, build_us, submit_us, hud_us, rdp_us, gpu_us;
    uint64_t phase_us[TIMED_PHASES];
    uint32_t frame_max, cpu_max, enemies, drawn, projectiles, particles;
    uint32_t rdp_n, gpu_n;
    char status[48];
} b;

static void reset_sums(void)
{
    b.frame_us = b.cpu_us = b.sim_us = b.anim_us = b.build_us = b.submit_us = b.hud_us = b.rdp_us = b.gpu_us = 0;
    for (int i = 0; i < TIMED_PHASES; i++)
        b.phase_us[i] = 0;
    b.frame_max = b.cpu_max = b.enemies = b.drawn = b.projectiles = b.particles = 0;
    b.rdp_n = b.gpu_n = 0;
}

void bench_start(sim_t *sim)
{
    sim_settings_t *st = &sim->settings;
    st->god_mode = true;
    st->infinite_ammo = true;
    st->force_max_zoom = true;
    st->waves = false;
    st->enemy_cap = SIM_MAX_ENEMIES;
    sim_kill_all_enemies(sim);
    sim_set_bots(sim, true);
    b.active = true;
    b.level = 0;
    b.frame = 0;
    reset_sums();
    debugf("BENCH START build=%s levels=%d settle=%d measure=%d frames\n",
           GAME_VERSION, LEVEL_COUNT, SETTLE, MEASURE);
}

bool bench_active(void)
{
    return b.active;
}

const char *bench_status(void)
{
    return b.active ? b.status : NULL;
}

static unsigned long avg(uint64_t sum, uint32_t n)
{
    return n ? (unsigned long)(sum / n) : 0;
}

static void report(const tickclock_t *clock)
{
    uint32_t n = MEASURE;
    uint32_t phase_avg[TIMED_PHASES];
    for (int i = 0; i < TIMED_PHASES; i++)
        phase_avg[i] = (uint32_t)avg(b.phase_us[i], n);
    char ph[64];
    overlay_format_phases(ph, sizeof(ph), phase_avg);
    unsigned long fa = avg(b.frame_us, n);
    unsigned long fps100 = fa ? 100000000ul / fa : 0;
    debugf("BENCH level=%u enemies=%lu drawn=%lu fps=%lu.%02lu frame_avg=%lu frame_max=%lu "
           "cpu_avg=%lu cpu_max=%lu sim=%lu ph=%s anim=%lu build=%lu submit=%lu hud=%lu "
           "rdp=%ld gpu_done=%ld drop=%lu proj=%lu fx=%lu\n",
           LEVELS[b.level], avg(b.enemies, n), avg(b.drawn, n), fps100 / 100, fps100 % 100,
           fa, (unsigned long)b.frame_max, avg(b.cpu_us, n), (unsigned long)b.cpu_max, avg(b.sim_us, n), ph,
           avg(b.anim_us, n), avg(b.build_us, n), avg(b.submit_us, n), avg(b.hud_us, n),
           b.rdp_n ? (long)avg(b.rdp_us, b.rdp_n) : -1L, b.gpu_n ? (long)avg(b.gpu_us, b.gpu_n) : -1L,
           (unsigned long)(clock->dropped_ticks - b.drop_start),
           avg(b.projectiles, n), avg(b.particles, n));
}

void bench_frame(sim_t *sim, const frame_timing_t *t, const tickclock_t *clock)
{
    if (!b.active)
        return;
    int want = LEVELS[b.level];

    // Hold the level: spawn a mixed swarm on screen around the squad.
    int missing = want - (int)sim->counts.enemies;
    if (missing > 0)
        sim_spawn_mix(sim, missing > TOPUP_MAX ? TOPUP_MAX : missing, sim->camera.target, 4.0f, 11.0f);

    if (b.frame == SETTLE) {
        reset_sums();
        b.drop_start = clock->dropped_ticks;
    }
    if (b.frame >= SETTLE) {
        b.frame_us += t->frame_us;
        b.cpu_us += t->cpu_us;
        b.sim_us += t->sim_us;
        for (int i = 0; i < TIMED_PHASES; i++)
            b.phase_us[i] += t->phase_us[i];
        b.anim_us += t->anim_us;
        b.build_us += t->build_us;
        b.submit_us += t->submit_us;
        b.hud_us += t->hud_us;
        if (t->frame_us > b.frame_max)
            b.frame_max = t->frame_us;
        if (t->cpu_us > b.cpu_max)
            b.cpu_max = t->cpu_us;
        b.enemies += sim->counts.enemies;
        b.drawn += t->drawn_bugs;
        b.projectiles += sim->counts.projectiles;
        b.particles += sim->counts.particles;
        uint32_t rdp = gpu_timing_rdp_us(), gpu = gpu_timing_gpu_us();
        if (rdp != GPU_TIMING_NA) {
            b.rdp_us += rdp;
            b.rdp_n++;
        }
        if (gpu != GPU_TIMING_NA) {
            b.gpu_us += gpu;
            b.gpu_n++;
        }
    }
    snprintf(b.status, sizeof(b.status), "BENCH %d/%d: %d BUGS", b.level + 1, LEVEL_COUNT, want);

    if (++b.frame >= SETTLE + MEASURE) {
        report(clock);
        b.frame = 0;
        if (++b.level >= LEVEL_COUNT) {
            b.active = false;
            debugf("BENCH DONE\n");
        }
    }
}
