#include "test.h"
#include "sim.h"

#include <string.h>

void test_sim_init_is_clean(void)
{
    sim_t s;
    memset(&s, 0xAB, sizeof(s));
    sim_init(&s, 99);
    CHECK_EQ_INT(s.tick, 0);
    CHECK_EQ_INT(s.counts.players, 0);
    CHECK_EQ_INT(s.counts.enemies, 0);
    for (int i = 0; i < SIM_MAX_PLAYERS; i++)
        CHECK(!s.players[i].present);
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
    CHECK_EQ_INT(s.counts.players, 2);
    CHECK(s.players[0].present);
    CHECK(!s.players[1].present);
    CHECK(s.players[2].present);
    CHECK(!s.players[3].present);

    in[2].connected = false;
    sim_step(&s, in);
    CHECK_EQ_INT(s.tick, 2);
    CHECK_EQ_INT(s.counts.players, 1);
}

void test_sim_same_seed_same_state(void)
{
    sim_t a, b;
    sim_init(&a, 555);
    sim_init(&b, 555);
    sim_input_t in[SIM_MAX_PLAYERS] = {0};
    in[1].connected = true;
    for (int i = 0; i < 100; i++) {
        sim_step(&a, in);
        sim_step(&b, in);
    }
    CHECK(memcmp(&a, &b, sizeof(a)) == 0);
}
