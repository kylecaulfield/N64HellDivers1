#include "bench.h"
#include "hud.h"
#include "weapons.h"

#include <libdragon.h>

#define FONT FONT_BUILTIN_DEBUG_MONO

static void bar(int x, int y, int w, int h, float frac, color_t fg)
{
    int fill = (int)((float)w * clampf(frac, 0.0f, 1.0f));
    rdpq_set_mode_fill(RGBA32(20, 20, 20, 0xFF));
    rdpq_fill_rectangle(x - 1, y - 1, x + w + 1, y + h + 1);
    if (fill > 0) {
        rdpq_set_mode_fill(fg);
        rdpq_fill_rectangle(x, y, x + fill, y + h);
    }
}

static void player_panel(const sim_t *sim, int idx, int x, int y)
{
    const player_t *p = &sim->players[idx];
    const weapon_slot_t *w = &p->weapons[p->weapon];
    const weapon_def_t *def = &weapon_defs[w->type];

    float hp = p->hp / PLAYER_HP;
    color_t hp_col = hp > 0.5f ? RGBA32(80, 210, 90, 0xFF)
                   : hp > 0.25f ? RGBA32(230, 190, 50, 0xFF) : RGBA32(230, 60, 50, 0xFF);
    bar(x + 26, y - 7, 70, 5, hp, hp_col);
    if (p->reload_timer)
        bar(x + 26, y + 5, 70, 2, 1.0f - (float)p->reload_timer / (float)def->reload_ticks,
            RGBA32(220, 220, 220, 0xFF));

    rdpq_text_printf(NULL, FONT, x, y, "P%d", idx + 1);
    rdpq_text_printf(NULL, FONT, x + 102, y, "%s %d/%d", def->name, w->mag, w->reserve);
    const char *status = sim->settings.scheme[idx] == SCHEME_B ? "CONTROLS B" : "CONTROLS A";
    if (p->reload_timer)
        status = "RELOADING";
    else if (w->mag == 0)
        status = w->reserve ? "RELOAD (B)" : "NO AMMO";
    rdpq_text_printf(NULL, FONT, x + 102, y + 11, "%s", status);
}

void hud_draw(const sim_t *sim, bool paused)
{
    player_panel(sim, 0, 12, 214);
    rdpq_text_printf(&(rdpq_textparms_t){ .width = 296, .align = ALIGN_RIGHT }, FONT, 12, 16,
                     "WAVE %u  KILLS %lu  BUGS %u", sim->waves.number, (unsigned long)sim->kills,
                     sim->counts.enemies);
    const char *bench = bench_status();
    if (bench)
        rdpq_text_printf(&(rdpq_textparms_t){ .width = 320, .align = ALIGN_CENTER }, FONT, 0, 30,
                         "%s", bench);

    const player_t *p1 = &sim->players[0];
    if (p1->active && p1->state == PSTATE_DEAD) {
        rdpq_text_printf(&(rdpq_textparms_t){ .width = 320, .align = ALIGN_CENTER }, FONT, 0, 110,
                         "DOWN  -  BACK IN %d", p1->state_timer / SIM_HZ + 1);
    }

    if (paused) {
        rdpq_textparms_t centre = { .width = 320, .align = ALIGN_CENTER };
        rdpq_text_printf(&centre, FONT, 0, 70, "PAUSED");
        rdpq_text_printf(&centre, FONT, 0, 96, "SCHEME A: STICK MOVE+AIM  Z FIRE+STRAFE");
        rdpq_text_printf(&centre, FONT, 0, 108, "A DIVE");
        rdpq_text_printf(&centre, FONT, 0, 126, "SCHEME B: STICK MOVE  C AIM+FIRE (8 DIR)");
        rdpq_text_printf(&centre, FONT, 0, 138, "Z DIVE");
        rdpq_text_printf(&centre, FONT, 0, 156, "BOTH: B TAP RELOAD  B HOLD SWITCH WEAPON");
        rdpq_text_printf(&centre, FONT, 0, 174, "L+DDOWN DEBUG MENU   L+DUP STATS");
    }
}
