#include "menu.h"
#include "bench.h"
#include "mission.h"
#include "overlay.h"

#include <libdragon.h>

#define FONT FONT_BUILTIN_DEBUG_MONO

enum {
    ITEM_WAVES,
    ITEM_SPAWN,
    ITEM_BOTS,
    ITEM_ZOOM,
    ITEM_GOD,
    ITEM_INF_AMMO,
    ITEM_REFILL,
    ITEM_KILL_ALL,
    ITEM_FREE_CALLDOWNS,
    ITEM_KILL_P1,
    ITEM_QUICK_MISSION,
    ITEM_COMPLETE_OBJECTIVE,
    ITEM_CAP,
    ITEM_FLOW,
    ITEM_BENCH,
    ITEM_OVERLAY,
    ITEM_CLOSE,
    ITEM_COUNT
};

static const uint16_t SPAWN_SIZES[] = { 10, 25, 50, 100 };
static const uint16_t FLOW_BUDGETS[] = { 256, 512, 1024, 2048, 4096 };
#define COUNT_OF(a) (int)(sizeof(a) / sizeof(a[0]))

static bool open;
static int cursor;
static int spawn_size; // index into SPAWN_SIZES

bool menu_is_open(void)
{
    return open;
}

void menu_toggle(void)
{
    open = !open;
    debugf("debug menu %s\n", open ? "open" : "closed");
}

static int step_index(int idx, int count, int dir)
{
    return (idx + (dir < 0 ? count - 1 : 1)) % count;
}

static int flow_index(uint16_t budget)
{
    for (int i = 0; i < COUNT_OF(FLOW_BUDGETS); i++)
        if (FLOW_BUDGETS[i] >= budget)
            return i;
    return COUNT_OF(FLOW_BUDGETS) - 1;
}

// dir: -1/+1 for D-Left/D-Right, 0 for A.
static void activate(sim_t *sim, int item, int dir)
{
    sim_settings_t *st = &sim->settings;
    switch (item) {
    case ITEM_WAVES:
        st->waves = !st->waves;
        break;
    case ITEM_SPAWN:
        if (dir)
            spawn_size = step_index(spawn_size, COUNT_OF(SPAWN_SIZES), dir);
        else
            sim_spawn_mix(sim, SPAWN_SIZES[spawn_size], sim->camera.target, SPAWN_RING_MIN, SPAWN_RING_MAX);
        break;
    case ITEM_BOTS:
        sim_set_bots(sim, !st->bots);
        break;
    case ITEM_ZOOM:
        st->force_max_zoom = !st->force_max_zoom;
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
    case ITEM_FREE_CALLDOWNS:
        st->free_calldowns = !st->free_calldowns;
        if (st->free_calldowns)
            for (int i = 0; i < SIM_MAX_PLAYERS; i++)
                for (int t = 0; t < CALLDOWN_COUNT; t++)
                    sim->cooldowns[i][t] = 0;
        break;
    case ITEM_KILL_P1:
        sim_kill_player(sim, 0);
        menu_toggle(); // watch the reinforce happen
        break;
    case ITEM_QUICK_MISSION:
        st->quick_mission = !st->quick_mission;
        break;
    case ITEM_COMPLETE_OBJECTIVE:
        mission_complete_objective(sim);
        menu_toggle();
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
    case ITEM_FLOW:
        st->flow_budget = FLOW_BUDGETS[step_index(flow_index(st->flow_budget), COUNT_OF(FLOW_BUDGETS), dir ? dir : 1)];
        debugf("flow budget %u cells/tick\n", st->flow_budget);
        break;
    case ITEM_BENCH:
        bench_start(sim);
        menu_toggle(); // the bench runs with the game unpaused
        break;
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
    const int x = 56, y0 = 24, lh = 11; // 17 items + title fit in 240 lines

    rdpq_set_mode_fill(RGBA32(10, 12, 18, 0xFF));
    rdpq_fill_rectangle(x - 10, y0 - 22, x + 218, y0 + (ITEM_COUNT + 1) * lh + 4);
    rdpq_text_printf(NULL, FONT, x, y0 - 8, "DEBUG MENU (paused)");

    for (int i = 0; i < ITEM_COUNT; i++) {
        int y = y0 + (i + 1) * lh;
        const char *m = i == cursor ? ">" : " ";
        switch (i) {
        case ITEM_WAVES:    rdpq_text_printf(NULL, FONT, x, y, "%s Waves: %s", m, on_off(st->waves)); break;
        case ITEM_SPAWN:    rdpq_text_printf(NULL, FONT, x, y, "%s Spawn %u mixed bugs <>", m, SPAWN_SIZES[spawn_size]); break;
        case ITEM_BOTS:     rdpq_text_printf(NULL, FONT, x, y, "%s Bots on free ports: %s", m, on_off(st->bots)); break;
        case ITEM_ZOOM:     rdpq_text_printf(NULL, FONT, x, y, "%s Force max zoom: %s", m, on_off(st->force_max_zoom)); break;
        case ITEM_GOD:      rdpq_text_printf(NULL, FONT, x, y, "%s God mode: %s", m, on_off(st->god_mode)); break;
        case ITEM_INF_AMMO: rdpq_text_printf(NULL, FONT, x, y, "%s Infinite ammo: %s", m, on_off(st->infinite_ammo)); break;
        case ITEM_REFILL:   rdpq_text_printf(NULL, FONT, x, y, "%s Refill ammo", m); break;
        case ITEM_KILL_ALL: rdpq_text_printf(NULL, FONT, x, y, "%s Kill all bugs", m); break;
        case ITEM_FREE_CALLDOWNS: rdpq_text_printf(NULL, FONT, x, y, "%s No calldown cooldowns: %s", m, on_off(st->free_calldowns)); break;
        case ITEM_KILL_P1:  rdpq_text_printf(NULL, FONT, x, y, "%s Kill P1 (test reinforce)", m); break;
        case ITEM_QUICK_MISSION: rdpq_text_printf(NULL, FONT, x, y, "%s Quick mission (1/%d times): %s", m, QUICK_MISSION_DIV, on_off(st->quick_mission)); break;
        case ITEM_COMPLETE_OBJECTIVE: rdpq_text_printf(NULL, FONT, x, y, "%s Complete the objective", m); break;
        case ITEM_CAP:      rdpq_text_printf(NULL, FONT, x, y, "%s Enemy cap: %u <>", m, st->enemy_cap); break;
        case ITEM_FLOW:     rdpq_text_printf(NULL, FONT, x, y, "%s Flow budget: %u <>", m, st->flow_budget); break;
        case ITEM_BENCH:    rdpq_text_printf(NULL, FONT, x, y, "%s Run stress bench (72 s)", m); break;
        case ITEM_OVERLAY:  rdpq_text_printf(NULL, FONT, x, y, "%s Stats overlay: %s", m, on_off(overlay_is_visible())); break;
        case ITEM_CLOSE:    rdpq_text_printf(NULL, FONT, x, y, "%s Close", m); break;
        }
    }
}
