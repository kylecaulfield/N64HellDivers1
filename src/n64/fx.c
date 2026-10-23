#include "fx.h"
#include "rng.h"

static fx_particle_t pool[FX_MAX];
static int active;
static rng_t rng;

void fx_init(void)
{
    for (int i = 0; i < FX_MAX; i++)
        pool[i].life = 0;
    active = 0;
    rng_seed(&rng, 0xF00D);
}

static float frand(float lo, float hi)
{
    return lo + rng_float(&rng) * (hi - lo);
}

static void spawn(vec2 pos, float height, float vx, float vy, float vz,
                  int life, int size, uint8_t r, uint8_t g, uint8_t b, bool gravity)
{
    for (int i = 0; i < FX_MAX; i++) {
        fx_particle_t *p = &pool[i];
        if (p->life)
            continue;
        *p = (fx_particle_t){
            .x = pos.x, .y = height, .z = -pos.y,
            .vx = vx, .vy = vy, .vz = vz,
            .life = (uint8_t)life, .life_max = (uint8_t)life, .size = (uint8_t)size,
            .rgb = { r, g, b }, .gravity = gravity,
        };
        active++;
        return;
    }
}

// Particles flung outwards (and up) from a point.
static void burst(vec2 pos, float height, int count, float speed_lo, float speed_hi,
                  int life_lo, int life_hi, int size, const uint8_t (*colors)[3], int ncolors)
{
    for (int n = 0; n < count; n++) {
        float ang = frand(0.0f, 6.2832f), sp = frand(speed_lo, speed_hi);
        const uint8_t *c = colors[n % ncolors];
        int life = life_lo + (int)rng_range(&rng, (uint32_t)(life_hi - life_lo + 1));
        spawn(pos, height, cosf(ang) * sp, frand(1.5f, 4.5f), sinf(ang) * sp,
              life, size, c[0], c[1], c[2], true);
    }
}

static const uint8_t ICHOR[][3] = { { 150, 230, 60 }, { 110, 200, 40 } };
static const uint8_t CHITIN[][3] = { { 120, 70, 160 }, { 150, 230, 60 }, { 70, 40, 100 } };
static const uint8_t DUST[][3] = { { 190, 170, 140 }, { 150, 135, 110 } };
static const uint8_t BLOOD[][3] = { { 230, 50, 40 }, { 160, 30, 30 } };

void fx_spawn_from_events(const sim_t *s)
{
    for (int i = 0; i < s->event_count; i++) {
        const sim_event_t *e = &s->events[i];
        switch (e->type) {
        case EV_SHOT: {
            vec2 at = v2_add(e->pos, v2_scale(e->dir, 0.3f));
            bool shotgun = e->a == WEAPON_SHOTGUN;
            spawn(at, 1.0f, 0, 0, 0, 2, shotgun ? 90 : 60, 255, 235, 140, false);
            break;
        }
        case EV_HIT_ENEMY:
            burst(e->pos, 0.7f, 4, 2.0f, 4.0f, 6, 10, 28, ICHOR, 2);
            break;
        case EV_HIT_WORLD:
            burst(e->pos, 0.8f, 3, 1.0f, 2.5f, 6, 10, 26, DUST, 2);
            break;
        case EV_ENEMY_DIED:
            burst(e->pos, 0.6f, 12, 1.5f, 5.0f, 14, 24, 44, CHITIN, 3);
            break;
        case EV_PLAYER_HURT:
            burst(e->pos, 1.0f, 4, 1.5f, 3.0f, 6, 10, 30, BLOOD, 2);
            break;
        case EV_PLAYER_DIED:
            burst(e->pos, 0.8f, 14, 1.5f, 4.0f, 16, 26, 40, BLOOD, 2);
            break;
        default:
            break;
        }
    }
}

void fx_update(uint32_t ticks)
{
    for (uint32_t t = 0; t < ticks; t++) {
        for (int i = 0; i < FX_MAX; i++) {
            fx_particle_t *p = &pool[i];
            if (!p->life)
                continue;
            if (--p->life == 0) {
                active--;
                continue;
            }
            p->x += p->vx * SIM_DT;
            p->y += p->vy * SIM_DT;
            p->z += p->vz * SIM_DT;
            if (p->gravity) {
                p->vy -= 12.0f * SIM_DT;
                if (p->y < 0.05f) { // bounce-free landing: stop on the ground
                    p->y = 0.05f;
                    p->vx = p->vy = p->vz = 0.0f;
                }
            }
        }
    }
}

const fx_particle_t *fx_particles(void)
{
    return pool;
}

int fx_count(void)
{
    return active;
}
