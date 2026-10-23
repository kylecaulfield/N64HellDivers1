#include "arena.h"
#include "camera.h"
#include "collide.h"
#include "sim_internal.h"
#include "weapons.h"

bool sim_player_alive(const player_t *p)
{
    return p->active && p->state != PSTATE_DEAD;
}

void sim_spawn_player(sim_t *s, int port, vec2 pos)
{
    player_t *p = &s->players[port];
    bool present = p->present;
    *p = (player_t){
        .present = present,
        .active = true,
        .state = PSTATE_NORMAL,
        .pos = pos,
        .facing = v2(0.0f, 1.0f),
        .hp = PLAYER_HP,
    };
    p->weapons[0] = weapon_slot_full(WEAPON_RIFLE);
    p->weapons[1] = weapon_slot_full(WEAPON_SHOTGUN);
}

void sim_refill_ammo(sim_t *s, int port)
{
    player_t *p = &s->players[port];
    for (int i = 0; i < PLAYER_WEAPON_SLOTS; i++)
        p->weapons[i] = weapon_slot_full((weapon_type_t)p->weapons[i].type);
}

void player_damage(sim_t *s, int idx, float amount)
{
    player_t *p = &s->players[idx];
    if (!sim_player_alive(p) || s->settings.god_mode)
        return;
    p->hp -= amount;
    sim_push_event(s, EV_PLAYER_HURT, (uint8_t)idx, p->pos, p->facing);
    if (p->hp <= 0.0f) {
        p->hp = 0.0f;
        p->state = PSTATE_DEAD;
        p->state_timer = PLAYER_RESPAWN_TICKS;
        p->reload_timer = 0;
        p->vel = v2(0.0f, 0.0f);
        sim_push_event(s, EV_PLAYER_DIED, (uint8_t)idx, p->pos, p->facing);
    }
}

static void update_dead(sim_t *s, int idx)
{
    player_t *p = &s->players[idx];
    if (p->state_timer && --p->state_timer)
        return;
    // Placeholder until Reinforce (M3): respawn where the camera is looking.
    bool present = p->present;
    uint8_t weapon = p->weapon;
    sim_spawn_player(s, idx, s->camera.target);
    p->present = present;
    p->weapon = weapon;
}

static void update_one(sim_t *s, int idx, const player_intent_t *in)
{
    player_t *p = &s->players[idx];

    if (p->dive_cooldown)
        p->dive_cooldown--;

    bool can_act = false;
    switch (p->state) {
    case PSTATE_DEAD:
        update_dead(s, idx);
        return;

    case PSTATE_DIVING:
        // Momentum carries the dive; steering is ignored.
        if (--p->state_timer == 0) {
            p->state = PSTATE_PRONE;
            p->state_timer = PLAYER_PRONE_TICKS;
            p->vel = v2(0.0f, 0.0f);
        }
        break;

    case PSTATE_PRONE:
        p->vel = v2(0.0f, 0.0f);
        if (--p->state_timer == 0)
            p->state = PSTATE_NORMAL;
        break;

    default: {
        can_act = true;
        float mlen = v2_len(in->move);

        // Facing: explicit aim wins; otherwise follow movement unless locked.
        if (v2_len2(in->aim) > 0.0f)
            p->facing = in->aim;
        else if (!in->lock_facing && mlen > FACE_MIN_MOVE)
            p->facing = v2_scale(in->move, 1.0f / mlen);

        if (in->dive && p->dive_cooldown == 0) {
            vec2 dir = mlen > FACE_MIN_MOVE ? v2_scale(in->move, 1.0f / mlen) : p->facing;
            p->state = PSTATE_DIVING;
            p->state_timer = PLAYER_DIVE_TICKS;
            p->dive_cooldown = PLAYER_DIVE_TICKS + PLAYER_PRONE_TICKS + PLAYER_DIVE_COOLDOWN;
            p->vel = v2_scale(dir, PLAYER_DIVE_SPEED);
            p->facing = dir;
            p->reload_timer = 0; // diving interrupts a reload
            can_act = false;
        } else {
            float speed = in->lock_facing ? PLAYER_STRAFE_SPEED : PLAYER_SPEED;
            p->vel = v2_scale(in->move, speed);
        }
        break;
    }
    }

    weapon_update(s, idx,
                  can_act && in->fire,
                  can_act && in->reload,
                  can_act && in->switch_weapon,
                  p->facing);

    p->pos = v2_add(p->pos, v2_scale(p->vel, SIM_DT));
}

void players_update(sim_t *s, const player_intent_t intents[SIM_MAX_PLAYERS])
{
    camera_update_leash(s);
    for (int i = 0; i < SIM_MAX_PLAYERS; i++)
        if (s->players[i].active)
            update_one(s, i, &intents[i]);

    // Collisions: bugs shove players a little, rocks and the arena are solid,
    // and the shared camera's leash keeps everyone on screen.
    for (int i = 0; i < SIM_MAX_PLAYERS; i++) {
        player_t *p = &s->players[i];
        if (!sim_player_alive(p))
            continue;
        for (int e = 0; e < SIM_MAX_ENEMIES; e++) {
            const enemy_t *en = &s->enemies[e];
            if (en->active)
                push_out_circle(&p->pos, PLAYER_RADIUS, en->pos, GNASHER_RADIUS);
        }
        arena_resolve(&s->arena, &p->pos, PLAYER_RADIUS);
        p->pos.x = clampf(p->pos.x, s->camera.leash_min.x, s->camera.leash_max.x);
        p->pos.y = clampf(p->pos.y, s->camera.leash_min.y, s->camera.leash_max.y);
    }
}
