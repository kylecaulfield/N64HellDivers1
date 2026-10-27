#include "grid.h"

void grid_build(sim_t *s)
{
    grid_t *g = &s->grid;
    for (int i = 0; i < GRID_CELLS; i++)
        g->head[i] = -1;
    for (int n = 0; n < s->active_count; n++) {
        int i = s->active[n];
        const enemy_t *e = &s->enemies[i];
        if (!e->active)
            continue;
        int cell = grid_coord(e->pos.y) * GRID_W + grid_coord(e->pos.x);
        g->next[i] = g->head[cell];
        g->head[cell] = (int16_t)i;
    }
}
