#include "sim.h"

#include <string.h>

#include "arena.h"
#include "camera.h"
#include "sim_internal.h"

void sim_init(sim_t *s, uint32_t seed)
{
    memset(s, 0, sizeof(*s));
    rng_seed(&s->rng, seed);
    s->settings.enemy_cap = SIM_ENEMY_CAP_DEFAULT;
    s->settings.trickle_spawn = true;
    arena_generate(&s->arena, &s->rng);
    // M1: one player, on port 1, at the drop zone. Joining arrives in M5.
    sim_spawn_player(s, 0, v2(0.0f, 0.0f));
    camera_reset(s);
    sim_update_counts(s);
}

void sim_push_event(sim_t *s, sim_event_type_t type, uint8_t a, vec2 pos, vec2 dir)
{
    if (s->event_count >= SIM_MAX_EVENTS) {
        s->events_dropped++;
        return;
    }
    s->events[s->event_count++] = (sim_event_t){ .type = (uint8_t)type, .a = a, .pos = pos, .dir = dir };
}

void sim_clear_events(sim_t *s)
{
    s->event_count = 0;
}

void sim_update_counts(sim_t *s)
{
    uint16_t players = 0, enemies = 0, projectiles = 0;
    for (int i = 0; i < SIM_MAX_PLAYERS; i++)
        if (s->players[i].active)
            players++;
    for (int i = 0; i < SIM_MAX_ENEMIES; i++)
        if (s->enemies[i].active)
            enemies++;
    for (int i = 0; i < SIM_MAX_PROJECTILES; i++)
        if (s->projectiles[i].active)
            projectiles++;
    s->counts.players = players;
    s->counts.enemies = enemies;
    s->counts.projectiles = projectiles;
}

void sim_step(sim_t *s, const sim_input_t inputs[SIM_MAX_PLAYERS])
{
    player_intent_t intents[SIM_MAX_PLAYERS];
    for (int i = 0; i < SIM_MAX_PLAYERS; i++) {
        player_t *p = &s->players[i];
        p->present = inputs[i].connected;
        intents[i] = controls_read((control_scheme_t)s->settings.scheme[i], &inputs[i], &p->b_held);
    }

    players_update(s, intents);
    enemies_update(s);
    projectiles_update(s);
    spawner_update(s);
    camera_update(s);
    sim_update_counts(s);
    s->tick++;
}
