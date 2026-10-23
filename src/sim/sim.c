#include "sim.h"

#include <string.h>

void sim_init(sim_t *s, uint32_t seed)
{
    memset(s, 0, sizeof(*s));
    rng_seed(&s->rng, seed);
}

void sim_step(sim_t *s, const sim_input_t inputs[SIM_MAX_PLAYERS])
{
    uint16_t players = 0;
    for (int i = 0; i < SIM_MAX_PLAYERS; i++) {
        s->players[i].present = inputs[i].connected;
        if (s->players[i].present)
            players++;
    }
    s->counts.players = players;
    s->tick++;
}
