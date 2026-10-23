#ifndef SIM_GRID_H
#define SIM_GRID_H

// Uniform grid over the arena for neighbour queries (separation, bullet
// hits, player shoving). Rebuilt from the active-enemy list each tick; a
// cell is larger than any interaction distance, so a 3x3 block of cells
// around a point holds everything that can touch it.

#include "sim.h"

void grid_build(sim_t *s);

// Iterates the enemies in every cell overlapping [min, max]. Dead enemies
// may still be listed until the next rebuild: check `active`.
typedef struct {
    const grid_t *g;
    int x0, x1, y1;
    int x, y;
    int cur;
} grid_iter_t;

static inline int grid_coord(float v)
{
    int c = (int)((v + ARENA_HALF_SIZE) * (1.0f / GRID_CELL));
    return c < 0 ? 0 : (c >= GRID_W ? GRID_W - 1 : c);
}

static inline void grid_iter_init(grid_iter_t *it, const sim_t *s, vec2 min, vec2 max)
{
    it->g = &s->grid;
    it->x0 = grid_coord(min.x);
    it->x1 = grid_coord(max.x);
    it->y1 = grid_coord(max.y);
    it->x = it->x0;
    it->y = grid_coord(min.y);
    it->cur = it->g->head[it->y * GRID_W + it->x];
}

// Enemy index, or -1 when done.
static inline int grid_iter_next(grid_iter_t *it)
{
    while (it->cur < 0) {
        if (++it->x > it->x1) {
            it->x = it->x0;
            if (++it->y > it->y1)
                return -1;
        }
        it->cur = it->g->head[it->y * GRID_W + it->x];
    }
    int i = it->cur;
    it->cur = it->g->next[i];
    return i;
}

#endif
