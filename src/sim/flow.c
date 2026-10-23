#include "flow.h"

#define COST_STRAIGHT 2
#define COST_DIAGONAL 3

// The 8 steps: 4 straight, then 4 diagonal. OFF is the same step as an
// offset in the cell array.
static const int8_t DX[8] = { 1, -1, 0, 0, 1, 1, -1, -1 };
static const int8_t DY[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };
static const int16_t OFF[8] = { 1, -1, FLOW_W, -FLOW_W, FLOW_W + 1, -FLOW_W + 1, FLOW_W - 1, -FLOW_W - 1 };

static int coord(float v)
{
    int c = (int)((v + ARENA_HALF_SIZE) * (1.0f / FLOW_CELL));
    return c < 0 ? 0 : (c >= FLOW_W ? FLOW_W - 1 : c);
}

int flow_cell_of(vec2 pos)
{
    return coord(pos.y) * FLOW_W + coord(pos.x);
}

static vec2 cell_center(int cell)
{
    return v2(-ARENA_HALF_SIZE + ((float)(cell % FLOW_W) + 0.5f) * FLOW_CELL,
              -ARENA_HALF_SIZE + ((float)(cell / FLOW_W) + 0.5f) * FLOW_CELL);
}

// Diagonal steps may not cut a blocked corner.
static bool step_ok(const flow_t *f, int x, int y, int k)
{
    int nx = x + DX[k], ny = y + DY[k];
    if (nx < 0 || ny < 0 || nx >= FLOW_W || ny >= FLOW_W)
        return false;
    if (f->blocked[ny * FLOW_W + nx])
        return false;
    if (k >= 4 && (f->blocked[y * FLOW_W + nx] || f->blocked[ny * FLOW_W + x]))
        return false;
    return true;
}

void flow_init(flow_t *f, const arena_t *a)
{
    for (int c = 0; c < FLOW_CELLS; c++) {
        vec2 p = cell_center(c);
        uint8_t b = 0;
        for (int i = 0; i < a->obstacle_count && !b; i++) {
            float r = a->obstacles[i].radius + FLOW_ROCK_MARGIN;
            b = v2_dist2(p, a->obstacles[i].pos) < r * r;
        }
        f->blocked[c] = b;
        f->dist[0][c] = f->dist[1][c] = FLOW_INF;
    }
    // Rocks never move, so the walkable steps out of each cell are fixed:
    // the build and the bugs test one bit instead of bounds and corners.
    for (int c = 0; c < FLOW_CELLS; c++) {
        uint8_t m = 0;
        for (int k = 0; k < 8; k++)
            if (step_ok(f, c % FLOW_W, c / FLOW_W, k))
                m |= (uint8_t)(1u << k);
        f->steps[c] = m;

        // Rocks that can touch a bug anywhere in this cell.
        vec2 p = cell_center(c);
        int n = 0;
        f->rocks[c][0] = f->rocks[c][1] = 0;
        for (int i = 0; i < a->obstacle_count; i++) {
            float r = a->obstacles[i].radius + ENEMY_MAX_RADIUS + 0.7072f * FLOW_CELL;
            if (v2_dist2(p, a->obstacles[i].pos) >= r * r)
                continue;
            if (n == 2) {
                f->rocks[c][0] = FLOW_ROCKS_ALL;
                break;
            }
            f->rocks[c][n++] = (uint8_t)(i + 1);
        }
    }
    f->limit = FLOW_INF;
    f->started = 0;
    f->cur = 0;
    f->building = false;
    f->ready = false;
    f->builds = 0;
    f->overflows = 0;
}

static void push(flow_t *f, int cell, uint32_t d)
{
    int b = (int)(d & 3);
    if (f->qcount[b] >= FLOW_QCAP) {
        f->overflows++;
        return;
    }
    int at = (f->qhead[b] + f->qcount[b]) & (FLOW_QCAP - 1);
    f->queue[b][at] = (uint16_t)cell;
    f->qcount[b]++;
}

static void begin(sim_t *s)
{
    flow_t *f = &s->flow;
    uint16_t *dist = f->dist[f->cur ^ 1];
    for (int c = 0; c < FLOW_CELLS; c++)
        dist[c] = FLOW_INF;
    for (int b = 0; b < 4; b++)
        f->qhead[b] = f->qcount[b] = 0;
    f->level = 0;
    f->building = true;
    f->started = s->tick;

    // Only build as far as the bugs are, by the last field, plus a margin.
    // A bug on a cell the last field never reached (just spawned) needs it
    // all; one inside a rock's margin is left to its neighbours.
    f->limit = FLOW_INF;
    if (f->ready) {
        const uint16_t *old = f->dist[f->cur];
        uint32_t far = 0;
        for (int n = 0; n < s->active_count; n++) {
            const enemy_t *e = &s->enemies[s->active[n]];
            int c = flow_cell_of(e->pos);
            if (!e->active || f->blocked[c])
                continue;
            if (old[c] == FLOW_INF) {
                far = FLOW_INF;
                break;
            }
            if (old[c] > far)
                far = old[c];
        }
        if (far != FLOW_INF)
            f->limit = far + FLOW_LIMIT_MARGIN;
    }
    for (int i = 0; i < SIM_MAX_PLAYERS; i++) {
        if (!sim_player_alive(&s->players[i]))
            continue;
        int c = flow_cell_of(s->players[i].pos);
        if (dist[c] != 0) {
            dist[c] = 0;
            push(f, c, 0);
        }
    }
}

int flow_step(sim_t *s, int budget)
{
    flow_t *f = &s->flow;
    if (!f->building)
        begin(s);
    uint16_t *dist = f->dist[f->cur ^ 1];
    int done = 0;

    while (done < budget) {
        // Find the next non-empty bucket. With costs <= 3, if four buckets
        // in a row are empty the build is complete.
        int empty = 0;
        while (f->qcount[f->level & 3] == 0) {
            if (++empty > 4)
                break;
            f->level++;
        }
        if (empty > 4 || f->level > f->limit) {
            f->cur ^= 1;
            f->building = false;
            f->ready = true;
            f->builds++;
            break;
        }

        int b = (int)(f->level & 3);
        int cell = f->queue[b][f->qhead[b]];
        f->qhead[b] = (uint16_t)((f->qhead[b] + 1) & (FLOW_QCAP - 1));
        f->qcount[b]--;
        if (dist[cell] != f->level)
            continue; // stale entry: a shorter path already settled this cell
        done++;

        unsigned steps = f->steps[cell];
        for (int k = 0; steps; k++, steps >>= 1) {
            if (!(steps & 1))
                continue;
            int n = cell + OFF[k];
            uint32_t nd = f->level + (k < 4 ? COST_STRAIGHT : COST_DIAGONAL);
            if (nd < dist[n]) {
                dist[n] = (uint16_t)nd;
                push(f, n, nd);
            }
        }
    }
    return done;
}

uint16_t flow_distance(const flow_t *f, vec2 pos)
{
    if (!f->ready)
        return FLOW_INF;
    return f->dist[f->cur][flow_cell_of(pos)];
}

vec2 flow_direction(const flow_t *f, vec2 pos)
{
    vec2 none = v2(0.0f, 0.0f);
    if (!f->ready)
        return none;
    const uint16_t *dist = f->dist[f->cur];
    int cell = flow_cell_of(pos);
    unsigned steps = f->steps[cell];
    uint16_t here = dist[cell];
    if (here == 0)
        return none;

    // Gradient estimate: every walkable downhill neighbour pulls in its
    // direction, weighted by how much closer it is. Unlike "step to the best
    // neighbour's centre", this does not wobble with the position inside the
    // cell. From an unreached or blocked cell (here == INF), any finite
    // neighbour is downhill, so bugs pushed against a rock still find a way.
    static const float UX[8] = { 1, -1, 0, 0, 0.7071f, 0.7071f, -0.7071f, -0.7071f };
    static const float UY[8] = { 0, 0, 1, -1, 0.7071f, -0.7071f, 0.7071f, -0.7071f };
    uint16_t best = here;
    for (int k = 0; k < 8; k++)
        if (steps & (1u << k)) {
            uint16_t d = dist[cell + OFF[k]];
            if (d < best)
                best = d;
        }
    if (best == here)
        return none; // a pocket the field never reached
    // Cap the drop so a single finite neighbour next to an INF cell does not
    // swamp the others.
    float gx = 0.0f, gy = 0.0f;
    for (int k = 0; k < 8; k++) {
        if (!(steps & (1u << k)))
            continue;
        uint16_t d = dist[cell + OFF[k]];
        if (d >= here)
            continue;
        float w = (float)(here - d > 4 ? 4 : here - d);
        gx += w * UX[k];
        gy += w * UY[k];
    }
    return v2_norm_or(v2(gx, gy), none);
}
