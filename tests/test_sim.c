#include "test.h"
#include "test_util.h"
#include "weapons.h"

#include <string.h>

void test_sim_init_is_clean(void)
{
    sim_t s;
    memset(&s, 0xAB, sizeof(s));
    sim_init(&s, 99);
    CHECK_EQ_INT(s.tick, 0);
    CHECK_EQ_INT(s.counts.players, 1);  // M1: player 1 drops in at the start
    CHECK(s.players[0].active);
    for (int i = 1; i < SIM_MAX_PLAYERS; i++)
        CHECK(!s.players[i].active);
    CHECK_EQ_INT(s.counts.enemies, 0);
    CHECK_EQ_INT(s.counts.projectiles, 0);
    CHECK_EQ_INT(s.settings.enemy_cap, SIM_ENEMY_CAP_DEFAULT);
    CHECK(s.arena.obstacle_count > 0);
}

void test_sim_step_tracks_ports(void)
{
    sim_t s;
    sim_init(&s, 1);
    sim_input_t in[SIM_MAX_PLAYERS] = {0};
    in[0].connected = true;
    in[2].connected = true;
    sim_step(&s, in);
    CHECK_EQ_INT(s.tick, 1);
    CHECK(s.players[0].present);
    CHECK(!s.players[1].present);
    CHECK(s.players[2].present);
    CHECK(!s.players[3].present);
}

void test_sim_trickle_spawns_up_to_limit(void)
{
    sim_t s;
    sim_init(&s, 5);
    s.settings.god_mode = true;
    sim_input_t in[SIM_MAX_PLAYERS];
    inputs_none(in);
    step_n(&s, in, TRICKLE_INTERVAL * (TRICKLE_MAX_ALIVE + 4));
    CHECK_EQ_INT(s.counts.enemies, TRICKLE_MAX_ALIVE);
}

void test_sim_events_accumulate_until_cleared(void)
{
    sim_t s;
    setup_empty(&s);
    sim_input_t in[SIM_MAX_PLAYERS];
    inputs_none(in);
    in[0].held = SIM_BTN_Z;
    step_n(&s, in, 1 + weapon_defs[WEAPON_RIFLE].fire_interval);
    CHECK_EQ_INT(count_events(&s, EV_SHOT), 2);
    sim_clear_events(&s);
    CHECK_EQ_INT(s.event_count, 0);
}

// Field-by-field comparison: memcmp would also compare struct padding,
// which struct assignment leaves unspecified.
static bool same_state(const sim_t *a, const sim_t *b)
{
    if (a->tick != b->tick || a->rng.state != b->rng.state || a->kills != b->kills)
        return false;
    for (int i = 0; i < SIM_MAX_PLAYERS; i++) {
        const player_t *p = &a->players[i], *q = &b->players[i];
        if (p->active != q->active || p->state != q->state || p->pos.x != q->pos.x ||
            p->pos.y != q->pos.y || p->hp != q->hp || p->weapon != q->weapon ||
            p->weapons[0].mag != q->weapons[0].mag || p->weapons[1].mag != q->weapons[1].mag)
            return false;
    }
    for (int i = 0; i < SIM_MAX_ENEMIES; i++) {
        const enemy_t *e = &a->enemies[i], *f = &b->enemies[i];
        if (e->active != f->active)
            return false;
        if (e->active && (e->pos.x != f->pos.x || e->pos.y != f->pos.y || e->hp != f->hp || e->state != f->state))
            return false;
    }
    for (int i = 0; i < SIM_MAX_PROJECTILES; i++) {
        const projectile_t *p = &a->projectiles[i], *q = &b->projectiles[i];
        if (p->active != q->active)
            return false;
        if (p->active && (p->pos.x != q->pos.x || p->pos.y != q->pos.y))
            return false;
    }
    return a->camera.target.x == b->camera.target.x && a->camera.dist == b->camera.dist;
}

void test_sim_same_seed_same_state(void)
{
    // A busy run (moving, shooting, bugs spawning and dying) must replay
    // bit-for-bit from the same seed and inputs.
    sim_t a, b;
    sim_init(&a, 555);
    sim_init(&b, 555);
    sim_input_t in[SIM_MAX_PLAYERS];
    inputs_none(in);
    for (int t = 0; t < 600; t++) {
        in[0].stick_x = (int8_t)((t / 40) % 2 ? 80 : -80);
        in[0].stick_y = (int8_t)((t / 25) % 2 ? 60 : -60);
        in[0].held = (t % 50) < 35 ? SIM_BTN_Z : 0;
        in[0].pressed = (t % 97) == 0 ? SIM_BTN_A : 0;
        sim_step(&a, in);
        sim_step(&b, in);
        sim_clear_events(&a);
        sim_clear_events(&b);
    }
    CHECK(a.kills > 0 || a.counts.enemies > 0);
    CHECK(same_state(&a, &b));
}
