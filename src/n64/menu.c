#include "menu.h"
#include "overlay.h"

#include <libdragon.h>

#define FONT FONT_BUILTIN_DEBUG_MONO

enum {
    ITEM_SCHEME,
    ITEM_SPAWN_1,
    ITEM_SPAWN_10,
    ITEM_TRICKLE,
    ITEM_GOD,
    ITEM_INF_AMMO,
    ITEM_REFILL,
    ITEM_KILL_ALL,
    ITEM_CAP,
    ITEM_OVERLAY,
    ITEM_CLOSE,
    ITEM_COUNT
};

static bool open;
static int cursor;

bool menu_is_open(void)
{
    return open;
}

void menu_toggle(void)
{
    open = !open;
    debugf("debug menu %s\n", open ? "open" : "closed");
}

// dir: -1/+1 for D-Left/D-Right, 0 for A.
static void activate(sim_t *sim, int item, int dir)
{
    sim_settings_t *st = &sim->settings;
    switch (item) {
    case ITEM_SCHEME:
        st->scheme[0] = (uint8_t)((st->scheme[0] + 1) % SCHEME_COUNT);
        debugf("P1 control scheme %c\n", st->scheme[0] == SCHEME_A ? 'A' : 'B');
        break;
    case ITEM_SPAWN_1:
        sim_spawn_enemies_around(sim, ENEMY_GNASHER, 1, sim->camera.target);
        break;
    case ITEM_SPAWN_10:
        sim_spawn_enemies_around(sim, ENEMY_GNASHER, 10, sim->camera.target);
        break;
    case ITEM_TRICKLE:
        st->trickle_spawn = !st->trickle_spawn;
        break;
    case ITEM_GOD:
        st->god_mode = !st->god_mode;
        break;
    case ITEM_INF_AMMO:
        st->infinite_ammo = !st->infinite_ammo;
        break;
    case ITEM_REFILL:
        for (int i = 0; i < SIM_MAX_PLAYERS; i++)
            if (sim->players[i].active)
                sim_refill_ammo(sim, i);
        break;
    case ITEM_KILL_ALL:
        sim_kill_all_enemies(sim);
        break;
    case ITEM_CAP: {
        int cap = (int)st->enemy_cap + (dir < 0 ? -8 : 8);
        if (cap < 8)
            cap = 8;
        if (cap > SIM_MAX_ENEMIES)
            cap = SIM_MAX_ENEMIES;
        st->enemy_cap = (uint16_t)cap;
        debugf("enemy cap %d\n", cap);
        break;
    }
    case ITEM_OVERLAY:
        overlay_toggle();
        break;
    case ITEM_CLOSE:
        menu_toggle();
        break;
    }
}

void menu_update(uint16_t pressed, sim_t *sim)
{
    if (!open)
        return;
    if (pressed & SIM_BTN_DUP)
        cursor = (cursor + ITEM_COUNT - 1) % ITEM_COUNT;
    if (pressed & SIM_BTN_DDOWN)
        cursor = (cursor + 1) % ITEM_COUNT;
    if (pressed & SIM_BTN_A)
        activate(sim, cursor, 0);
    else if (pressed & SIM_BTN_DLEFT)
        activate(sim, cursor, -1);
    else if (pressed & SIM_BTN_DRIGHT)
        activate(sim, cursor, +1);
    if (pressed & SIM_BTN_B)
        menu_toggle();
}

static const char *on_off(bool v)
{
    return v ? "ON" : "OFF";
}

void menu_draw(const sim_t *sim)
{
    if (!open)
        return;
    const sim_settings_t *st = &sim->settings;
    const int x = 64, y0 = 44, lh = 12;

    rdpq_set_mode_fill(RGBA32(10, 12, 18, 0xFF));
    rdpq_fill_rectangle(x - 10, y0 - 22, x + 202, y0 + ITEM_COUNT * lh + 4);
    rdpq_text_printf(NULL, FONT, x, y0 - 8, "DEBUG MENU (paused)");

    for (int i = 0; i < ITEM_COUNT; i++) {
        int y = y0 + (i + 1) * lh;
        const char *mark = i == cursor ? ">" : " ";
        switch (i) {
        case ITEM_SCHEME:
            rdpq_text_printf(NULL, FONT, x, y, "%s P1 controls: %s", mark, st->scheme[0] == SCHEME_B ? "B" : "A");
            break;
        case ITEM_SPAWN_1:   rdpq_text_printf(NULL, FONT, x, y, "%s Spawn 1 gnasher", mark); break;
        case ITEM_SPAWN_10:  rdpq_text_printf(NULL, FONT, x, y, "%s Spawn 10 gnashers", mark); break;
        case ITEM_TRICKLE:   rdpq_text_printf(NULL, FONT, x, y, "%s Trickle spawn: %s", mark, on_off(st->trickle_spawn)); break;
        case ITEM_GOD:       rdpq_text_printf(NULL, FONT, x, y, "%s God mode: %s", mark, on_off(st->god_mode)); break;
        case ITEM_INF_AMMO:  rdpq_text_printf(NULL, FONT, x, y, "%s Infinite ammo: %s", mark, on_off(st->infinite_ammo)); break;
        case ITEM_REFILL:    rdpq_text_printf(NULL, FONT, x, y, "%s Refill ammo", mark); break;
        case ITEM_KILL_ALL:  rdpq_text_printf(NULL, FONT, x, y, "%s Kill all bugs", mark); break;
        case ITEM_CAP:       rdpq_text_printf(NULL, FONT, x, y, "%s Enemy cap: %u <>", mark, st->enemy_cap); break;
        case ITEM_OVERLAY:   rdpq_text_printf(NULL, FONT, x, y, "%s Stats overlay: %s", mark, on_off(overlay_is_visible())); break;
        case ITEM_CLOSE:     rdpq_text_printf(NULL, FONT, x, y, "%s Close", mark); break;
        }
    }
}
