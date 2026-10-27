#include "waves.h"
#include "difficulty.h"
#include "enemy.h"
#include "flow.h"
#include "mission.h"

// The standard ramp.
static void base_composition(int wave, uint16_t out[ENEMY_TYPE_COUNT])
{
    static const uint16_t early[3][ENEMY_TYPE_COUNT] = {
        { 6, 3, 0 },
        { 10, 5, 0 },
        { 14, 7, 1 },
    };
    if (wave < 1)
        wave = 1;
    if (wave <= 3) {
        for (int t = 0; t < ENEMY_TYPE_COUNT; t++)
            out[t] = early[wave - 1][t];
        return;
    }
    int k = wave - 3;
    out[ENEMY_SKITTER] = (uint16_t)(14 + 4 * k);
    out[ENEMY_GNASHER] = (uint16_t)(7 + 2 * k);
    out[ENEMY_BULWARK] = (uint16_t)(1 + k / 2);
}

void wave_composition(int wave, int difficulty, int troopers, uint16_t out[ENEMY_TYPE_COUNT])
{
    // Percent of the wave by squad size (bots count): the standard waves are
    // tuned for three troopers.
    static const int SQUAD[SIM_MAX_PLAYERS] = { 70, 85, 100, 115 };
    const difficulty_def_t *d = &difficulty_defs[difficulty < DIFFICULTY_COUNT ? difficulty : DIFFICULTY_STANDARD];
    troopers = troopers < 1 ? 1 : troopers > SIM_MAX_PLAYERS ? SIM_MAX_PLAYERS : troopers;
    base_composition(wave + d->wave_offset, out);
    for (int t = 0; t < ENEMY_TYPE_COUNT; t++)
        out[t] = (uint16_t)((out[t] * d->wave_size * SQUAD[troopers - 1] + 5000) / 10000);
}

void waves_reset(sim_t *s)
{
    s->waves = (waves_t){ .number = 0, .state = WAVE_BREAK, .timer = WAVE_FIRST_DELAY };
}

// A point off-screen around the camera, inside the arena and not in a rock.
static vec2 breach_point(sim_t *s)
{
    const arena_t *a = &s->arena;
    vec2 p = s->camera.target;
    for (int tries = 0; tries < 8; tries++) {
        float ang = rng_float(&s->rng) * 6.2832f;
        float r = BREACH_RING_MIN + rng_float(&s->rng) * (BREACH_RING_MAX - BREACH_RING_MIN);
        p = v2_add(s->camera.target, v2_rotate(v2(r, 0.0f), ang));
        p.x = clampf(p.x, a->min.x + 2.0f, a->max.x - 2.0f);
        p.y = clampf(p.y, a->min.y + 2.0f, a->max.y - 2.0f);
        if (!s->flow.blocked[flow_cell_of(p)])
            break;
    }
    return p;
}

static void start_wave(sim_t *s)
{
    waves_t *w = &s->waves;
    w->number++;
    w->state = WAVE_ACTIVE;
    w->timer = 0;
    wave_composition(w->number, s->mission.difficulty, s->counts.players, w->remaining);
    w->total = 0;
    for (int t = 0; t < ENEMY_TYPE_COUNT; t++)
        w->total = (uint16_t)(w->total + w->remaining[t]);
}

static int remaining_total(const waves_t *w)
{
    int n = 0;
    for (int t = 0; t < ENEMY_TYPE_COUNT; t++)
        n += w->remaining[t];
    return n;
}

static void spawn_group(sim_t *s)
{
    waves_t *w = &s->waves;
    vec2 at = breach_point(s);
    for (int n = 0; n < WAVE_GROUP_SIZE; n++) {
        int left = remaining_total(w);
        if (left == 0)
            return;
        // Pick a type weighted by how many of each are still to come.
        int pick = (int)rng_range(&s->rng, (uint32_t)left);
        int type = 0;
        while (pick >= w->remaining[type])
            pick -= w->remaining[type++];
        vec2 jitter = v2((rng_float(&s->rng) - 0.5f) * 3.0f, (rng_float(&s->rng) - 0.5f) * 3.0f);
        if (sim_spawn_enemy(s, (enemy_type_t)type, v2_add(at, jitter)) < 0)
            return; // at the cap: try again next interval
        w->remaining[type]--;
    }
}

void waves_update(sim_t *s)
{
    if (!s->settings.waves || s->mission.phase == MISSION_DROP)
        return;
    waves_t *w = &s->waves;
    // Holding out for the dropship (or uploading): shorter breaks, groups
    // more often. Harder levels shorten them too.
    int speedup = s->mission.phase == MISSION_HOLD || s->mission.phase == MISSION_BOARDING ? HOLD_WAVE_SPEEDUP
                  : mission_upload_running(s)                                             ? UPLOAD_WAVE_SPEEDUP
                                                                                          : 1;
    int cadence = sim_difficulty(s)->cadence;
    if (w->timer) {
        w->timer--;
        return;
    }
    if (w->state == WAVE_BREAK) {
        start_wave(s);
        return;
    }
    if (remaining_total(w) > 0) {
        spawn_group(s);
        w->timer = (uint16_t)(WAVE_GROUP_INTERVAL * cadence / 100 / speedup);
    } else if (s->counts.enemies <= w->total / WAVE_CLEAR_DIVISOR) {
        w->state = WAVE_BREAK;
        w->timer = (uint16_t)(WAVE_BREAK_TICKS * cadence / 100 / speedup);
    }
}
