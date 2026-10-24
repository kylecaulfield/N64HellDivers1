#include "fx.h"
#include "rng.h"

static fx_particle_t pool[FX_MAX];
static int active;
static rng_t rng;
static float shake;   // metres
static float flash;   // 0..1

void fx_init(void)
{
    for (int i = 0; i < FX_MAX; i++)
        pool[i].life = 0;
    active = 0;
    shake = flash = 0.0f;
    rng_seed(&rng, 0xF00D);
}

static float frand(float lo, float hi)
{
    return lo + rng_float(&rng) * (hi - lo);
}

static fx_particle_t *spawn(vec2 pos, float height, float vx, float vy, float vz,
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
        return p;
    }
    return NULL;
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
static const uint8_t SPARK[][3] = { { 255, 230, 160 }, { 200, 200, 200 } };
static const uint8_t FIRE[][3] = { { 255, 170, 50 }, { 255, 110, 30 }, { 255, 220, 120 } };
static const uint8_t SMOKE[][3] = { { 70, 64, 60 }, { 96, 90, 84 } };
static const uint8_t CORE[][3] = { { 255, 240, 200 }, { 255, 200, 90 } };
static const uint8_t REINFORCE[][3] = { { 90, 220, 255 }, { 220, 250, 255 } };
static const uint8_t PICKUP[][3] = { { 255, 200, 80 }, { 255, 255, 200 } };

// Bigger blasts near the middle of the screen shake harder.
static void kick(const sim_t *s, vec2 pos, float amount, float flash_amount)
{
    float d = v2_len(v2_sub(pos, s->camera.target));
    float k = clampf(1.0f - d / 30.0f, 0.2f, 1.0f);
    shake = fminf(shake + amount * k, 0.6f);
    flash = fminf(flash + flash_amount * k, 0.6f);
}

static void explosion(const sim_t *s, vec2 pos, bool big)
{
    // A few large semi-transparent cores, fast sparks, slow smoke.
    for (int n = 0; n < (big ? 4 : 3); n++) {
        float ang = frand(0.0f, 6.2832f), sp = frand(0.0f, 1.5f);
        fx_particle_t *p = spawn(pos, frand(0.3f, 1.2f), cosf(ang) * sp, frand(0.5f, 2.0f), sinf(ang) * sp,
                                 (int)frand(6.0f, 10.0f), big ? 150 : 120,
                                 CORE[n % 2][0], CORE[n % 2][1], CORE[n % 2][2], false);
        if (p)
            p->glow = true;
    }
    burst(pos, 0.5f, big ? 10 : 8, 3.0f, 8.0f, 8, 16, 30, FIRE, 3);
    for (int n = 0; n < 4; n++) {
        const uint8_t *c = SMOKE[n % 2];
        spawn(v2_add(pos, v2(frand(-0.8f, 0.8f), frand(-0.8f, 0.8f))), frand(0.4f, 1.0f),
              frand(-0.4f, 0.4f), frand(1.0f, 2.0f), frand(-0.4f, 0.4f),
              (int)frand(20.0f, 32.0f), 70, c[0], c[1], c[2], false);
    }
    kick(s, pos, big ? 0.45f : 0.35f, big ? 0.35f : 0.25f);
}

// A ring of dust thrown sideways by something landing.
static void impact(const sim_t *s, vec2 pos)
{
    for (int n = 0; n < 12; n++) {
        float ang = 6.2832f * (float)n / 12.0f, sp = frand(3.0f, 5.0f);
        const uint8_t *c = DUST[n % 2];
        spawn(pos, 0.2f, cosf(ang) * sp, frand(0.5f, 1.5f), sinf(ang) * sp,
              (int)frand(10.0f, 16.0f), 40, c[0], c[1], c[2], true);
    }
    kick(s, pos, 0.15f, 0.0f);
}

// Glowing motes rising from a point (reinforcements, pickups).
static void sparkle(vec2 pos, const uint8_t (*colors)[3], int count)
{
    for (int n = 0; n < count; n++) {
        const uint8_t *c = colors[n % 2];
        fx_particle_t *p = spawn(v2_add(pos, v2(frand(-0.5f, 0.5f), frand(-0.5f, 0.5f))), frand(0.2f, 1.0f),
                                 0.0f, frand(1.5f, 3.0f), 0.0f, (int)frand(12.0f, 20.0f), 36,
                                 c[0], c[1], c[2], false);
        if (p)
            p->glow = true;
    }
}

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
            if (e->a & EV_HIT_ARMORED) // glancing off plate: sparks, no ichor
                burst(e->pos, 1.0f, 3, 2.5f, 4.5f, 4, 7, 22, SPARK, 2);
            else
                burst(e->pos, 0.7f, 4, 2.0f, 4.0f, 6, 10, 28, ICHOR, 2);
            break;
        case EV_HIT_WORLD:
            burst(e->pos, 0.8f, 3, 1.0f, 2.5f, 6, 10, 26, DUST, 2);
            break;
        case EV_ENEMY_DIED:
            if (e->a == ENEMY_SKITTER)
                burst(e->pos, 0.3f, 6, 1.5f, 4.0f, 10, 18, 30, CHITIN, 3);
            else if (e->a == ENEMY_BULWARK)
                burst(e->pos, 1.0f, 20, 2.0f, 6.0f, 18, 30, 60, CHITIN, 3);
            else
                burst(e->pos, 0.6f, 12, 1.5f, 5.0f, 14, 24, 44, CHITIN, 3);
            break;
        case EV_PLAYER_HURT:
            burst(e->pos, 1.0f, 4, 1.5f, 3.0f, 6, 10, 30, BLOOD, 2);
            break;
        case EV_PLAYER_DIED:
            burst(e->pos, 0.8f, 14, 1.5f, 4.0f, 16, 26, 40, BLOOD, 2);
            break;
        case EV_EXPLOSION:
            if (e->a == BLAST_IMPACT)
                impact(s, e->pos);
            else
                explosion(s, e->pos, e->a == BLAST_BOMB);
            break;
        case EV_REINFORCED:
            sparkle(e->pos, REINFORCE, 10);
            break;
        case EV_PICKUP:
            sparkle(e->pos, PICKUP, 8);
            break;
        default:
            break;
        }
    }
}

void fx_update(uint32_t ticks)
{
    for (uint32_t t = 0; t < ticks; t++) {
        shake *= 0.8f;
        flash = fmaxf(flash - 0.12f, 0.0f);
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

vec2 fx_shake(void)
{
    if (shake < 0.01f)
        return v2(0.0f, 0.0f);
    float ang = frand(0.0f, 6.2832f);
    return v2(cosf(ang) * shake, sinf(ang) * shake);
}

float fx_flash(void)
{
    return flash;
}
